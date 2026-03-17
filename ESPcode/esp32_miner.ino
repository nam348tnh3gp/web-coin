#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "DSHA1.h"
#include <time.h>

// Config
const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASS = "your_wifi_password";
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";

// Global
Preferences prefs;
String walletAddress = "";
String walletPassword = "";
String walletPublicKey = "";
String authCookie = "";

int cpuThreads = 2;
int cpuPercent = 100;
int difficultyOverride = 0;

volatile unsigned long totalHashes = 0;
volatile int blocksMined = 0;
volatile int totalReward = 0;

bool running = true;
unsigned long startTime;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

#define LED_PIN LED_BUILTIN

// Hash
String calculateBlockHash(int height, String prevHash, unsigned long timestamp, JsonArray transactions, unsigned long nonce) {

    String txString = "";
    for (JsonVariant v : transactions) {
        String tmp;
        serializeJson(v, tmp);
        txString += tmp;
    }

    char data[512];
    snprintf(data, sizeof(data), "%d%s%lu%s%lu",
             height, prevHash.c_str(), timestamp, txString.c_str(), nonce);

    DSHA1 sha1;
    sha1.write((const unsigned char*)data, strlen(data));

    unsigned char hashResult[20];
    sha1.finalize(hashResult);

    String hash = "";
    for (int i = 0; i < 20; i++) {
        if (hashResult[i] < 0x10) hash += "0";
        hash += String(hashResult[i], HEX);
    }
    hash.toLowerCase();
    return hash;
}

// API
bool login() {
    HTTPClient http;
    http.begin(String(SERVER_URL) + "/login");
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"displayAddress\":\"" + walletAddress + "\",\"password\":\"" + walletPassword + "\"}";
    int code = http.POST(payload);

    if (code == 200) {
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, http.getString());

        if (!doc.containsKey("error")) {
            walletPublicKey = doc["publicKey"].as<String>();

            String cookie = http.header("Set-Cookie");
            int semi = cookie.indexOf(';');
            authCookie = semi > 0 ? cookie.substring(0, semi) : cookie;

            http.end();
            return true;
        }
    }
    http.end();
    return false;
}

bool getNetwork(DynamicJsonDocument &doc) {
    HTTPClient http;
    http.begin(String(SERVER_URL) + "/info");
    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    int code = http.GET();
    if (code == 200) {
        String res = http.getString();
        http.end();
        if (!deserializeJson(doc, res) && doc.containsKey("latestBlock")) {
            return true;
        }
    }
    http.end();
    return false;
}

bool getPending(DynamicJsonDocument &doc) {
    HTTPClient http;
    http.begin(String(SERVER_URL) + "/pending");
    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    int code = http.GET();
    if (code == 200) {
        String res = http.getString();
        http.end();
        if (!deserializeJson(doc, res) && doc.size() > 0) {
            return true;
        }
    }
    http.end();
    return false;
}

bool submitBlock(int height, unsigned long nonce, String hash, String prevHash, int reward, JsonArray transactions) {

    HTTPClient http;
    http.begin(String(SERVER_URL) + "/blocks/submit");
    http.addHeader("Content-Type", "application/json");
    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    DynamicJsonDocument doc(8192);
    doc["height"] = height;
    doc["previousHash"] = prevHash;
    doc["timestamp"] = (unsigned long)time(nullptr) * 1000;
    doc["nonce"] = nonce;
    doc["hash"] = hash;
    doc["minerAddress"] = walletAddress;

    JsonArray txs = doc.createNestedArray("transactions");
    for (JsonVariant v : transactions) txs.add(v);

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    http.end();

    return code == 200;
}

// Mining
void miningTask(void* p) {
    int id = (int)(intptr_t)p;

    while (running) {

        if (WiFi.status() != WL_CONNECTED) {
            WiFi.reconnect();
            delay(1000);
            continue;
        }

        DynamicJsonDocument info(4096);
        if (!getNetwork(info)) {
            delay(1000);
            continue;
        }

        JsonObject latest = info["latestBlock"];

        int diff = difficultyOverride > 0 ? difficultyOverride : info["difficulty"];
        int reward = info["reward"];

        String target = "";
        for (int i = 0; i < diff; i++) target += "0";

        int height = latest["height"] + 1;
        String prevHash = latest["hash"].as<String>();

        DynamicJsonDocument pendingDoc(8192);
        JsonArray pending;
        if (getPending(pendingDoc)) pending = pendingDoc.as<JsonArray>();

        DynamicJsonDocument txDoc(8192);
        JsonArray txs = txDoc.to<JsonArray>();

        unsigned long timestamp = time(nullptr) * 1000;

        JsonObject coinbase = txs.createNestedObject();
        coinbase["from"] = nullptr;
        coinbase["to"] = walletPublicKey;
        coinbase["amount"] = reward;
        coinbase["timestamp"] = timestamp;
        coinbase["signature"] = nullptr;

        for (JsonObject t : pending) txs.add(t);

        if (id == 0) {
            Serial.printf("\n📦 Block %d | TX: %d | diff: %d\n", height, txs.size(), diff);
        }

        unsigned long nonce = id * 10000000UL;

        while (running) {

            if (nonce % 5000 == 0) {
                timestamp = time(nullptr) * 1000;
                txs[0]["timestamp"] = timestamp;
            }

            String hash = calculateBlockHash(height, prevHash, timestamp, txs, nonce);

            if (hash.startsWith(target)) {

                Serial.printf("\n🎯 FOUND T%d nonce=%lu\n", id, nonce);

                if (submitBlock(height, nonce, hash, prevHash, reward, txs)) {
                    portENTER_CRITICAL(&mux);
                    blocksMined++;
                    totalReward += reward;
                    portEXIT_CRITICAL(&mux);
                }

                break;
            }

            nonce++;

            if (nonce % 10000 == 0) {
                portENTER_CRITICAL(&mux);
                totalHashes += 10000;
                portEXIT_CRITICAL(&mux);
            }

            if (cpuPercent < 100 && nonce % 50000 == 0) delay(1);
            if (nonce % 5000 == 0) delay(0);
        }
    }

    vTaskDelete(NULL);
}

// Setup
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_PIN, OUTPUT);

    prefs.begin("webcoin", false);
    walletAddress = prefs.getString("wallet", "");
    walletPassword = prefs.getString("pass", "");

    if (walletAddress == "") {
        Serial.println("Nhập wallet:");
        while (!Serial.available());
        walletAddress = Serial.readStringUntil('\n');

        Serial.println("Nhập pass:");
        while (!Serial.available());
        walletPassword = Serial.readStringUntil('\n');

        prefs.putString("wallet", walletAddress);
        prefs.putString("pass", walletPassword);
    }

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi OK");

    configTime(0, 0, "pool.ntp.org");

    if (!login()) {
        Serial.println("Login fail");
        return;
    }

    Serial.println("Login OK");

    startTime = millis();

    for (int i = 0; i < cpuThreads; i++) {
        xTaskCreatePinnedToCore(miningTask, "mine", 8192, (void*)i, 1, NULL, i % 2);
    }
}

// loop
void loop() {

    static unsigned long last = 0;
    delay(5000);

    unsigned long now = millis();
    float speed = (totalHashes - last) / 5.0;

    Serial.printf("\n📊 Speed: %.2f H/s | Blocks: %d | Reward: %d\n",
                  speed, blocksMined, totalReward);

    last = totalHashes;

    int blinkDelay = (speed > 0) ? 1000 / max((int)(speed / 100), 1) : 1000;

    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(blinkDelay);
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}
