#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "DSHA1.h"
#include <time.h>

const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASS = "your_wifi_password";
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";

#define EEPROM_SIZE 512
#define CONFIG_VERSION 0x02
#define LED_PIN LED_BUILTIN

struct Config {
    uint8_t version;
    char wallet[100];
    char password[64];
    char publicKey[256];
    uint8_t difficulty;
} config;

WiFiClientSecure client;
String authCookie = "";

volatile unsigned long totalHashes = 0;
volatile int blocksMined = 0;
volatile int totalReward = 0;

unsigned long startTime;
bool running = true;

// Hash
String calculateHash(int height, String prevHash, unsigned long ts, String txStr, unsigned long nonce) {
    char data[512];
    snprintf(data, sizeof(data), "%d%s%lu%s%lu",
             height, prevHash.c_str(), ts, txStr.c_str(), nonce);

    DSHA1 sha1;
    sha1.write((const unsigned char*)data, strlen(data));

    unsigned char out[20];
    sha1.finalize(out);

    String hash = "";
    for (int i = 0; i < 20; i++) {
        if (out[i] < 0x10) hash += "0";
        hash += String(out[i], HEX);
    }
    hash.toLowerCase();
    return hash;
}

// login
bool login() {
    client.setInsecure();

    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/login");
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"displayAddress\":\"" + String(config.wallet) +
                     "\",\"password\":\"" + String(config.password) + "\"}";

    int code = http.POST(payload);

    if (code == 200) {
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, http.getString());

        if (doc["error"].isNull()) {
            if (!doc["publicKey"].isNull()) {
                strcpy(config.publicKey, doc["publicKey"]);
            }

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

// get network
bool getNetwork(DynamicJsonDocument &doc) {
    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/info");

    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    int code = http.GET();
    if (code == 200) {
        String res = http.getString();
        http.end();
        if (!deserializeJson(doc, res) && !doc["latestBlock"].isNull()) {
            return true;
        }
    }
    http.end();
    return false;
}

// get pending
bool getPending(DynamicJsonDocument &doc) {
    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/pending");

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

// submit
bool submitBlock(int height, unsigned long nonce, String hash, String prevHash, int reward, String txStr) {
    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/blocks/submit");
    http.addHeader("Content-Type", "application/json");

    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    DynamicJsonDocument doc(4096);

    doc["height"] = height;
    doc["previousHash"] = prevHash;
    doc["timestamp"] = (unsigned long)time(nullptr) * 1000;
    doc["nonce"] = nonce;
    doc["hash"] = hash;
    doc["minerAddress"] = String(config.wallet);

    // TX string parse 
    DynamicJsonDocument txDoc(4096);
    deserializeJson(txDoc, txStr);
    doc["transactions"] = txDoc.as<JsonArray>();

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    http.end();

    return code == 200;
}

// EEPROM
void saveConfig() {
    EEPROM.put(0, config);
    EEPROM.commit();
}

void loadConfig() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, config);

    if (config.version != CONFIG_VERSION) {
        config.version = CONFIG_VERSION;
        strcpy(config.wallet, "");
        strcpy(config.password, "");
        strcpy(config.publicKey, "");
        config.difficulty = 0;
        saveConfig();
    }
}

// setup
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_PIN, OUTPUT);

    loadConfig();

    if (strlen(config.wallet) == 0) {
        Serial.println("Nhap wallet:");
        while (!Serial.available());
        String w = Serial.readStringUntil('\n');
        strcpy(config.wallet, w.c_str());

        Serial.println("Nhap password:");
        while (!Serial.available());
        String p = Serial.readStringUntil('\n');
        strcpy(config.password, p.c_str());

        saveConfig();
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
}

// mining loop
void loop() {

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        delay(1000);
        return;
    }

    DynamicJsonDocument info(4096);
    if (!getNetwork(info)) {
        delay(1000);
        return;
    }

    JsonObject latest = info["latestBlock"];

    int diff = config.difficulty > 0 ? config.difficulty : info["difficulty"];
    int reward = info["reward"];

    String target = "";
    for (int i = 0; i < diff; i++) target += "0";

    int height = latest["height"] + 1;
    String prevHash = latest["hash"].as<String>();

    DynamicJsonDocument pendingDoc(4096);
    String txStr = "[";

    unsigned long ts = time(nullptr) * 1000;

    // coinbase
    txStr += "{\"from\":null,\"to\":\"" + String(config.publicKey) +
             "\",\"amount\":" + String(reward) +
             ",\"timestamp\":" + String(ts) +
             ",\"signature\":null}";

    if (getPending(pendingDoc)) {
        for (JsonObject tx : pendingDoc.as<JsonArray>()) {
            String tmp;
            serializeJson(tx, tmp);
            txStr += "," + tmp;
        }
    }

    txStr += "]";

    Serial.printf("\n📦 Block %d | diff %d\n", height, diff);

    unsigned long nonce = 0;

    while (true) {

        String hash = calculateHash(height, prevHash, ts, txStr, nonce);

        totalHashes++;

        if (hash.startsWith(target)) {

            Serial.printf("\n🎯 FOUND nonce=%lu\n", nonce);

            if (submitBlock(height, nonce, hash, prevHash, reward, txStr)) {
                blocksMined++;
                totalReward += reward;
            }

            break;
        }

        nonce++;

        if (nonce % 5000 == 0) delay(0); // watchdog
    }

    delay(100);
}
