```cpp
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

bool login() {
    client.setInsecure();
    
    HTTPClient http;
    if (!http.begin(client, String(SERVER_URL) + "/login")) {
        Serial.println("[HTTP] Khong the ket noi!");
        return false;
    }
    
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"displayAddress\":\"" + String(config.wallet) +
                     "\",\"password\":\"" + String(config.password) + "\"}";

    Serial.println("\n----- DEBUG LOGIN -----");
    Serial.println("Gui len server: " + payload);

    int code = http.POST(payload);
    Serial.printf("Ma phan hoi HTTP: %d\n", code);

    if (code == 200) {
        String response = http.getString();
        Serial.println("Server tra ve: " + response);

        DynamicJsonDocument doc(2048);
        deserializeJson(doc, response);

        if (doc["error"].isNull()) {

            if (!doc["publicKey"].isNull()) {
                String pk = doc["publicKey"].as<String>();
                strncpy(config.publicKey, pk.c_str(), sizeof(config.publicKey) - 1);
                config.publicKey[sizeof(config.publicKey) - 1] = '\0';
                Serial.println("PublicKey nhan duoc: " + pk);
            }

            String cookie = http.header("Set-Cookie");
            if (cookie.length() > 0) {
                int semi = cookie.indexOf(';');
                authCookie = semi > 0 ? cookie.substring(0, semi) : cookie;
                Serial.println("Cookie: " + authCookie);
            }

            http.end();
            Serial.println("----- DANG NHAP THANH CONG -----\n");
            return true;
        } else {
            Serial.println("Loi tu server: " + doc["error"].as<String>());
        }
    } else {
        String response = http.getString();
        Serial.println("Loi response: " + response);
    }
    http.end();
    Serial.println("----- DANG NHAP THAT BAI -----\n");
    return false;
}

bool getNetwork(DynamicJsonDocument &doc) {
    HTTPClient http;
    if (!http.begin(client, String(SERVER_URL) + "/info")) {
        return false;
    }

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

bool getPending(DynamicJsonDocument &doc) {
    HTTPClient http;
    if (!http.begin(client, String(SERVER_URL) + "/pending")) {
        return false;
    }

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

bool submitBlock(int height, unsigned long nonce, String hash, String prevHash, int reward, String txStr) {
    HTTPClient http;
    if (!http.begin(client, String(SERVER_URL) + "/blocks/submit")) {
        return false;
    }
    
    http.addHeader("Content-Type", "application/json");
    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    DynamicJsonDocument doc(4096);
    doc["height"] = height;
    doc["previousHash"] = prevHash;
    doc["timestamp"] = (unsigned long)time(nullptr) * 1000;
    doc["nonce"] = nonce;
    doc["hash"] = hash;
    doc["minerAddress"] = String(config.wallet);

    DynamicJsonDocument txDoc(4096);
    deserializeJson(txDoc, txStr);
    doc["transactions"] = txDoc.as<JsonArray>();

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    http.end();

    return code == 200;
}

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

void resetWalletInfo() {
    Serial.println("\n----- RESET THONG TIN VI -----");
    config.version = CONFIG_VERSION;
    strcpy(config.wallet, "");
    strcpy(config.password, "");
    strcpy(config.publicKey, "");
    config.difficulty = 0;
    saveConfig();
    Serial.println("Da xoa thong tin cu. Khoi dong lai de nhap moi!");
    delay(2000);
    ESP.restart();
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    Serial.println("\n\n=================================");
    Serial.println("   WebCoin Miner cho ESP8266");
    Serial.println("=================================");

    loadConfig();

    Serial.println("Thong tin hien tai:");
    
    Serial.print("Dia chi vi: ");
    if (strlen(config.wallet) > 0) {
        Serial.println(config.wallet);
    } else {
        Serial.println("Chua co");
    }
    
    Serial.print("Mat khau: ");
    if (strlen(config.password) > 0) {
        Serial.println("Da luu");
    } else {
        Serial.println("Chua co");
    }
    
    Serial.print("PublicKey: ");
    if (strlen(config.publicKey) > 0) {
        Serial.println(config.publicKey);
    } else {
        Serial.println("Chua co");
    }

    if (strlen(config.wallet) == 0 || strlen(config.password) == 0) {
        Serial.println("\n----- NHAP THONG TIN VI -----");
        
        Serial.print("Nhap dia chi vi (bat dau bang W_...): ");
        while (!Serial.available());
        String w = Serial.readStringUntil('\n');
        w.trim();
        strncpy(config.wallet, w.c_str(), sizeof(config.wallet) - 1);
        Serial.println("Da nhan: " + w);
        
        Serial.print("Nhap mat khau: ");
        while (!Serial.available());
        String p = Serial.readStringUntil('\n');
        p.trim();
        strncpy(config.password, p.c_str(), sizeof(config.password) - 1);
        Serial.println("Da nhan: " + p);

        saveConfig();
        Serial.println("[OK] Da luu thong tin vi!");
    }

    Serial.printf("\n[WiFi] Dang ket noi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 60) {
        delay(500);
        Serial.print(".");
        wifiAttempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Da ket noi! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Khong the ket noi! Kiem tra lai ten va mat khau WiFi.");
        return;
    }

    Serial.print("[NTP] Dang dong bo thoi gian");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 100000 && attempts < 60) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        attempts++;
    }

    if (now >= 100000) {
        Serial.printf("\n[NTP] Thoi gian: %s", ctime(&now));
    } else {
        Serial.println("\n[NTP] Khong the dong bo thoi gian!");
        return;
    }

    if (!login()) {
        Serial.println("\n[LOGIN] That bai! Kiem tra lai dia chi vi va mat khau.");
        Serial.println("Ban co muon nhap lai khong? (y/n)");
        
        unsigned long timeout = millis() + 5000;
        while (millis() < timeout) {
            if (Serial.available()) {
                char c = Serial.read();
                if (c == 'y' || c == 'Y') {
                    resetWalletInfo();
                }
            }
            delay(100);
        }
        return;
    }

    if (strlen(config.publicKey) == 0) {
        saveConfig();
    }

    Serial.println("[LOGIN] Thanh cong!");
    startTime = millis();
    Serial.println("\n[Miner] San sang! Dang dao...\n");
}

void loop() {
    static unsigned long lastStats = 0;
    static unsigned long lastLedBlink = 0;
    static bool ledState = false;
    static unsigned long lastLoginCheck = 0;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Mat ket noi, dang ket noi lai...");
        WiFi.reconnect();
        delay(5000);
        return;
    }

    if (millis() - lastLoginCheck > 60000) {
        if (authCookie.length() == 0) {
            Serial.println("[WARN] Mat cookie, dang dang nhap lai...");
            if (!login()) {
                Serial.println("[ERROR] Khong the dang nhap lai!");
            }
        }
        lastLoginCheck = millis();
    }

    DynamicJsonDocument info(2048);
    if (!getNetwork(info)) {
        delay(2000);
        return;
    }

    JsonObject latest = info["latestBlock"];

    int diff = config.difficulty > 0 ? config.difficulty : info["difficulty"].as<int>();
    int reward = info["reward"].as<int>();

    String target = "";
    for (int i = 0; i < diff; i++) target += "0";

    int height = latest["height"].as<int>() + 1;
    String prevHash = latest["hash"].as<String>();

    unsigned long ts = time(nullptr) * 1000;
    String txStr = "[{\"from\":null,\"to\":\"" + String(config.publicKey) +
                   "\",\"amount\":" + String(reward) +
                   ",\"timestamp\":" + String(ts) +
                   ",\"signature\":null}";

    DynamicJsonDocument pendingDoc(2048);
    if (getPending(pendingDoc)) {
        for (JsonObject tx : pendingDoc.as<JsonArray>()) {
            String tmp;
            serializeJson(tx, tmp);
            txStr += "," + tmp;
        }
    }
    txStr += "]";

    Serial.printf("\n[BLOCK %d] Do kho: %d | Thuong: %d\n", height, diff, reward);
    Serial.printf("Bat dau dao...\n");

    unsigned long nonce = 0;
    unsigned long localHashCount = 0;

    while (true) {
        if (millis() - lastLedBlink > 500) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? LOW : HIGH);
            lastLedBlink = millis();
        }

        String hash = calculateHash(height, prevHash, ts, txStr, nonce);
        localHashCount++;
        totalHashes++;

        if (hash.startsWith(target)) {
            digitalWrite(LED_PIN, HIGH);
            Serial.printf("\n[FOUND] Nonce: %lu | Hash: %s\n", nonce, hash.c_str());
            
            if (submitBlock(height, nonce, hash, prevHash, reward, txStr)) {
                blocksMined++;
                totalReward += reward;
                Serial.printf("[OK] Block %d duoc chap nhan! +%d WebCoin\n", height, reward);
            } else {
                Serial.printf("[FAIL] Block %d bi tu choi\n", height);
            }
            break;
        }

        nonce++;

        if (nonce % 10000 == 0) {
            float speed = totalHashes / ((millis() - startTime) / 1000.0);
            Serial.printf("\rSpeed: %.2f H/s | Total: %d | Blocks: %d | Reward: %d", 
                         speed, totalHashes, blocksMined, totalReward);
            yield();
        }

        if (nonce % 5000 == 0) {
            delay(0);
        }
    }

    delay(100);
}
```
