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

String bytesToHex(uint8_t* bytes, size_t len) {
    String hex = "";
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] < 0x10) hex += "0";
        hex += String(bytes[i], HEX);
    }
    hex.toLowerCase();
    return hex;
}

String generateSalt(size_t length) {
    uint8_t* buffer = (uint8_t*)malloc(length);
    if (!buffer) return "";
    for (size_t i = 0; i < length; i++) {
        buffer[i] = esp_random() & 0xFF;
    }
    String salt = bytesToHex(buffer, length);
    free(buffer);
    return salt;
}

String calculateHMAC(const String& data, const String& key, const String& salt) {
    String hmacInput = key + salt;
    uint8_t hmacResult[32];
    
    BearSSL::HashSHA256 hash;
    BearSSL::HmacSHA256 hmac;
    
    hmac.begin((const uint8_t*)hmacInput.c_str(), hmacInput.length());
    hmac.add((const uint8_t*)data.c_str(), data.length());
    hmac.end(hmacResult);
    
    return bytesToHex(hmacResult, 32);
}

String calculateBlockHash(int height, String prevHash, unsigned long ts, 
                         JsonArray transactions, unsigned long nonce,
                         const String& miningSalt, const String& blockSalt) {
    String txStr = "";
    for (JsonVariant v : transactions) {
        String tmp;
        serializeJson(v, tmp);
        txStr += tmp;
    }
    
    char data[1024];
    snprintf(data, sizeof(data), "%d%s%lu%s%lu%s%s",
             height, prevHash.c_str(), ts, txStr.c_str(), nonce,
             miningSalt.c_str(), blockSalt.c_str());

    DSHA1 sha1;
    sha1.write((const unsigned char*)data, strlen(data));

    unsigned char out[20];
    sha1.finalize(out);

    return bytesToHex(out, 20);
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

bool submitBlock(int height, unsigned long nonce, String hash, String prevHash, 
                int reward, JsonArray transactions, const String& blockHMAC,
                const String& workerSalt, const String& miningSalt, 
                const String& blockSalt) {
    HTTPClient http;
    if (!http.begin(client, String(SERVER_URL) + "/blocks/submit")) {
        return false;
    }
    
    http.addHeader("Content-Type", "application/json");
    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    DynamicJsonDocument doc(8192);
    doc["height"] = height;
    doc["previousHash"] = prevHash;
    doc["timestamp"] = (unsigned long)time(nullptr) * 1000;
    doc["nonce"] = nonce;
    doc["hash"] = hash;
    doc["minerAddress"] = String(config.wallet);
    doc["blockHMAC"] = blockHMAC;
    doc["workerSalt"] = workerSalt;
    doc["miningSalt"] = miningSalt;
    doc["blockSalt"] = blockSalt;

    JsonArray txs = doc.createNestedArray("transactions");
    for (JsonVariant v : transactions) {
        txs.add(v);
    }

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
    Serial.println("   WebCoin Miner cho ESP8266 v2.0");
    Serial.println("   (SHA1 + HMAC + Salt)");
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

    DynamicJsonDocument info(4096);
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

    DynamicJsonDocument txDoc(8192);
    JsonArray transactions = txDoc.to<JsonArray>();

    String txSalt = generateSalt(16);
    String txData = String(config.publicKey) + String(reward) + String(ts) + txSalt;
    String txHash = calculateBlockHash(height, prevHash, ts, transactions, 0, "", "");
    String txHMAC = calculateHMAC(txData, config.publicKey, txSalt);

    JsonObject coinbase = transactions.createNestedObject();
    coinbase["from"] = nullptr;
    coinbase["to"] = config.publicKey;
    coinbase["amount"] = reward;
    coinbase["timestamp"] = ts;
    coinbase["signature"] = nullptr;
    coinbase["salt"] = txSalt;
    coinbase["hmac"] = txHMAC;

    DynamicJsonDocument pendingDoc(4096);
    if (getPending(pendingDoc)) {
        for (JsonObject tx : pendingDoc.as<JsonArray>()) {
            transactions.add(tx);
        }
    }

    String miningSalt = generateSalt(8);
    String blockSalt = generateSalt(8);

    Serial.printf("\n[BLOCK %d] TX: %d | Do kho: %d | Thuong: %d\n", 
                  height, transactions.size(), diff, reward);
    Serial.printf("Mining Salt: %s\n", miningSalt.c_str());
    Serial.printf("Block Salt: %s\n", blockSalt.c_str());

    unsigned long nonce = 0;
    unsigned long localHashCount = 0;
    unsigned long start = millis();

    while (true) {
        if (millis() - lastLedBlink > 500) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? LOW : HIGH);
            lastLedBlink = millis();
        }

        String hash = calculateBlockHash(height, prevHash, ts, transactions, nonce, miningSalt, blockSalt);
        localHashCount++;
        totalHashes++;

        if (hash.startsWith(target)) {
            digitalWrite(LED_PIN, HIGH);
            
            unsigned long elapsed = (millis() - start) / 1000;
            float hashrate = localHashCount / (elapsed > 0 ? elapsed : 1);
            
            Serial.printf("\n[FOUND] Nonce: %lu | Hash: %s\n", nonce, hash.c_str());
            Serial.printf("Hashrate: %.2f H/s\n", hashrate);

            String workerSalt = generateSalt(16);
            String blockData = String(height) + hash + prevHash + String(nonce);
            String blockHMAC = calculateHMAC(blockData, config.publicKey, workerSalt);

            Serial.printf("Block HMAC: %s\n", blockHMAC.c_str());

            if (submitBlock(height, nonce, hash, prevHash, reward, 
                           transactions, blockHMAC, workerSalt, miningSalt, blockSalt)) {
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
