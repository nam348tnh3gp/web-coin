/*
 * WebCoin ESP32 Miner - FIXED (không dùng containsKey)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "DSHA1.h"
#include <time.h>

// ============== CẤU HÌNH ==============
const char* WIFI_SSID = "your_wifi_ssid";        // Đã sửa: thêm dấu "
const char* WIFI_PASS = "your_wifi_password";    // Đã sửa: thêm dấu "
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";
const int SOC_TIMEOUT = 10;

// ============== BIẾN TOÀN CỤC ==============
Preferences prefs;
String walletAddress = "";
String walletPassword = "";
String walletPublicKey = "";
String authCookie = "";
int difficultyOverride = 0; // 0 = auto
int cpuThreads = 2;
int cpuPercent = 100;

// Stats
volatile unsigned long totalHashes = 0;
volatile int blocksMined = 0;
volatile int totalReward = 0;
unsigned long startTime = 0;
bool running = true;

// Mutex cho thread
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

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
    // Tạo txString giống JS: JSON.stringify(tx) và join('')
    String txString = "";
    for (size_t i = 0; i < transactions.size(); i++) {
        // Serialize từng transaction thành JSON string
        String txJson;
        serializeJson(transactions[i], txJson);
        txString += txJson;
    }
    
    // Tạo data string giống JS: height + prevHash + timestamp + txString + nonce
    String data = String(height) + prevHash + String(timestamp) + txString + String(nonce);
    
    // Dùng DSHA1 để tính hash (giống CryptoJS.SHA1)
    DSHA1 sha1;
    sha1.write((const unsigned char*)data.c_str(), data.length());
    
    unsigned char hashResult[20];
    sha1.finalize(hashResult);
    
    // Chuyển sang hex string
    String hash = "";
    for (int i = 0; i < 20; i++) {
        if (hashResult[i] < 0x10) hash += "0";
        hash += String(hashResult[i], HEX);
    }
    // Chuyển thành chữ thường cho giống JS
    hash.toLowerCase();
    return hash;
}

// ============== API FUNCTIONS ==============
bool login(String wallet, String password) {
    HTTPClient http;
    http.begin(String(SERVER_URL) + "/login");
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{\"displayAddress\":\"" + wallet + "\",\"password\":\"" + password + "\"}";
    int httpCode = http.POST(payload);
    
    if (httpCode == 200) {
        String response = http.getString();
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, response);
        
        // Cách 2: Dùng .success() và .isNull() thay vì containsKey
        if (doc["error"].isNull()) {  // Không có lỗi
            // Lưu publicKey từ response
            if (doc["publicKey"].success()) {  // Kiểm tra publicKey tồn tại
                walletPublicKey = doc["publicKey"].as<String>();
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
    http.begin(String(SERVER_URL) + "/info");
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
        
        // Kiểm tra có dữ liệu hợp lệ không
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
    http.begin(String(SERVER_URL) + "/pending");
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
    http.begin(String(SERVER_URL) + "/blocks/submit");
    http.addHeader("Content-Type", "application/json");
    if (authCookie.length() > 0) {
        http.addHeader("Cookie", authCookie);
    }
    
    // Tạo payload giống JS
    DynamicJsonDocument doc(16384);
    doc["height"] = height;
    doc["previousHash"] = prevHash;
    doc["timestamp"] = (unsigned long)time(nullptr) * 1000;
    doc["nonce"] = nonce;
    doc["hash"] = hash;
    doc["minerAddress"] = walletAddress;
    
    // Thêm transactions array
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

// ============== THREAD ĐÀO ==============
void miningTask(void* parameter) {
    int threadId = (int)(intptr_t)parameter;
    
    Serial.printf("🧵 Thread %d started\n", threadId);
    
    while (running) {
        // Lấy thông tin mạng
        DynamicJsonDocument* info = getNetworkInfo();
        // Cách 2: Kiểm tra bằng .success() thay vì .containsKey()
        if (!info || !(*info)["latestBlock"].success()) {
            delete info;
            delay(1000);
            continue;
        }
        
        JsonObject latest = (*info)["latestBlock"];
        int networkDiff = (*info)["difficulty"] | 3;
        int baseReward = (*info)["reward"] | 48;
        
        int difficulty = difficultyOverride > 0 ? difficultyOverride : networkDiff;
        String target = "";
        for (int i = 0; i < difficulty; i++) target += "0";
        
        int height = latest["height"].as<int>() + 1;
        String prevHash = latest["hash"].as<String>();
        
        // Lấy pending transactions
        DynamicJsonDocument* pendingDoc = getPending();
        JsonArray pending = pendingDoc ? pendingDoc->as<JsonArray>() : JsonArray();
        
        // Lấy thời gian thực
        time_t now = time(nullptr);
        unsigned long timestamp = (unsigned long)now * 1000;
        
        // Tạo coinbase transaction
        DynamicJsonDocument coinbaseDoc(512);
        coinbaseDoc["from"] = nullptr;
        coinbaseDoc["to"] = walletPublicKey;
        coinbaseDoc["amount"] = baseReward;
        coinbaseDoc["timestamp"] = timestamp;
        coinbaseDoc["signature"] = nullptr;
        
        // Tạo transactions array
        DynamicJsonDocument transactionsDoc(16384);
        JsonArray transactions = transactionsDoc.to<JsonArray>();
        transactions.add(coinbaseDoc.as<JsonVariant>());
        
        // Thêm pending transactions
        if (pendingDoc) {
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
        }
        
        if (threadId == 0) {
            Serial.printf("\n📦 Block #%d - %d pending - Target: %s (difficulty: %d)\n", 
                         height, pending.size(), target.c_str(), difficulty);
        }
        
        // Range nonce cho thread này
        unsigned long startNonce = threadId * 20000000UL;
        unsigned long endNonce = (threadId + 1) * 20000000UL;
        unsigned long nonce = startNonce;
        
        unsigned long startLocal = millis();
        unsigned long localHashes = 0;
        bool found = false;
        
        while (running && nonce < endNonce && !found) {
            // Cập nhật timestamp mỗi 10000 nonce
            if (nonce % 10000 == 0) {
                now = time(nullptr);
                timestamp = (unsigned long)now * 1000;
                coinbaseDoc["timestamp"] = timestamp;
                transactions[0] = coinbaseDoc.as<JsonVariant>();
            }
            
            // Tính hash
            String hash = calculateBlockHash(height, prevHash, timestamp, transactions, nonce);
            localHashes++;
            
            // Kiểm tra target
            if (hash.startsWith(target)) {
                found = true;
                unsigned long elapsedTime = (millis() - startLocal) / 1000;
                float speed = localHashes / (elapsedTime > 0 ? elapsedTime : 1);
                
                Serial.printf("\n🎯 Thread %d: Found nonce %lu\n", threadId, nonce);
                Serial.printf("   Speed: %.2f H/s\n", speed);
                Serial.printf("   Hash: %s\n", hash.c_str());
                
                // Thread 0 gửi block lên server
                if (threadId == 0 && authCookie.length() > 0) {
                    if (submitBlock(height, nonce, hash, prevHash, baseReward, transactions)) {
                        portENTER_CRITICAL(&mux);
                        blocksMined++;
                        totalReward += baseReward;
                        portEXIT_CRITICAL(&mux);
                    }
                }
            }
            
            nonce++;
            
            // Cập nhật tổng số hash
            if (nonce % 10000 == 0) {
                portENTER_CRITICAL(&mux);
                totalHashes += 10000;
                portEXIT_CRITICAL(&mux);
            }
            
            // Điều chỉnh CPU
            if (cpuPercent < 100 && nonce % 100000 == 0) {
                delay(1);
            }
        }
        
        delete info;
        if (pendingDoc) delete pendingDoc;
    }
    
    vTaskDelete(NULL);
}

// ============== SETUP ==============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    String line = createLine(60);
    Serial.println("\n" + line);
    Serial.println(" WEBCCOIN ESP32 MINER - FIXED VERSION");
    Serial.println(createLine(59));
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    // Đọc config từ Preferences
    prefs.begin("webcoin", false);
    walletAddress = prefs.getString("wallet", "");
    walletPassword = prefs.getString("password", "");
    walletPublicKey = prefs.getString("pubkey", "");
    cpuThreads = prefs.getInt("threads", 2);
    cpuPercent = prefs.getInt("cpu", 100);
    difficultyOverride = prefs.getInt("diff", 0);
    
    if (walletAddress.length() == 0) {
        // Nhập thông tin lần đầu
        Serial.println("\n📝 NHẬP THÔNG TIN:");
        
        Serial.print("Địa chỉ ví (W_...): ");
        while (!Serial.available());
        walletAddress = Serial.readStringUntil('\n');
        walletAddress.trim();
        
        Serial.print("Mật khẩu ví: ");
        while (!Serial.available());
        walletPassword = Serial.readStringUntil('\n');
        walletPassword.trim();
        
        Serial.print("Số luồng (1-2) [2]: ");
        while (!Serial.available());
        String input = Serial.readStringUntil('\n');
        cpuThreads = input.toInt();
        if (cpuThreads < 1 || cpuThreads > 2) cpuThreads = 2;
        
        Serial.print("% CPU (10-100) [100]: ");
        while (!Serial.available());
        input = Serial.readStringUntil('\n');
        cpuPercent = input.toInt();
        if (cpuPercent < 10 || cpuPercent > 100) cpuPercent = 100;
        
        Serial.println("\n⚙️ Độ khó:");
        Serial.println(" 0 - Tự động (theo mạng)");
        Serial.println(" 2 - Thấp (2 số 0)");
        Serial.println(" 3 - Trung bình (3 số 0)");
        Serial.println(" 4 - Cao (4 số 0)");
        Serial.print("Chọn (0-4) [0]: ");
        while (!Serial.available());
        input = Serial.readStringUntil('\n');
        int choice = input.toInt();
        if (choice == 2) difficultyOverride = 2;
        else if (choice == 3) difficultyOverride = 3;
        else if (choice == 4) difficultyOverride = 4;
        else difficultyOverride = 0;
        
        Serial.println("\n🔑 Đang đăng nhập để lấy publicKey...");
        
        // Đăng nhập để lấy publicKey
        if (login(walletAddress, walletPassword)) {
            Serial.println("✅ Đăng nhập thành công, đã lấy publicKey");
            // Lưu config
            prefs.putString("wallet", walletAddress);
            prefs.putString("password", walletPassword);
            prefs.putString("pubkey", walletPublicKey);
            prefs.putInt("threads", cpuThreads);
            prefs.putInt("cpu", cpuPercent);
            prefs.putInt("diff", difficultyOverride);
            Serial.println("✅ Config saved");
        } else {
            Serial.println("❌ Đăng nhập thất bại! Kiểm tra lại thông tin.");
            return;
        }
    }
    
    Serial.printf("\n📊 Wallet: %s\n", walletAddress.c_str());
    Serial.printf("   PublicKey: %s...\n", walletPublicKey.substring(0, 30).c_str());
    Serial.printf("   Threads: %d | CPU: %d%%\n", cpuThreads, cpuPercent);
    
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
    
    // Đồng bộ thời gian NTP
    Serial.print("\n🕒 Syncing NTP...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    time_t now = time(nullptr);
    while (now < 100000) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.printf("\n✅ Time synced: %s", ctime(&now));
    
    // Đăng nhập nếu chưa có publicKey
    if (walletPublicKey.length() == 0) {
        Serial.print("\n🔐 Logging in... ");
        if (login(walletAddress, walletPassword)) {
            Serial.println("✅ Success!");
            prefs.putString("pubkey", walletPublicKey);
        } else {
            Serial.println("❌ Failed!");
            return;
        }
    } else {
        // Đăng nhập để lấy cookie
        Serial.print("\n🔐 Logging in... ");
        if (login(walletAddress, walletPassword)) {
            Serial.println("✅ Success!");
        } else {
            Serial.println("❌ Failed!");
            return;
        }
    }
    
    // Lấy thông tin mạng
    DynamicJsonDocument* info = getNetworkInfo();
    if (info) {
        Serial.printf("📡 Network: diff=%d, reward=%d WBC, pending=%d\n", 
                     (*info)["difficulty"].as<int>(), 
                     (*info)["reward"].as<int>(),
                     (*info)["pendingCount"].as<int>());
        delete info;
    }
    
    // Bắt đầu đào
    Serial.printf("\n🚀 Starting %d threads at %d%% CPU\n", cpuThreads, cpuPercent);
    Serial.println(createLine(59));
    
    startTime = millis();
    
    for (int i = 0; i < cpuThreads; i++) {
        xTaskCreatePinnedToCore(
            miningTask,
            "MiningTask",
            16384,
            (void*)(intptr_t)i,
            1,
            NULL,
            i % 2
        );
    }
}

// ============== STATS ==============
void loop() {
    static unsigned long lastHashes = 0;
    static unsigned long lastTime = millis();
    
    delay(5000);
    
    unsigned long now = millis();
    unsigned long elapsed = (now - startTime) / 1000;
    
    portENTER_CRITICAL(&mux);
    unsigned long currentHashes = totalHashes;
    int currentBlocks = blocksMined;
    int currentReward = totalReward;
    portEXIT_CRITICAL(&mux);
    
    float speed = (currentHashes - lastHashes) / ((now - lastTime) / 1000.0);
    
    Serial.printf("\n📊 STATS [%lum %lus]\n", elapsed / 60, elapsed % 60);
    Serial.printf("   📈 Hashes: %lu | Speed: %.2f H/s\n", currentHashes, speed);
    Serial.printf("   ⛏️  Blocks: %d | Reward: %d WBC\n", currentBlocks, currentReward);
    
    // Nhấp nháy LED theo hashrate
    if (speed > 0) {
        int blinkDelay = 1000 / (speed / 100);
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(blinkDelay);
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    
    lastHashes = currentHashes;
    lastTime = now;
}
