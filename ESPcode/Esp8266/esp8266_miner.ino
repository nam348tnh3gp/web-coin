#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "DSHA1.h"
#include <time.h>

// cấu hình wifi
const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASS = "your_wifi_password";
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";

#define EEPROM_SIZE 512
#define CONFIG_VERSION 0x02
#define LED_PIN LED_BUILTIN  // ESP8266 LED thường ở GPIO2

// cấu trúc dữ liệu
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

// hàm băm
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
    client.setInsecure();  // Bỏ qua kiểm tra SSL (tạm thời)
    
    HTTPClient http;
    if (!http.begin(client, String(SERVER_URL) + "/login")) {
        Serial.println("[HTTP] Khong the ket noi!");
        return false;
    }
    
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"displayAddress\":\"" + String(config.wallet) +
                     "\",\"password\":\"" + String(config.password) + "\"}";

    int code = http.POST(payload);

    if (code == 200) {
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, http.getString());

        if (doc["error"].isNull()) {

            if (!doc["publicKey"].isNull()) {
                String pk = doc["publicKey"].as<String>();
                strncpy(config.publicKey, pk.c_str(), sizeof(config.publicKey) - 1);
                config.publicKey[sizeof(config.publicKey) - 1] = '\0';
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

// thông tin mạng
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

// pending
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

// gửi block
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

// lưu cấu hình
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
    digitalWrite(LED_PIN, HIGH);  // Tắt LED

    loadConfig();

    if (strlen(config.wallet) == 0) {
        Serial.println("Nhap wallet (dang W_...):");
        while (!Serial.available());
        String w = Serial.readStringUntil('\n');
        w.trim();
        strncpy(config.wallet, w.c_str(), sizeof(config.wallet) - 1);

        Serial.println("Nhap password:");
        while (!Serial.available());
        String p = Serial.readStringUntil('\n');
        p.trim();
        strncpy(config.password, p.c_str(), sizeof(config.password) - 1);

        saveConfig();
    }

    Serial.print("Dang ket noi WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi OK");
    } else {
        Serial.println("\nWiFi FAIL!");
        return;
    }

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    Serial.print("Dong bo thoi gian");
    time_t now = time(nullptr);
    while (now < 100000 && attempts < 20) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        attempts++;
    }
    Serial.println();

    if (!login()) {
        Serial.println("Login fail!");
        return;
    }

    Serial.println("Login OK");
    startTime = millis();
}

// loop
void loop() {
    static unsigned long lastStats = 0;
    
    // Kiểm tra WiFi
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Mat WiFi, dang ket noi lai...");
        WiFi.reconnect();
        delay(5000);
        return;
    }

    // Lấy thông tin mạng
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

    // Tạo chuỗi giao dịch
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
    unsigned long lastBlink = millis();
    bool ledState = false;

    while (true) {
        // Nhấp nháy LED báo hiệu đang chạy
        if (millis() - lastBlink > 500) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? LOW : HIGH);
            lastBlink = millis();
        }

        // Tính hash
        String hash = calculateHash(height, prevHash, ts, txStr, nonce);
        totalHashes++;

        // Kiểm tra kết quả
        if (hash.startsWith(target)) {
            digitalWrite(LED_PIN, HIGH);  // Tắt LED
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

        // Thống kê tốc độ mỗi 10 giây
        if (nonce % 10000 == 0) {
            float speed = totalHashes / ((millis() - startTime) / 1000.0);
            Serial.printf("\rSpeed: %.2f H/s | Total: %d | Blocks: %d | Reward: %d", 
                         speed, totalHashes, blocksMined, totalReward);
            
            // Cho phép ESP8266 xử lý WiFi
            yield();
        }

        // Tránh watchdog timeout
        if (nonce % 5000 == 0) {
            delay(0);
        }
    }

    delay(100);
}
