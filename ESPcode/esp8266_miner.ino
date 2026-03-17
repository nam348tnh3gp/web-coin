/*
 * ESP8266miner fixed - KHÔNG DÙNG containsKey
 * Chạy trên NodeMCU, Wemos D1, ESP-01, v.v.
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <BearSSLHelpers.h>
#include <EEPROM.h>
#include "DSHA1.h"
#include <time.h>

// ============== CẤU HÌNH ==============
const char* WIFI_SSID = "your_wifi_ssid";        // Đã sửa: thêm dấu "
const char* WIFI_PASS = "your_wifi_password";    // Đã sửa: thêm dấu "
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";
const int SOC_TIMEOUT = 10;

// Cấu hình EEPROM
#define EEPROM_SIZE 512
#define CONFIG_VERSION 0x02

// ============== CẤU TRÚC LƯU CONFIG ==============
struct Config {
    uint8_t version;
    char wallet[100];
    char password[64];
    char publicKey[256];
    uint8_t threads;
    uint8_t cpuPercent;
    uint8_t difficulty;
} config;

// ============== BIẾN TOÀN CỤC ==============
WiFiClientSecure client;
String authCookie = "";
volatile unsigned long totalHashes = 0;
volatile int blocksMined = 0;
volatile int totalReward = 0;
unsigned long startTime = 0;
bool running = true;

// LED indicator
#define LED_PIN LED_BUILTIN

// Hàm tạo dòng kẻ
String createLine(int length, char c = '=') {
    String line = "";
    for (int i = 0; i < length; i++) {
        line += c;
    }
    return line;
}

// ============== HÀM HASH DÙNG DSHA1 ==============
String calculateBlockHash(int height, String prevHash, unsigned long timestamp, JsonArray transactions, unsigned long nonce) {
    String txString = "";
    for (size_t i = 0; i < transactions.size(); i++) {
        String txJson;
        serializeJson(transactions[i], txJson);
        txString += txJson;
    }
    
    String data = String(height) + prevHash + String(timestamp) + txString + String(nonce);
    
    DSHA1 sha1;
    sha1.write((const unsigned char*)data.c_str(), data.length());
    
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

// ============== API FUNCTIONS ==============
bool login(String wallet, String password) {
    client.setInsecure();
    
    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/login");
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{\"displayAddress\":\"" + wallet + "\",\"password\":\"" + password + "\"}";
    int httpCode = http.POST(payload);
    
    if (httpCode == 200) {
        String response = http.getString();
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, response);
        
        // Cách 2: Dùng .isNull() thay vì containsKey
        if (doc["error"].isNull()) {  // Không có lỗi
            // Lưu publicKey từ response - dùng .success()
            if (doc["publicKey"].success()) {
                String pubKey = doc["publicKey"].as<String>();
                strcpy(config.publicKey, pubKey.c_str());
            }
            
            // Lấy cookie từ header
            String cookie = http.header("Set-Cookie");
            if (cookie.length() > 0) {
                int semi = cookie.indexOf(';');
                if (semi > 0) {
                    authCookie = cookie.substring(0, semi);
                } else {
                    authCookie = cookie;
                }
            }
            http.end();
            return true;
        }
    }
    http.end();
    return false;
}

DynamicJsonDocument* getNetworkInfo() {
    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/info");
    http.addHeader("Content-Type", "application/json");
    if (authCookie.length() > 0) {
        http.addHeader("Cookie", authCookie);
    }
    
    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        DynamicJsonDocument* doc = new DynamicJsonDocument(4096);
        deserializeJson(*doc, response);
        http.end();
        
        // Kiểm tra có dữ liệu không
        if ((*doc)["latestBlock"].success()) {
            return doc;
        } else {
            delete doc;
            return nullptr;
        }
    }
    http.end();
    return nullptr;
}

DynamicJsonDocument* getPending() {
    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/pending");
    http.addHeader("Content-Type", "application/json");
    if (authCookie.length() > 0) {
        http.addHeader("Cookie", authCookie);
    }
    
    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        DynamicJsonDocument* doc = new DynamicJsonDocument(16384);
        deserializeJson(*doc, response);
        http.end();
        
        // Kiểm tra có dữ liệu không
        if (!(*doc).isNull()) {
            return doc;
        } else {
            delete doc;
            return nullptr;
        }
    }
    http.end();
    return nullptr;
}

bool submitBlock(int height, unsigned long nonce, String hash, String prevHash, int reward, JsonArray transactions) {
    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/blocks/submit");
    http.addHeader("Content-Type", "application/json");
    if (authCookie.length() > 0) {
        http.addHeader("Cookie", authCookie);
    }
    
    DynamicJsonDocument doc(16384);
    doc["height"] = height;
    doc["previousHash"] = prevHash;
    doc["timestamp"] = (unsigned long)time(nullptr) * 1000;
    doc["nonce"] = nonce;
    doc["hash"] = hash;
    doc["minerAddress"] = String(config.wallet);
    
    JsonArray txs = doc.createNestedArray("transactions");
    for (size_t i = 0; i < transactions.size(); i++) {
        txs.add(transactions[i]);
    }
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.printf("📤 Submitting block #%d with %d transactions\n", height, transactions.size());
    
    int httpCode = http.POST(payload);
    String response = http.getString();
    http.end();
    
    if (httpCode == 200) {
        Serial.println("✅ Block accepted!");
        return true;
    } else {
        Serial.printf("❌ Block rejected: HTTP %d - %s\n", httpCode, response.c_str());
        return false;
    }
}

// ============== LƯU/ĐỌC CONFIG ==============
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
        config.threads = 1;
        config.cpuPercent = 100;
        config.difficulty = 0;
        saveConfig();
    }
}

// ============== SETUP ==============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    String line = createLine(60);
    Serial.println("\n" + line);
    Serial.println(" WEBCCOIN ESP8266 MINER - FIXED VERSION");
    Serial.println(createLine(59));
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    loadConfig();
    
    if (strlen(config.wallet) == 0) {
        Serial.println("\n📝 NHẬP THÔNG TIN:");
        
        Serial.print("Địa chỉ ví (W_...): ");
        while (!Serial.available());
        String input = Serial.readStringUntil('\n');
        input.trim();
        strcpy(config.wallet, input.c_str());
        
        Serial.print("Mật khẩu ví: ");
        while (!Serial.available());
        input = Serial.readStringUntil('\n');
        input.trim();
        strcpy(config.password, input.c_str());
        
        Serial.print("Độ khó (1-4, 0=auto) [0]: ");
        while (!Serial.available());
        input = Serial.readStringUntil('\n');
        config.difficulty = input.toInt();
        if (config.difficulty > 4) config.difficulty = 0;
        
        Serial.println("\n🔑 Đang đăng nhập để lấy publicKey...");
        
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            if (login(config.wallet, config.password)) {
                Serial.println("\n✅ Đăng nhập thành công, đã lấy publicKey");
                saveConfig();
                Serial.println("✅ Config saved");
            } else {
                Serial.println("\n❌ Đăng nhập thất bại! Kiểm tra lại thông tin.");
                return;
            }
        } else {
            Serial.println("\n❌ WiFi failed!");
            return;
        }
    }
    
    Serial.printf("\n📊 Wallet: %s\n", config.wallet);
    Serial.printf("   PublicKey: %s...\n", String(config.publicKey).substring(0, 30).c_str());
    Serial.printf("   Difficulty: %s\n", config.difficulty == 0 ? "Auto" : String(config.difficulty).c_str());
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("\n📡 Connecting to %s", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            attempts++;
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi connected");
        Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
        digitalWrite(LED_PIN, LOW);
    } else {
        Serial.println("\n❌ WiFi failed!");
        return;
    }
    
    Serial.print("\n🕒 Syncing NTP...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    time_t now = time(nullptr);
    while (now < 100000) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.printf("\n✅ Time synced: %s", ctime(&now));
    
    if (strlen(config.publicKey) == 0) {
        Serial.print("\n🔐 Logging in... ");
        if (login(config.wallet, config.password)) {
            Serial.println("✅ Success!");
            saveConfig();
        } else {
            Serial.println("❌ Failed!");
            return;
        }
    } else {
        Serial.print("\n🔐 Logging in... ");
        if (login(config.wallet, config.password)) {
            Serial.println("✅ Success!");
        } else {
            Serial.println("❌ Failed!");
            return;
        }
    }
    
    DynamicJsonDocument* info = getNetworkInfo();
    if (info) {
        Serial.printf("📡 Network: diff=%d, reward=%d WBC, pending=%d\n", 
                     (*info)["difficulty"].as<int>(), 
                     (*info)["reward"].as<int>(),
                     (*info)["pendingCount"].as<int>());
        delete info;
    }
    
    Serial.println("\n🚀 Starting miner...");
    Serial.println(createLine(59));
    
    startTime = millis();
}

// ============== HÀM ĐÀO ==============
void mine() {
    static unsigned long lastInfoTime = 0;
    static int currentHeight = 0;
    static String prevHash = "";
    static int targetDiff = 0;
    static String target = "";
    static int baseReward = 0;
    static DynamicJsonDocument* pendingDoc = nullptr;
    static unsigned long timestamp = 0;
    static DynamicJsonDocument coinbaseDoc(512);
    static DynamicJsonDocument transactionsDoc(16384);
    static JsonArray transactions;
    
    if (millis() - lastInfoTime > 30000 || currentHeight == 0) {
        DynamicJsonDocument* info = getNetworkInfo();
        // Cách 2: Dùng .success() thay vì .containsKey()
        if (info && (*info)["latestBlock"].success()) {
            JsonObject latest = (*info)["latestBlock"];
            currentHeight = latest["height"].as<int>() + 1;
            prevHash = latest["hash"].as<String>();
            
            int networkDiff = (*info)["difficulty"] | 3;
            baseReward = (*info)["reward"] | 48;
            
            targetDiff = config.difficulty > 0 ? config.difficulty : networkDiff;
            target = "";
            for (int i = 0; i < targetDiff; i++) target += "0";
            
            if (pendingDoc) delete pendingDoc;
            pendingDoc = getPending();
            JsonArray pending = pendingDoc ? pendingDoc->as<JsonArray>() : JsonArray();
            
            time_t now = time(nullptr);
            timestamp = (unsigned long)now * 1000;
            
            coinbaseDoc.clear();
            coinbaseDoc["from"] = nullptr;
            coinbaseDoc["to"] = config.publicKey;
            coinbaseDoc["amount"] = baseReward;
            coinbaseDoc["timestamp"] = timestamp;
            coinbaseDoc["signature"] = nullptr;
            
            transactionsDoc.clear();
            transactions = transactionsDoc.to<JsonArray>();
            transactions.add(coinbaseDoc.as<JsonVariant>());
            
            for (size_t i = 0; i < pending.size(); i++) {
                JsonObject pendingTx = pending[i];
                DynamicJsonDocument txDoc(512);
                txDoc["from"] = pendingTx["from"].as<String>();
                txDoc["to"] = pendingTx["to"].as<String>();
                txDoc["amount"] = pendingTx["amount"].as<int>();
                txDoc["timestamp"] = pendingTx["timestamp"].as<unsigned long>();
                txDoc["signature"] = pendingTx["signature"].as<String>();
                transactions.add(txDoc.as<JsonVariant>());
            }
            
            Serial.printf("\n📦 Block #%d - %d pending - Target: %s (difficulty: %d)\n", 
                         currentHeight, pending.size(), target.c_str(), targetDiff);
            
            lastInfoTime = millis();
        } else {
            delete info;
        }
    }
    
    if (currentHeight == 0) {
        delay(1000);
        return;
    }
    
    static unsigned long nonce = 0;
    unsigned long startLocal = millis();
    unsigned long localHashes = 0;
    bool found = false;
    
    for (int i = 0; i < 10000 && !found; i++) {
        if (nonce % 1000 == 0) {
            time_t now = time(nullptr);
            timestamp = (unsigned long)now * 1000;
            coinbaseDoc["timestamp"] = timestamp;
            transactions[0] = coinbaseDoc.as<JsonVariant>();
        }
        
        String hash = calculateBlockHash(currentHeight, prevHash, timestamp, transactions, nonce);
        localHashes++;
        
        noInterrupts();
        totalHashes++;
        interrupts();
        
        if (hash.startsWith(target)) {
            found = true;
            unsigned long elapsed = (millis() - startLocal) / 1000;
            float speed = localHashes / (elapsed > 0 ? elapsed : 1);
            
            Serial.printf("\n🎯 Found nonce %lu\n", nonce);
            Serial.printf("   Speed: %.2f H/s\n", speed);
            Serial.printf("   Hash: %s\n", hash.c_str());
            
            if (submitBlock(currentHeight, nonce, hash, prevHash, baseReward, transactions)) {
                blocksMined++;
                totalReward += baseReward;
                Serial.printf("✅✅✅ Block #%d ACCEPTED! +%d WBC ✅✅✅\n", currentHeight, baseReward);
            } else {
                Serial.printf("❌❌❌ Block #%d REJECTED! ❌❌❌\n", currentHeight);
            }
            
            currentHeight = 0;
            nonce = 0;
            break;
        }
        
        nonce++;
        
        if (nonce >= 0xFFFFFFFF) nonce = 0;
    }
    
    if (config.cpuPercent < 100) {
        delay(10 - config.cpuPercent / 10);
    }
}

// ============== LOOP CHÍNH ==============
void loop() {
    static unsigned long lastStats = 0;
    static unsigned long lastHashes = 0;
    static unsigned long lastTime = millis();
    
    if (running) {
        mine();
    }
    
    if (millis() - lastStats > 10000) {
        unsigned long now = millis();
        unsigned long elapsed = (now - startTime) / 1000;
        
        float speed = 0;
        unsigned long timeDiff = (now - lastTime);
        if (timeDiff > 0) {
            speed = (totalHashes - lastHashes) / (timeDiff / 1000.0);
        }
        
        Serial.printf("\n📊 STATS [%lum %lus]\n", elapsed / 60, elapsed % 60);
        Serial.printf("   📈 Hashes: %lu | Speed: %.2f H/s\n", totalHashes, speed);
        Serial.printf("   ⛏️  Blocks: %d | Reward: %d WBC\n", blocksMined, totalReward);
        
        if (speed > 0) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        }
        
        lastHashes = totalHashes;
        lastTime = now;
        lastStats = millis();
    }
}
