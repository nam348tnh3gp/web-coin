#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "DSHA1.h"
#include <time.h>

// cấu hình
#define ARDUINO_USB_MODE 1           // 1 để dùng Serial qua USB
#define ARDUINO_USB_CDC_ON_BOOT 1    // Bật CDC để dùng Serial monitor

// LED 
#ifndef LED_BUILTIN
#define LED_BUILTIN 48                // Đa số ESP32-S3 dùng GPIO48
#endif

// cấu hình mạng
const char* WIFI_SSID = "your_wifi_ssid";        // Sửa tên WiFi
const char* WIFI_PASS = "your_wifi_password";    // Sửa mật khẩu
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";

// cấu hình đào
int cpuThreads = 2;                    // Số luồng đào (tối đa 2 cho dual-core)
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
unsigned long lastNetworkCheck = 0;
const unsigned long NETWORK_CHECK_INTERVAL = 30000; // 30 giây

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ===== HÀM BĂM TỐI ƯU CHO ESP32-S3 =====
String calculateBlockHash(int height, String prevHash, unsigned long timestamp, JsonArray transactions, unsigned long nonce) {
    static char txBuffer[1024];
    static char dataBuffer[1024];
    
    // Tạo chuỗi transactions 
    txBuffer[0] = '\0';
    for (JsonVariant v : transactions) {
        String tmp;
        serializeJson(v, tmp);
        strcat(txBuffer, tmp.c_str());
    }
    
    // Tạo dữ liệu đầu vào
    snprintf(dataBuffer, sizeof(dataBuffer), "%d%s%lu%s%lu",
             height, prevHash.c_str(), timestamp, txBuffer, nonce);
    
    // Tính hash
    DSHA1 sha1;
    sha1.write((const unsigned char*)dataBuffer, strlen(dataBuffer));
    
    unsigned char hashResult[20];
    sha1.finalize(hashResult);
    
    // Chuyển sang hex
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

// kiểm tra mạng
bool checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("🔄 Mất WiFi, đang kết nối lại...");
        WiFi.reconnect();
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n✅ Đã kết nối lại WiFi");
            return true;
        } else {
            Serial.println("\n❌ Không thể kết nối lại WiFi");
            return false;
        }
    }
    return true;
}

// task đào chính
void miningTask(void* p) {
    int id = (int)(intptr_t)p;
    
    // Bộ đệm tĩnh cho target
    char targetStr[32];
    
    while (running) {
        // Kiểm tra WiFi
        if (!checkWiFi()) {
            delay(5000);
            continue;
        }
        
        // Lấy thông tin mạng
        DynamicJsonDocument info(4096);
        if (!getNetwork(info)) {
            Serial.printf("❌ Task %d: Không lấy được thông tin mạng\n", id);
            delay(2000);
            continue;
        }
        
        JsonObject latest = info["latestBlock"];
        
        int diff = difficultyOverride > 0 ? difficultyOverride : info["difficulty"].as<int>();
        int reward = info["reward"].as<int>();
        
        // Tạo target string
        for (int i = 0; i < diff; i++) targetStr[i] = '0';
        targetStr[diff] = '\0';
        
        int height = latest["height"].as<int>() + 1;
        String prevHash = latest["hash"].as<String>();
        
        // Lấy giao dịch pending
        DynamicJsonDocument pendingDoc(8192);
        JsonArray pending;
        if (getPending(pendingDoc)) pending = pendingDoc.as<JsonArray>();
        
        // Tạo block mới
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
            Serial.printf("\n🔨 [BLOCK %d] | TX: %d | Độ khó: %d | Thưởng: %d\n", 
                         height, (int)txs.size(), diff, reward);
        }
        
        // Bắt đầu đào
        unsigned long nonce = id * 10000000UL;
        unsigned long localHashCount = 0;
        unsigned long lastTimestampUpdate = timestamp;
        
        while (running) {
            // Cập nhật timestamp mỗi 0.5 giây
            if (nonce % 50000 == 0) {
                unsigned long newTimestamp = time(nullptr) * 1000;
                if (newTimestamp > lastTimestampUpdate + 500) {
                    timestamp = newTimestamp;
                    lastTimestampUpdate = timestamp;
                    if (txs.size() > 0) txs[0]["timestamp"] = timestamp;
                }
            }
            
            // Tính hash
            String hash = calculateBlockHash(height, prevHash, timestamp, txs, nonce);
            
            // Kiểm tra kết quả
            if (strncmp(hash.c_str(), targetStr, diff) == 0) {
                Serial.printf("\n🎉 [FOUND] Task %d | Nonce: %lu | Hash: %s\n", id, nonce, hash.c_str());
                
                if (submitBlock(height, nonce, hash, prevHash, reward, txs)) {
                    portENTER_CRITICAL(&mux);
                    blocksMined++;
                    totalReward += reward;
                    portEXIT_CRITICAL(&mux);
                    
                    Serial.printf("✅ Block %d đã được chấp nhận! +%d WebCoin\n", height, reward);
                } else {
                    Serial.printf("❌ Block %d bị từ chối\n", height);
                }
                break;
            }
            
            nonce++;
            localHashCount++;
            
            // Cập nhật thống kê mỗi 10000 hash
            if (localHashCount >= 10000) {
                portENTER_CRITICAL(&mux);
                totalHashes += localHashCount;
                portEXIT_CRITICAL(&mux);
                localHashCount = 0;
            }
            
            // Yield cho các task khác (chỉ khi cần)
            if (cpuPercent < 100 && nonce % 50000 == 0) {
                vTaskDelay(1 / portTICK_PERIOD_MS);
            }
        }
    }
    
    vTaskDelete(NULL);
}

// ===== SETUP =====
void setup() {
    // Khởi tạo Serial cho ESP32-S3
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n🚀 WebCoin Miner cho ESP32-S3");
    Serial.println("=================================");
    
    // Khởi tạo LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    // Đọc thông tin ví từ bộ nhớ
    prefs.begin("webcoin", false);
    walletAddress = prefs.getString("wallet", "");
    walletPassword = prefs.getString("pass", "");
    
    if (walletAddress == "") {
        Serial.println("📝 Chưa có thông tin ví!");
        Serial.print("Nhập địa chỉ ví (dạng W_...): ");
        while (!Serial.available());
        walletAddress = Serial.readStringUntil('\n');
        walletAddress.trim();
        
        Serial.print("Nhập mật khẩu: ");
        while (!Serial.available());
        walletPassword = Serial.readStringUntil('\n');
        walletPassword.trim();
        
        prefs.putString("wallet", walletAddress);
        prefs.putString("pass", walletPassword);
        Serial.println("✅ Đã lưu thông tin ví!");
    } else {
        Serial.println("✅ Đã đọc thông tin ví từ bộ nhớ");
    }
    
    // Kết nối WiFi
    Serial.printf("\n📶 Đang kết nối WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 40) {
        delay(500);
        Serial.print(".");
        wifiAttempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n✅ WiFi đã kết nối! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n❌ Không thể kết nối WiFi!");
        return;
    }
    
    // Đồng bộ thời gian
    Serial.print("🕐 Đang đồng bộ thời gian...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    time_t now = time(nullptr);
    while (now < 100000) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.printf("\n✅ Thời gian: %s", ctime(&now));
    
    // Đăng nhập vào server
    Serial.print("🔑 Đang đăng nhập...");
    if (!login()) {
        Serial.println("\n❌ Đăng nhập thất bại!");
        return;
    }
    Serial.println(" ✅ Thành công!");
    
    startTime = millis();
    
    // Khởi tạo các task đào
    Serial.printf("\n⛏️  Bắt đầu đào với %d luồng...\n", cpuThreads);
    for (int i = 0; i < cpuThreads; i++) {
        xTaskCreatePinnedToCore(
            miningTask,           // Hàm task
            "miner",              // Tên task
            8192,                 // Kích thước stack
            (void*)i,             // Tham số
            1,                    // Độ ưu tiên
            NULL,                 // Handle
            i % 2                 // Core (0 hoặc 1)
        );
    }
    
    Serial.println("\n✅ Miner đã sẵn sàng!\n");
}

// ===== LOOP CHÍNH =====
void loop() {
    static unsigned long lastStats = 0;
    static unsigned long lastNetworkCheck = 0;
    
    delay(5000);  // Cập nhật mỗi 5 giây
    
    // Tính tốc độ đào
    unsigned long now = millis();
    float elapsedSeconds = (now - lastStats) / 1000.0;
    
    if (lastStats > 0 && elapsedSeconds > 0) {
        portENTER_CRITICAL(&mux);
        unsigned long hashes = totalHashes;
        portEXIT_CRITICAL(&mux);
        
        float speed = (hashes - lastStats) / elapsedSeconds;
        
        Serial.printf("\n📊 [THỐNG KÊ] %02d:%02d:%02d\n", 
                     (now/3600000)%24, (now/60000)%60, (now/1000)%60);
        Serial.printf("   ⚡ Tốc độ: %.2f H/s\n", speed);
        Serial.printf("   📦 Blocks: %d\n", blocksMined);
        Serial.printf("   💰 Tổng thưởng: %d WebCoin\n", totalReward);
        
        lastStats = hashes;
        
        // Nhấp nháy LED theo tốc độ
        int blinkTime = (speed > 100) ? 100 : (speed > 10) ? 500 : 1000;
        digitalWrite(LED_BUILTIN, HIGH);
        delay(blinkTime);
        digitalWrite(LED_BUILTIN, LOW);
    } else {
        lastStats = totalHashes;
    }
}
