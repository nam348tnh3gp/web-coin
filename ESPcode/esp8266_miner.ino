/*
 * WebCoin ESP8266 Miner
 * Chạy trên NodeMCU, Wemos D1, ESP-01, v.v.
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <BearSSLHelpers.h>
#include <EEPROM.h>

// ============== CẤU HÌNH ==============
const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASS = "your_wifi_password";
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";
const int SOC_TIMEOUT = 10;

// Cấu hình EEPROM
#define EEPROM_SIZE 512
#define CONFIG_VERSION 0x01

// ============== CẤU TRÚC LƯU CONFIG ==============
struct Config {
    uint8_t version;
    char wallet[100];
    char password[64];
    uint8_t threads;
    uint8_t cpuPercent;
    uint8_t difficulty; // 0 = auto
} config;

// ============== BIẾN TOÀN CỤC ==============
WiFiClientSecure client;
String authCookie = "";
unsigned long totalHashes = 0;
int blocksMined = 0;
int totalReward = 0;
unsigned long startTime = 0;
bool running = true;

// LED indicator
#define LED_PIN LED_BUILTIN

// ============== SHA1 IMPLEMENTATION ==============
// (ESP8266 không có thư viện SHA1 chuẩn, tự implement)
#define SHA1_K0 0x5a827999
#define SHA1_K20 0x6ed9eba1
#define SHA1_K40 0x8f1bbcdc
#define SHA1_K60 0xca62c1d6

uint8_t sha1Result[20];

void sha1(const uint8_t* data, uint32_t len) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;
    
    uint32_t w[80];
    uint32_t a, b, c, d, e, temp;
    uint8_t padding[64];
    uint64_t bitLen = len * 8;
    uint32_t i, j;
    
    // Copy data vào buffer
    uint8_t buffer[len + 9];
    memcpy(buffer, data, len);
    buffer[len] = 0x80;
    
    // Thêm padding
    uint32_t padLen = (len < 56) ? (56 - len - 1) : (64 - (len - 56) - 1);
    for (i = 0; i < padLen; i++) {
        buffer[len + 1 + i] = 0;
    }
    
    // Thêm độ dài
    for (i = 0; i < 8; i++) {
        buffer[len + 1 + padLen + i] = (bitLen >> (56 - i * 8)) & 0xFF;
    }
    
    uint32_t totalLen = len + 1 + padLen + 8;
    
    // Xử lý từng block 64 byte
    for (uint32_t block = 0; block < totalLen; block += 64) {
        // Copy vào w[0..15]
        for (i = 0; i < 16; i++) {
            w[i] = ((uint32_t)buffer[block + i*4]) << 24;
            w[i] |= ((uint32_t)buffer[block + i*4 + 1]) << 16;
            w[i] |= ((uint32_t)buffer[block + i*4 + 2]) << 8;
            w[i] |= (uint32_t)buffer[block + i*4 + 3];
        }
        
        // Mở rộng lên w[16..79]
        for (i = 16; i < 80; i++) {
            w[i] = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (w[i] << 1) | (w[i] >> 31);
        }
        
        a = h0; b = h1; c = h2; d = h3; e = h4;
        
        for (i = 0; i < 20; i++) {
            temp = ((a << 5) | (a >> 27)) + ((b & c) | ((~b) & d)) + e + w[i] + SHA1_K0;
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
        }
        
        for (i = 20; i < 40; i++) {
            temp = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + w[i] + SHA1_K20;
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
        }
        
        for (i = 40; i < 60; i++) {
            temp = ((a << 5) | (a >> 27)) + ((b & c) | (b & d) | (c & d)) + e + w[i] + SHA1_K40;
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
        }
        
        for (i = 60; i < 80; i++) {
            temp = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + w[i] + SHA1_K60;
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
        }
        
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    
    // Lưu kết quả
    for (i = 0; i < 4; i++) {
        sha1Result[i] = (h0 >> (24 - i*8)) & 0xFF;
        sha1Result[i+4] = (h1 >> (24 - i*8)) & 0xFF;
        sha1Result[i+8] = (h2 >> (24 - i*8)) & 0xFF;
        sha1Result[i+12] = (h3 >> (24 - i*8)) & 0xFF;
        sha1Result[i+16] = (h4 >> (24 - i*8)) & 0xFF;
    }
}

String sha1Hex(const uint8_t* data, uint32_t len) {
    sha1(data, len);
    String result = "";
    for (int i = 0; i < 20; i++) {
        if (sha1Result[i] < 0x10) result += "0";
        result += String(sha1Result[i], HEX);
    }
    return result;
}

// ============== HÀM JSON STRINGIFY GIỐNG JS ==============
String jsonStringify(JsonVariant obj) {
    if (obj.is<const char*>()) {
        return "\"" + String(obj.as<const char*>()) + "\"";
    }
    if (obj.is<String>()) {
        return "\"" + obj.as<String>() + "\"";
    }
    if (obj.is<int>() || obj.is<long>() || obj.is<double>()) {
        return String(obj.as<double>());
    }
    if (obj.is<bool>()) {
        return obj.as<bool>() ? "true" : "false";
    }
    if (obj.is<JsonArray>()) {
        JsonArray arr = obj.as<JsonArray>();
        String result = "[";
        for (size_t i = 0; i < arr.size(); i++) {
            if (i > 0) result += ",";
            result += jsonStringify(arr.get(i));
        }
        result += "]";
        return result;
    }
    if (obj.is<JsonObject>()) {
        JsonObject obj2 = obj.as<JsonObject>();
        String result = "{";
        bool first = true;
        for (JsonPair kv : obj2) {
            if (!first) result += ",";
            first = false;
            result += "\"" + String(kv.key().c_str()) + "\":";
            result += jsonStringify(kv.value());
        }
        result += "}";
        return result;
    }
    return "null";
}

// ============== HÀM HASH GIỐNG JS ==============
String calculateBlockHash(int height, String prevHash, unsigned long timestamp, JsonArray transactions, unsigned long nonce) {
    // Tạo txString giống JS: join(JSON.stringify)
    String txString = "";
    for (size_t i = 0; i < transactions.size(); i++) {
        txString += jsonStringify(transactions.get(i));
    }
    
    // Tạo data string
    String data = String(height) + prevHash + String(timestamp) + txString + String(nonce);
    
    // Tính SHA1
    return sha1Hex((uint8_t*)data.c_str(), data.length());
}

// ============== API FUNCTIONS ==============
bool login(String wallet, String password) {
    client.setInsecure(); // Bỏ qua SSL certificate
    
    HTTPClient http;
    http.begin(client, String(SERVER_URL) + "/login");
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{\"displayAddress\":\"" + wallet + "\",\"password\":\"" + password + "\"}";
    int httpCode = http.POST(payload);
    
    if (httpCode == 200) {
        String response = http.getString();
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, response);
        
        if (!doc.containsKey("error")) {
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
        DynamicJsonDocument* doc = new DynamicJsonDocument(2048);
        deserializeJson(*doc, response);
        http.end();
        return doc;
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
        DynamicJsonDocument* doc = new DynamicJsonDocument(8192);
        deserializeJson(*doc, response);
        http.end();
        return doc;
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
    
    // Tạo payload
    DynamicJsonDocument doc(8192);
    doc["height"] = height;
    doc["previousHash"] = prevHash;
    doc["timestamp"] = millis();
    doc["nonce"] = nonce;
    doc["hash"] = hash;
    doc["minerAddress"] = String(config.wallet);
    
    JsonArray txs = doc.createNestedArray("transactions");
    for (size_t i = 0; i < transactions.size(); i++) {
        txs.add(transactions.get(i));
    }
    
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    http.end();
    
    return httpCode == 200;
}

// ============== LƯU/ĐỌC CONFIG ==============
void saveConfig() {
    EEPROM.put(0, config);
    EEPROM.commit();
}

void loadConfig() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, config);
    
    // Kiểm tra version
    if (config.version != CONFIG_VERSION) {
        // Config mặc định
        config.version = CONFIG_VERSION;
        strcpy(config.wallet, "");
        strcpy(config.password, "");
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
    
    Serial.println("\n================================");
    Serial.println(" WEBCCOIN ESP8266 MINER");
    Serial.println("================================");
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    // Đọc config
    loadConfig();
    
    if (strlen(config.wallet) == 0) {
        // Nhập thông tin lần đầu
        Serial.println("\n📝 NHẬP THÔNG TIN:");
        
        Serial.print("Địa chỉ ví (W_...): ");
        while (!Serial.available());
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (!input.startsWith("W_")) {
            input = "W_" + input;
        }
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
        
        // Lưu config
        saveConfig();
        Serial.println("✅ Config saved");
    }
    
    Serial.printf("\n📊 Wallet: %s...\n", String(config.wallet).substring(0, 20).c_str());
    Serial.printf("   Difficulty: %s\n", config.difficulty == 0 ? "Auto" : String(config.difficulty).c_str());
    
    // Kết nối WiFi
    Serial.printf("\n📡 Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi connected");
        Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
        digitalWrite(LED_PIN, LOW);
    } else {
        Serial.println("\n❌ WiFi failed!");
        return;
    }
    
    // Đăng nhập
    Serial.print("\n🔐 Logging in... ");
    if (login(config.wallet, config.password)) {
        Serial.println("✅ Success!");
    } else {
        Serial.println("❌ Failed!");
        return;
    }
    
    // Lấy thông tin mạng
    DynamicJsonDocument* info = getNetworkInfo();
    if (info) {
        Serial.printf("📡 Network: diff=%d, reward=%d WBC\n", 
                     (*info)["difficulty"].as<int>(), 
                     (*info)["reward"].as<int>());
        delete info;
    }
    
    // Bắt đầu đào
    Serial.println("\n🚀 Starting miner...");
    Serial.println("================================");
    
    startTime = millis();
}

// ============== HÀM ĐÀO ==============
void mine() {
    static unsigned long lastInfoTime = 0;
    static int currentHeight = 0;
    static String prevHash = "";
    static int targetDiff = 0;
    static String target = "";
    static JsonArray pending;
    static DynamicJsonDocument* pendingDoc = nullptr;
    static unsigned long timestamp = 0;
    static DynamicJsonDocument coinbaseDoc(256);
    static DynamicJsonDocument transactionsDoc(8192);
    static JsonArray transactions;
    
    // Lấy thông tin mạng mỗi 30 giây
    if (millis() - lastInfoTime > 30000 || currentHeight == 0) {
        DynamicJsonDocument* info = getNetworkInfo();
        if (info && (*info).containsKey("latestBlock")) {
            JsonObject latest = (*info)["latestBlock"];
            currentHeight = latest["height"].as<int>() + 1;
            prevHash = latest["hash"].as<String>();
            
            int networkDiff = (*info)["difficulty"] | 3;
            int baseReward = (*info)["reward"] | 48;
            
            targetDiff = config.difficulty > 0 ? config.difficulty : networkDiff;
            target = "";
            for (int i = 0; i < targetDiff; i++) target += "0";
            
            // Lấy pending
            if (pendingDoc) delete pendingDoc;
            pendingDoc = getPending();
            pending = pendingDoc ? pendingDoc->as<JsonArray>() : JsonArray();
            
            // Tạo timestamp
            timestamp = millis();
            
            // Tạo coinbase transaction
            String publicKey = String(config.wallet).substring(2);
            coinbaseDoc.clear();
            coinbaseDoc["from"] = nullptr;
            coinbaseDoc["to"] = publicKey;
            coinbaseDoc["amount"] = baseReward;
            coinbaseDoc["timestamp"] = timestamp;
            coinbaseDoc["signature"] = nullptr;
            
            // Tạo transactions array
            transactionsDoc.clear();
            transactions = transactionsDoc.to<JsonArray>();
            transactions.add(coinbaseDoc.as<JsonVariant>());
            for (size_t i = 0; i < pending.size(); i++) {
                transactions.add(pending[i]);
            }
            
            Serial.printf("\n📦 Block #%d - %d pending - Target: %s\n", 
                         currentHeight, pending.size(), target.c_str());
            
            lastInfoTime = millis();
        }
        delete info;
    }
    
    if (currentHeight == 0) {
        delay(1000);
        return;
    }
    
    // Đào 10000 nonce mỗi lần
    unsigned long nonce = millis() % 10000000; // Nonce random
    unsigned long startLocal = millis();
    unsigned long localHashes = 0;
    
    for (int i = 0; i < 10000; i++) {
        String hash = calculateBlockHash(currentHeight, prevHash, timestamp, transactions, nonce + i);
        localHashes++;
        totalHashes++;
        
        if (hash.startsWith(target)) {
            unsigned long elapsed = (millis() - startLocal) / 1000;
            float speed = localHashes / (elapsed > 0 ? elapsed : 1);
            
            Serial.printf("\n🎯 Found nonce %lu\n", nonce + i);
            Serial.printf("   Speed: %.1f kH/s\n", speed/1000);
            Serial.printf("   Hash: %s\n", hash.substring(0, 30).c_str());
            
            if (submitBlock(currentHeight, nonce + i, hash, prevHash, 
                           (*coinbaseDoc.as<JsonObject>())["amount"].as<int>(), 
                           transactions)) {
                blocksMined++;
                totalReward += (*coinbaseDoc.as<JsonObject>())["amount"].as<int>();
                Serial.printf("✅✅✅ Block #%d ACCEPTED! +%d WBC ✅✅✅\n", 
                             currentHeight, (*coinbaseDoc.as<JsonObject>())["amount"].as<int>());
            } else {
                Serial.printf("❌❌❌ Block #%d REJECTED! ❌❌❌\n", currentHeight);
            }
            
            // Reset để lấy block mới
            currentHeight = 0;
            break;
        }
    }
    
    // Điều chỉnh CPU (ESP8266 chỉ có 1 core)
    if (config.cpuPercent < 100) {
        delay(10 - config.cpuPercent / 10);
    }
}

// ============== LOOP CHÍNH ==============
void loop() {
    static unsigned long lastStats = 0;
    static unsigned long lastHashes = 0;
    static unsigned long lastTime = millis();
    
    mine();
    
    // Thống kê mỗi 10 giây
    if (millis() - lastStats > 10000) {
        unsigned long now = millis();
        unsigned long elapsed = (now - startTime) / 1000;
        
        float speed = (totalHashes - lastHashes) / ((now - lastTime) / 1000.0);
        
        Serial.printf("\n📊 STATS [%lum %lus]\n", elapsed / 60, elapsed % 60);
        Serial.printf("   📈 Hashes: %lu | Speed: %.2f kH/s\n", totalHashes, speed/1000);
        Serial.printf("   ⛏️  Blocks: %d | Reward: %d WBC\n", blocksMined, totalReward);
        
        // Nhấp nháy LED
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        
        lastHashes = totalHashes;
        lastTime = now;
        lastStats = millis();
    }
}
