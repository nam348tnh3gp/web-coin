#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "DSHA1.h"
#include <time.h>
#include <mbedtls/md.h>  // Thêm cho HMAC

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

const char* WIFI_SSID = "THANH PHONG";
const char* WIFI_PASS = "matkhauwifi";
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";

int cpuThreads = 2;
int cpuPercent = 100;
int difficultyOverride = 0;

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

// ============== HÀM TIỆN ÍCH ==============
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

// ============== HÀM HMAC ==============
String calculateHMAC(const String& data, const String& key, const String& salt) {
    String hmacInput = key + salt;
    String dataStr = data;
    
    uint8_t hmacResult[32]; // SHA-256 = 32 bytes
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)hmacInput.c_str(), hmacInput.length());
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)dataStr.c_str(), dataStr.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);
    
    return bytesToHex(hmacResult, 32);
}

// ============== HÀM HASH BLOCK ==============
String calculateBlockHash(int height, String prevHash, unsigned long timestamp, 
                         JsonArray transactions, unsigned long nonce, 
                         const String& miningSalt, const String& blockSalt) {
    static char txBuffer[2048];
    static char dataBuffer[2048];
    
    txBuffer[0] = '\0';
    for (JsonVariant v : transactions) {
        String tmp;
        serializeJson(v, tmp);
        strcat(txBuffer, tmp.c_str());
    }
    
    // Format: height + previousHash + timestamp + txString + nonce + miningSalt + blockSalt
    snprintf(dataBuffer, sizeof(dataBuffer), "%d%s%lu%s%lu%s%s",
             height, prevHash.c_str(), timestamp, txBuffer, nonce,
             miningSalt.c_str(), blockSalt.c_str());
    
    DSHA1 sha1;
    sha1.write((const unsigned char*)dataBuffer, strlen(dataBuffer));
    
    unsigned char hashResult[20];
    sha1.finalize(hashResult);
    
    return bytesToHex(hashResult, 20);
}

// ============== HÀM ĐĂNG NHẬP ==============
bool login() {
    WiFiClientSecure *client = new WiFiClientSecure;
    if(!client) {
        Serial.println("Khong the tao WiFiClientSecure");
        return false;
    }
    
    client->setInsecure();
    
    HTTPClient http;
    http.begin(*client, String(SERVER_URL) + "/login");
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"displayAddress\":\"" + walletAddress + "\",\"password\":\"" + walletPassword + "\"}";
    
    Serial.println("\n----- DEBUG LOGIN -----");
    Serial.println("Gui len server: " + payload);
    
    int code = http.POST(payload);
    Serial.printf("Ma phan hoi HTTP: %d\n", code);
    
    if (code > 0) {
        if (code == 200) {
            String response = http.getString();
            Serial.println("Server tra ve: " + response);
            
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, response);
            
            if (error) {
                Serial.print("Loi parse JSON: ");
                Serial.println(error.c_str());
                http.end();
                delete client;
                return false;
            }

            if (doc.containsKey("error") && !doc["error"].isNull()) {
                Serial.println("Loi tu server: " + doc["error"].as<String>());
                http.end();
                delete client;
                return false;
            }

            if (doc.containsKey("publicKey")) {
                walletPublicKey = doc["publicKey"].as<String>();
                Serial.println("PublicKey nhan duoc: " + walletPublicKey);
            } else {
                Serial.println("Khong tim thay publicKey trong response!");
                http.end();
                delete client;
                return false;
            }

            String cookie = http.header("Set-Cookie");
            if (cookie.length() > 0) {
                int semi = cookie.indexOf(';');
                authCookie = semi > 0 ? cookie.substring(0, semi) : cookie;
                Serial.println("Cookie: " + authCookie);
            } else {
                Serial.println("Khong nhan duoc cookie!");
            }

            http.end();
            delete client;
            Serial.println("----- DANG NHAP THANH CONG -----\n");
            return true;
        } else {
            String response = http.getString();
            Serial.println("Loi response: " + response);
            http.end();
            delete client;
            Serial.println("----- DANG NHAP THAT BAI -----\n");
            return false;
        }
    } else {
        Serial.println("Khong the ket noi den server!");
        http.end();
        delete client;
        return false;
    }
}

// ============== HÀM LẤY THÔNG TIN MẠNG ==============
bool getNetwork(DynamicJsonDocument &doc) {
    WiFiClientSecure *client = new WiFiClientSecure;
    if(!client) return false;
    
    client->setInsecure();
    HTTPClient http;
    http.begin(*client, String(SERVER_URL) + "/info");
    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    int code = http.GET();
    bool success = false;
    
    if (code == 200) {
        String res = http.getString();
        http.end();
        if (!deserializeJson(doc, res) && doc.containsKey("latestBlock")) {
            success = true;
        }
    }
    http.end();
    delete client;
    return success;
}

// ============== HÀM LẤY PENDING TRANSACTIONS ==============
bool getPending(DynamicJsonDocument &doc) {
    WiFiClientSecure *client = new WiFiClientSecure;
    if(!client) return false;
    
    client->setInsecure();
    HTTPClient http;
    http.begin(*client, String(SERVER_URL) + "/pending");
    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    int code = http.GET();
    bool success = false;
    
    if (code == 200) {
        String res = http.getString();
        http.end();
        if (!deserializeJson(doc, res)) {
            success = true;
        }
    }
    http.end();
    delete client;
    return success;
}

// ============== HÀM GỬI BLOCK ==============
bool submitBlock(int height, unsigned long nonce, String hash, String prevHash, 
                int reward, JsonArray transactions, const String& blockHMAC, 
                const String& workerSalt, const String& miningSalt) {
    WiFiClientSecure *client = new WiFiClientSecure;
    if(!client) return false;
    
    client->setInsecure();
    HTTPClient http;
    http.begin(*client, String(SERVER_URL) + "/blocks/submit");
    http.addHeader("Content-Type", "application/json");
    if (authCookie.length()) http.addHeader("Cookie", authCookie);

    DynamicJsonDocument doc(8192);
    doc["height"] = height;
    doc["previousHash"] = prevHash;
    doc["timestamp"] = (unsigned long)time(nullptr) * 1000;
    doc["nonce"] = nonce;
    doc["hash"] = hash;
    doc["minerAddress"] = walletAddress;
    doc["blockHMAC"] = blockHMAC;
    doc["workerSalt"] = workerSalt;
    doc["miningSalt"] = miningSalt;

    JsonArray txs = doc.createNestedArray("transactions");
    for (JsonVariant v : transactions) {
        txs.add(v);
    }

    String payload;
    serializeJson(doc, payload);

    Serial.println("Gui block len server...");
    int code = http.POST(payload);
    
    if (code == 200) {
        String response = http.getString();
        Serial.println("Server phan hoi: " + response);
    } else {
        Serial.printf("HTTP code: %d\n", code);
        String response = http.getString();
        Serial.println("Loi: " + response);
    }
    
    http.end();
    delete client;

    return code == 200;
}

// ============== HÀM KIỂM TRA WIFI ==============
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

// ============== THREAD ĐÀO ==============
void miningTask(void* p) {
    int id = (int)(intptr_t)p;
    
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
        
        char targetStr[32];
        for (int i = 0; i < diff; i++) targetStr[i] = '0';
        targetStr[diff] = '\0';
        
        int height = latest["height"].as<int>() + 1;
        String prevHash = latest["hash"].as<String>();
        
        // Lấy pending transactions
        DynamicJsonDocument pendingDoc(8192);
        JsonArray pending;
        if (getPending(pendingDoc)) {
            pending = pendingDoc.as<JsonArray>();
        }
        
        // Tạo transactions array
        DynamicJsonDocument txDoc(8192);
        JsonArray txs = txDoc.to<JsonArray>();
        
        unsigned long timestamp = time(nullptr) * 1000;
        
        // Tạo coinbase transaction với salt
        String txSalt = generateSalt(16);
        JsonObject coinbase = txs.createNestedObject();
        coinbase["from"] = nullptr;
        coinbase["to"] = walletPublicKey;
        coinbase["amount"] = reward;
        coinbase["timestamp"] = timestamp;
        coinbase["signature"] = nullptr;
        coinbase["salt"] = txSalt;
        
        // Tạo HMAC cho coinbase
        String txData = String(walletPublicKey) + String(reward) + String(timestamp) + txSalt;
        String txHash = calculateBlockHash(height, prevHash, timestamp, txs, 0, "", ""); // Tạm
        String txHMAC = calculateHMAC(txData, walletPublicKey, txSalt);
        coinbase["hmac"] = txHMAC;
        
        // Thêm pending transactions (giữ nguyên salt và hmac từ server)
        for (JsonVariant v : pending) {
            txs.add(v);
        }
        
        // Tạo salt cho block
        String miningSalt = generateSalt(8);
        String blockSalt = generateSalt(8);
        
        if (id == 0) {
            Serial.printf("\n[BLOCK %d] TX: %d | Do kho: %d | Thuong: %d\n", 
                         height, txs.size(), diff, reward);
            Serial.printf("Mining Salt: %s | Block Salt: %s\n", 
                         miningSalt.c_str(), blockSalt.c_str());
        }
        
        unsigned long nonce = id * 10000000UL;
        unsigned long localHashCount = 0;
        unsigned long startTime = millis();
        
        while (running) {
            // Tính hash với đầy đủ salts
            String hash = calculateBlockHash(height, prevHash, timestamp, txs, nonce, miningSalt, blockSalt);
            localHashCount++;
            
            // Kiểm tra target
            if (strncmp(hash.c_str(), targetStr, diff) == 0) {
                unsigned long elapsed = (millis() - startTime) / 1000;
                float hashrate = localHashCount / (elapsed > 0 ? elapsed : 1);
                
                Serial.printf("\n[FOUND] Task %d | Nonce: %lu | Hash: %s\n", 
                             id, nonce, hash.c_str());
                Serial.printf("Hashrate: %.2f H/s\n", hashrate);
                
                // Tạo dữ liệu cho HMAC block
                String blockData = String(height) + hash + prevHash + String(nonce);
                String workerSalt = generateSalt(16);
                String blockHMAC = calculateHMAC(blockData, walletPublicKey, workerSalt);
                
                Serial.printf("Block HMAC: %s\n", blockHMAC.c_str());
                
                // Gửi block lên server
                if (submitBlock(height, nonce, hash, prevHash, reward, txs, 
                               blockHMAC, workerSalt, miningSalt)) {
                    portENTER_CRITICAL(&mux);
                    blocksMined++;
                    totalReward += reward;
                    portEXIT_CRITICAL(&mux);
                    
                    Serial.printf("[OK] Block %d duoc chap nhan! +%d WebCoin\n", height, reward);
                } else {
                    Serial.printf("[FAIL] Block %d bi tu choi\n", height);
                }
                break;
            }
            
            nonce++;
            
            // Cập nhật thống kê
            if (localHashCount >= 10000) {
                portENTER_CRITICAL(&mux);
                totalHashes += localHashCount;
                portEXIT_CRITICAL(&mux);
                localHashCount = 0;
            }
            
            // Tránh watchdog timeout
            if (nonce % 1000 == 0) {
                yield();
            }
        }
    }
    
    vTaskDelete(NULL);
}

// ============== HÀM RESET THÔNG TIN ==============
void resetWalletInfo() {
    Serial.println("\n----- RESET THONG TIN VI -----");
    prefs.clear();
    walletAddress = "";
    walletPassword = "";
    walletPublicKey = "";
    authCookie = "";
    Serial.println("Da xoa thong tin cu. Khoi dong lai de nhap moi!");
    delay(2000);
    ESP.restart();
}

// ============== SETUP ==============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=================================");
    Serial.println("   WebCoin Miner cho ESP32 v2.0");
    Serial.println("   (SHA1 + HMAC + Salt)");
    Serial.println("=================================");
    
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    prefs.begin("webcoin", false);
    
    walletAddress = prefs.getString("wallet", "");
    walletPassword = prefs.getString("pass", "");
    walletPublicKey = prefs.getString("pubkey", "");
    
    Serial.println("Thong tin hien tai:");
    Serial.print("Dia chi vi: ");
    if (walletAddress.length() > 0) {
        Serial.println(walletAddress);
    } else {
        Serial.println("Chua co");
    }
    
    Serial.print("Mat khau: ");
    if (walletPassword.length() > 0) {
        Serial.println("Da luu");
    } else {
        Serial.println("Chua co");
    }
    
    if (walletAddress.length() == 0 || walletPassword.length() == 0) {
        Serial.println("\n----- NHAP THONG TIN VI -----");
        
        Serial.print("Nhap dia chi vi (bat dau bang W_...): ");
        while (!Serial.available());
        walletAddress = Serial.readStringUntil('\n');
        walletAddress.trim();
        Serial.println("Da nhan: " + walletAddress);
        
        Serial.print("Nhap mat khau: ");
        while (!Serial.available());
        walletPassword = Serial.readStringUntil('\n');
        walletPassword.trim();
        Serial.println("Da nhan: " + walletPassword);
        
        prefs.putString("wallet", walletAddress);
        prefs.putString("pass", walletPassword);
        Serial.println("[OK] Da luu thong tin vi!");
    } else {
        Serial.println("[OK] Da doc thong tin vi tu bo nho");
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
    
    if (walletPublicKey.length() == 0) {
        prefs.putString("pubkey", walletPublicKey);
    }
    
    Serial.println("[LOGIN] Thanh cong!");
    startTime = millis();
    
    Serial.printf("\n[MINER] Bat dau dao voi %d luong...\n", cpuThreads);
    for (int i = 0; i < cpuThreads; i++) {
        xTaskCreatePinnedToCore(
            miningTask,
            "miner",
            8192,  // Tăng stack size
            (void*)i,
            1,
            NULL,
            i % 2
        );
    }
    
    Serial.println("\n[Miner] San sang! Dang dao...\n");
}

// ============== LOOP ==============
void loop() {
    static unsigned long lastStats = 0;
    static unsigned long lastLedBlink = 0;
    static bool ledState = false;
    static unsigned long lastLoginCheck = 0;
    
    delay(5000);
    
    if (millis() - lastLoginCheck > 60000) {
        if (authCookie.length() == 0) {
            Serial.println("[WARN] Mat cookie, dang dang nhap lai...");
            if (!login()) {
                Serial.println("[ERROR] Khong the dang nhap lai!");
            }
        }
        lastLoginCheck = millis();
    }
    
    unsigned long now = millis();
    float elapsedSeconds = (now - lastStats) / 1000.0;
    
    if (lastStats > 0 && elapsedSeconds > 0) {
        portENTER_CRITICAL(&mux);
        unsigned long hashes = totalHashes;
        portEXIT_CRITICAL(&mux);
        
        float speed = (hashes - lastStats) / elapsedSeconds;
        
        Serial.printf("\n[THONG KE] %.2f H/s | Blocks: %d | Reward: %d\n",
                      speed, blocksMined, totalReward);
        
        lastStats = hashes;
        
        int blinkInterval = (speed > 100) ? 100 : (speed > 10) ? 500 : 1000;
        if (now - lastLedBlink > blinkInterval) {
            ledState = !ledState;
            digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
            lastLedBlink = now;
        }
    } else {
        lastStats = totalHashes;
    }
}
