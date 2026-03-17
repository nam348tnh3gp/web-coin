/*
 * WebCoin Miner for ArduinoDroid
 * Chạy trên Android qua ArduinoDroid app
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SHA1.h>
#include <Preferences.h>

// ============== CẤU HÌNH ==============
const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASS = "your_wifi_password";
const char* SERVER_URL = "https://webcoin-1n9d.onrender.com/api";
const int SOC_TIMEOUT = 10;

// ============== BIẾN TOÀN CỤC ==============
Preferences prefs;
String walletAddress = "";
String walletPassword = "";
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
    SHA1 sha1;
    sha1.update(data);
    uint8_t* hashBytes = sha1.finalize();
    
    // Chuyển sang hex string
    String hash = "";
    for (int i = 0; i < 20; i++) {
        if (hashBytes[i] < 0x10) hash += "0";
        hash += String(hashBytes[i], HEX);
    }
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
    http.begin(String(SERVER_URL) + "/info");
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
    http.begin(String(SERVER_URL) + "/pending");
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
    http.begin(String(SERVER_URL) + "/blocks/submit");
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
    doc["minerAddress"] = walletAddress;
    
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

// ============== THREAD ĐÀO ==============
void miningTask(void* parameter) {
    int threadId = (int)parameter;
    
    Serial.printf("🧵 Thread %d started\n", threadId);
    
    while (running) {
        // Lấy thông tin mạng
        DynamicJsonDocument* info = getNetworkInfo();
        if (!info || !(*info).containsKey("latestBlock")) {
            delay(100);
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
        
        // Lấy pending
        DynamicJsonDocument* pendingDoc = getPending();
        JsonArray pending = pendingDoc ? pendingDoc->as<JsonArray>() : JsonArray();
        
        // Tạo timestamp
        unsigned long timestamp = millis();
        
        // Tạo coinbase transaction
        DynamicJsonDocument coinbaseDoc(256);
        String publicKey = walletAddress.substring(2);
        coinbaseDoc["from"] = nullptr;
        coinbaseDoc["to"] = publicKey;
        coinbaseDoc["amount"] = baseReward;
        coinbaseDoc["timestamp"] = timestamp;
        coinbaseDoc["signature"] = nullptr;
        
        // Tạo transactions array
        DynamicJsonDocument transactionsDoc(8192);
        JsonArray transactions = transactionsDoc.to<JsonArray>();
        transactions.add(coinbaseDoc.as<JsonVariant>());
        if (pendingDoc) {
            for (size_t i = 0; i < pending.size(); i++) {
                transactions.add(pending[i]);
            }
        }
        
        if (threadId == 0) {
            Serial.printf("\n📦 Block #%d - %d pending - Target: %s\n", height, pending.size(), target.c_str());
        }
        
        // Range nonce cho thread này
        unsigned long startNonce = threadId * 20000000UL;
        unsigned long endNonce = (threadId + 1) * 20000000UL;
        unsigned long nonce = startNonce;
        
        unsigned long startLocal = millis();
        unsigned long localHashes = 0;
        bool found = false;
        
        while (running && nonce < endNonce && !found) {
            // Tạo timestamp mới mỗi 10000 nonce
            if (nonce % 10000 == 0) {
                timestamp = millis();
                coinbaseDoc["timestamp"] = timestamp;
                transactions[0] = coinbaseDoc.as<JsonVariant>();
            }
            
            String hash = calculateBlockHash(height, prevHash, timestamp, transactions, nonce);
            localHashes++;
            
            if (hash.startsWith(target)) {
                found = true;
                unsigned long elapsed = (millis() - startLocal) / 1000;
                float speed = localHashes / (elapsed > 0 ? elapsed : 1);
                
                Serial.printf("\n🎯 Thread %d: Found nonce %lu\n", threadId, nonce);
                Serial.printf("   Speed: %.1f kH/s\n", speed/1000);
                Serial.printf("   Hash: %s\n", hash.substring(0, 30).c_str());
                
                if (threadId == 0 && authCookie.length() > 0) {
                    if (submitBlock(height, nonce, hash, prevHash, baseReward, transactions)) {
                        portENTER_CRITICAL(&mux);
                        blocksMined++;
                        totalReward += baseReward;
                        portEXIT_CRITICAL(&mux);
                        Serial.printf("✅✅✅ Block #%d ACCEPTED! +%d WBC ✅✅✅\n", height, baseReward);
                    } else {
                        Serial.printf("❌❌❌ Block #%d REJECTED! ❌❌❌\n", height);
                    }
                }
            }
            
            nonce++;
            
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
    
    Serial.println("\n" + String("=").repeat(60));
    Serial.println(" WEBCCOIN MINER FOR ANDROID");
    Serial.println("=" + String("=").repeat(59));
    
    // Đọc config từ Preferences
    prefs.begin("webcoin", false);
    walletAddress = prefs.getString("wallet", "");
    walletPassword = prefs.getString("password", "");
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
        if (!walletAddress.startsWith("W_")) {
            walletAddress = "W_" + walletAddress;
        }
        
        Serial.print("Mật khẩu ví: ");
        while (!Serial.available());
        walletPassword = Serial.readStringUntil('\n');
        walletPassword.trim();
        
        Serial.print("Số luồng (1-4) [2]: ");
        while (!Serial.available());
        String input = Serial.readStringUntil('\n');
        cpuThreads = input.toInt();
        if (cpuThreads < 1 || cpuThreads > 4) cpuThreads = 2;
        
        Serial.print("% CPU (10-100) [100]: ");
        while (!Serial.available());
        input = Serial.readStringUntil('\n');
        cpuPercent = input.toInt();
        if (cpuPercent < 10 || cpuPercent > 100) cpuPercent = 100;
        
        Serial.println("\n⚙️ Độ khó:");
        Serial.println(" 1 - Thấp (2 số 0)");
        Serial.println(" 2 - Trung bình (3 số 0)");
        Serial.println(" 3 - Cao (4 số 0)");
        Serial.println(" 4 - Tự động (theo mạng)");
        Serial.print("Chọn (1-4) [4]: ");
        while (!Serial.available());
        input = Serial.readStringUntil('\n');
        int choice = input.toInt();
        if (choice == 1) difficultyOverride = 2;
        else if (choice == 2) difficultyOverride = 3;
        else if (choice == 3) difficultyOverride = 4;
        else difficultyOverride = 0;
        
        // Lưu config
        prefs.putString("wallet", walletAddress);
        prefs.putString("password", walletPassword);
        prefs.putInt("threads", cpuThreads);
        prefs.putInt("cpu", cpuPercent);
        prefs.putInt("diff", difficultyOverride);
        Serial.println("✅ Config saved");
    }
    
    Serial.printf("\n📊 Wallet: %s...\n", walletAddress.substring(0, 20).c_str());
    Serial.printf("   Threads: %d | CPU: %d%%\n", cpuThreads, cpuPercent);
    
    // Kết nối WiFi
    Serial.printf("\n📡 Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi connected");
        Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n❌ WiFi failed!");
        return;
    }
    
    // Đăng nhập
    Serial.print("\n🔐 Logging in... ");
    if (login(walletAddress, walletPassword)) {
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
    Serial.printf("\n🚀 Starting %d threads at %d%% CPU\n", cpuThreads, cpuPercent);
    Serial.println("=" + String("=").repeat(59));
    
    startTime = millis();
    
    for (int i = 0; i < cpuThreads; i++) {
        xTaskCreatePinnedToCore(
            miningTask,
            "MiningTask",
            10000,
            (void*)i,
            1,
            NULL,
            i % 2
        );
    }
}

// ============== STATS ==============
void loop() {
    static unsigned long lastStats = 0;
    static unsigned long lastHashes = 0;
    static unsigned long lastTime = millis();
    
    delay(3000);
    
    unsigned long now = millis();
    unsigned long elapsed = (now - startTime) / 1000;
    
    portENTER_CRITICAL(&mux);
    unsigned long currentHashes = totalHashes;
    int currentBlocks = blocksMined;
    int currentReward = totalReward;
    portEXIT_CRITICAL(&mux);
    
    float speed = (currentHashes - lastHashes) / ((now - lastTime) / 1000.0);
    
    Serial.printf("\n📊 STATS [%lum %lus]\n", elapsed / 60, elapsed % 60);
    Serial.printf("   📈 Hashes: %lu | Speed: %.2f kH/s\n", currentHashes, speed/1000);
    Serial.printf("   ⛏️  Blocks: %d | Reward: %d WBC\n", currentBlocks, currentReward);
    
    lastHashes = currentHashes;
    lastTime = now;
}
