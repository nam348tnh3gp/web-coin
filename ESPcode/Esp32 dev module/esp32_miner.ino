#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "DSHA1.h"
#include <time.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// cấu hình mạng
const char* WIFI_SSID = "your_wifi_ssid";        // Sửa tên WiFi
const char* WIFI_PASS = "your_wifi_password";    // Sửa mật khẩu
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";

// cấu hình đào
int cpuThreads = 2;                    // Số luồng đào (tối đa 2)
int cpuPercent = 100;                  // 100% = không delay
int difficultyOverride = 0;             // 0 = dùng độ khó từ server

// biến toàn cục 
Preferences prefs;
String walletAddress = "";
String walletPassword = "";
String walletPublicKey = "";
String authCookie = "";

volatile unsigned long totalHashes = 0;
volatile int blocksMined = 0;
volatile int totalReward = 0;

bool running = true;
unsigned long startTime;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// hàm băm
String calculateBlockHash(int height, String prevHash, unsigned long timestamp, JsonArray transactions, unsigned long nonce) {
    static char txBuffer[1024];
    static char dataBuffer[1024];
    
    txBuffer[0] = '\0';
    for (JsonVariant v : transactions) {
        String tmp;
        serializeJson(v, tmp);
        strcat(txBuffer, tmp.c_str());
    }
    
    snprintf(dataBuffer, sizeof(dataBuffer), "%d%s%lu%s%lu",
             height, prevHash.c_str(), timestamp, txBuffer, nonce);
    
    DSHA1 sha1;
    sha1.write((const unsigned char*)dataBuffer, strlen(dataBuffer));
    
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

// hàm api
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

// check wifi
bool checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Mat ket noi, dang ket noi lai...");
        WiFi.reconnect();
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[WiFi] Da ket noi lai!");
            return true;
        } else {
            Serial.println("\n[WiFi] Khong the ket noi lai!");
            return false;
        }
    }
    return true;
}

// task đào
void miningTask(void* p) {
    int id = (int)(intptr_t)p;
    char targetStr[32];
    
    while (running) {
        if (!checkWiFi()) {
            delay(5000);
            continue;
        }
        
        DynamicJsonDocument info(4096);
        if (!getNetwork(info)) {
            delay(2000);
            continue;
        }
        
        JsonObject latest = info["latestBlock"];
        
        int diff = difficultyOverride > 0 ? difficultyOverride : info["difficulty"].as<int>();
        int reward = info["reward"].as<int>();
        
        for (int i = 0; i < diff; i++) targetStr[i] = '0';
        targetStr[diff] = '\0';
        
        int height = latest["height"].as<int>() + 1;
        String prevHash = latest["hash"].as<String>();
        
        DynamicJsonDocument pendingDoc(4096);
        JsonArray pending;
        if (getPending(pendingDoc)) pending = pendingDoc.as<JsonArray>();
        
        DynamicJsonDocument txDoc(4096);
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
            Serial.printf("\n[BLOCK %d] TX: %d | Do kho: %d | Thuong: %d\n", 
                         height, txs.size(), diff, reward);
        }
        
        unsigned long nonce = id * 10000000UL;
        unsigned long localHashCount = 0;
        
        while (running) {
            String hash = calculateBlockHash(height, prevHash, timestamp, txs, nonce);
            
            if (strncmp(hash.c_str(), targetStr, diff) == 0) {
                Serial.printf("\n[FOUND] Task %d | Nonce: %lu | Hash: %s\n", id, nonce, hash.c_str());
                
                if (submitBlock(height, nonce, hash, prevHash, reward, txs)) {
                    portENTER_CRITICAL(&mux);
                    blocksMined++;
                    totalReward += reward;
                    portEXIT_CRITICAL(&mux);
                    
                    Serial.printf("[OK] Block %d duoc chap nhan! +%d WebCoin\n", height, reward);
                }
                break;
            }
            
            nonce++;
            localHashCount++;
            
            if (localHashCount >= 10000) {
                portENTER_CRITICAL(&mux);
                totalHashes += localHashCount;
                portEXIT_CRITICAL(&mux);
                localHashCount = 0;
            }
            
            // Cho ESP32 thường xử lý tác vụ nền
            if (nonce % 1000 == 0) {
                yield();
            }
        }
    }
    
    vTaskDelete(NULL);
}

// setup
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\nWebCoin Miner cho ESP32");
    Serial.println("================================");
    
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    prefs.begin("webcoin", false);
    walletAddress = prefs.getString("wallet", "");
    walletPassword = prefs.getString("pass", "");
    
    if (walletAddress == "") {
        Serial.println("[NHAP] Chua co thong tin vi!");
        Serial.print("Nhap dia chi vi (dang W_...): ");
        while (!Serial.available());
        walletAddress = Serial.readStringUntil('\n');
        walletAddress.trim();
        
        Serial.print("Nhap mat khau: ");
        while (!Serial.available());
        walletPassword = Serial.readStringUntil('\n');
        walletPassword.trim();
        
        prefs.putString("wallet", walletAddress);
        prefs.putString("pass", walletPassword);
        Serial.println("[OK] Da luu thong tin vi!");
    } else {
        Serial.println("[OK] Da doc thong tin vi tu bo nho");
    }
    
    Serial.printf("\n[WiFi] Dang ket noi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 40) {
        delay(500);
        Serial.print(".");
        wifiAttempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Da ket noi! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Khong the ket noi!");
        return;
    }
    
    Serial.print("[NTP] Dang dong bo thoi gian...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 100000 && attempts < 30) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        attempts++;
    }
    Serial.printf("\n[NTP] Thoi gian: %s", ctime(&now));
    
    Serial.print("[LOGIN] Dang dang nhap...");
    if (!login()) {
        Serial.println("\n[LOGIN] That bai!");
        return;
    }
    Serial.println(" Thanh cong!");
    
    startTime = millis();
    
    Serial.printf("\n[MINER] Bat dau dao voi %d luong...\n", cpuThreads);
    for (int i = 0; i < cpuThreads; i++) {
        xTaskCreatePinnedToCore(
            miningTask,
            "miner",
            4096,                    // Stack nhỏ hơn cho ESP32 thường
            (void*)i,
            1,
            NULL,
            i % 2
        );
    }
    
    Serial.println("\n[Miner] San sang!\n");
}

// loop
void loop() {
    static unsigned long lastStats = 0;
    static unsigned long lastLedBlink = 0;
    static bool ledState = false;
    
    delay(5000);
    
    unsigned long now = millis();
    float speed = (totalHashes - lastStats) / 5.0;
    
    Serial.printf("\n[THONG KE] %.2f H/s | Blocks: %d | Reward: %d\n",
                  speed, blocksMined, totalReward);
    
    lastStats = totalHashes;
    
    // LED nhấp nháy theo tốc độ
    int blinkInterval = (speed > 100) ? 100 : (speed > 10) ? 500 : 1000;
    if (now - lastLedBlink > blinkInterval) {
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
        lastLedBlink = now;
    }
}
