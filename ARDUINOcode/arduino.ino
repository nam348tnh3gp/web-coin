#include <Arduino.h>
#include <EEPROM.h>
#include "duco_hash.h"
#include "uniqueID.h"

#define LED_PIN LED_BUILTIN
#define BAUD_RATE 115200
#define EEPROM_CONFIG_ADDR 0
#define CONFIG_VERSION 0x02

struct Config {
    uint8_t version;
    char wallet[100];
    uint8_t difficulty;
    uint32_t totalHashes;
    uint32_t totalBlocks;
    uint8_t identifier[UniqueIDbuffer];
} config;

duco_hash_state_t hasher;
unsigned long lastJobTime = 0;
unsigned long totalHashes = 0;
unsigned long totalBlocks = 0;
unsigned long startTime = 0;
bool mining = true;

String bytesToHex(uint8_t* bytes, size_t len) {
    String hex = "";
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] < 0x10) hex += "0";
        hex += String(bytes[i], HEX);
    }
    hex.toLowerCase();
    return hex;
}

uint8_t randomByte() {
    return (uint8_t)((analogRead(A0) + micros()) & 0xFF);
}

String generateSalt(uint8_t length) {
    uint8_t* buffer = (uint8_t*)malloc(length);
    if (!buffer) return "";
    for (uint8_t i = 0; i < length; i++) {
        buffer[i] = randomByte();
    }
    String salt = bytesToHex(buffer, length);
    free(buffer);
    return salt;
}

String calculateHMAC(const String& data, const String& key, const String& salt) {
    String hmacInput = key + salt;
    
    DSHA1 sha1;
    sha1.write((const unsigned char*)hmacInput.c_str(), hmacInput.length());
    unsigned char keyHash[20];
    sha1.finalize(keyHash);
    
    String innerPad = "";
    String outerPad = "";
    for (int i = 0; i < 20; i++) {
        innerPad += (char)(keyHash[i] ^ 0x36);
        outerPad += (char)(keyHash[i] ^ 0x5C);
    }
    
    DSHA1 innerSha1;
    innerSha1.write((const unsigned char*)innerPad.c_str(), 20);
    innerSha1.write((const unsigned char*)data.c_str(), data.length());
    unsigned char innerHash[20];
    innerSha1.finalize(innerHash);
    
    DSHA1 outerSha1;
    outerSha1.write((const unsigned char*)outerPad.c_str(), 20);
    outerSha1.write(innerHash, 20);
    unsigned char hmacResult[20];
    outerSha1.finalize(hmacResult);
    
    return bytesToHex(hmacResult, 20);
}

String calculateBlockHash(int height, String prevHash, unsigned long timestamp, 
                         const String& txString, unsigned long nonce,
                         const String& miningSalt, const String& blockSalt) {
    char dataToHash[256];
    snprintf(dataToHash, sizeof(dataToHash), "%d%s%lu%s%lu%s%s",
             height, prevHash.c_str(), timestamp, txString.c_str(), nonce,
             miningSalt.c_str(), blockSalt.c_str());

    DSHA1 sha1;
    sha1.write((const unsigned char*)dataToHash, strlen(dataToHash));

    unsigned char hashResult[20];
    sha1.finalize(hashResult);

    return bytesToHex(hashResult, 20);
}

bool checkHashTarget(const String& hash, uint8_t difficulty) {
    for (uint8_t i = 0; i < difficulty; i++) {
        if (hash.charAt(i) != '0') return false;
    }
    return true;
}

void loadConfig() {
    EEPROM.get(EEPROM_CONFIG_ADDR, config);
    
    if (config.version != CONFIG_VERSION) {
        config.version = CONFIG_VERSION;
        memset(config.wallet, 0, sizeof(config.wallet));
        config.difficulty = 3;
        config.totalHashes = 0;
        config.totalBlocks = 0;
        memcpy(config.identifier, _UniqueID.id, UniqueIDbuffer);
        saveConfig();
    }
}

void saveConfig() {
    EEPROM.put(EEPROM_CONFIG_ADDR, config);
    EEPROM.commit();
}

void printConfig() {
    Serial.println(F("\nConfiguration:"));
    Serial.print(F("Wallet: "));
    Serial.println(config.wallet);
    Serial.print(F("Difficulty: "));
    Serial.println(config.difficulty);
    Serial.print(F("Identifier: "));
    for (int i = 0; i < UniqueIDsize; i++) {
        if (_UniqueID.id[i] < 0x10) Serial.print('0');
        Serial.print(_UniqueID.id[i], HEX);
    }
    Serial.println();
    Serial.print(F("Total Hashes: "));
    Serial.println(config.totalHashes);
    Serial.print(F("Total Blocks: "));
    Serial.println(config.totalBlocks);
    Serial.println();
}

void blinkLED(int count, int delayMs) {
    for (int i = 0; i < count; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_PIN, LOW);
        delay(delayMs);
    }
}

void processJob(String jobData) {
    int comma1 = jobData.indexOf(',');
    int comma2 = jobData.indexOf(',', comma1 + 1);
    int comma3 = jobData.indexOf(',', comma2 + 1);
    int comma4 = jobData.indexOf(',', comma3 + 1);
    int comma5 = jobData.indexOf(',', comma4 + 1);
    int comma6 = jobData.indexOf(',', comma5 + 1);
    int comma7 = jobData.indexOf(',', comma6 + 1);
    
    if (comma1 == -1 || comma2 == -1 || comma3 == -1 || comma4 == -1 || comma5 == -1) {
        Serial.println(F("ERROR,Invalid job format"));
        return;
    }
    
    String heightStr = jobData.substring(comma1 + 1, comma2);
    String prevHash = jobData.substring(comma2 + 1, comma3);
    String timestampStr = jobData.substring(comma3 + 1, comma4);
    String difficultyStr = jobData.substring(comma4 + 1, comma5);
    String rewardStr = jobData.substring(comma5 + 1, comma6);
    
    uint32_t height = heightStr.toInt();
    uint32_t timestamp = timestampStr.toInt();
    uint8_t difficulty = difficultyStr.toInt();
    uint32_t reward = rewardStr.toInt();
    
    String txString = "";
    if (comma6 != -1 && comma7 != -1) {
        txString = jobData.substring(comma6 + 1, comma7);
    }
    
    String miningSalt = "";
    String blockSalt = "";
    if (comma7 != -1) {
        miningSalt = jobData.substring(comma7 + 1, jobData.indexOf(',', comma7 + 1));
        blockSalt = jobData.substring(jobData.indexOf(',', comma7 + 1) + 1);
    } else {
        miningSalt = generateSalt(8);
        blockSalt = generateSalt(8);
    }
    
    unsigned long nonce = 0;
    unsigned long startTime = micros();
    unsigned long hashCount = 0;
    
    while (mining) {
        String hash = calculateBlockHash(height, prevHash, timestamp, txString, nonce, miningSalt, blockSalt);
        hashCount++;
        totalHashes++;
        config.totalHashes++;
        
        if (checkHashTarget(hash, difficulty)) {
            unsigned long elapsed = micros() - startTime;
            float hashrate = (hashCount * 1000000.0) / elapsed;
            
            String workerSalt = generateSalt(16);
            String blockData = String(height) + hash + prevHash + String(nonce);
            String blockHMAC = calculateHMAC(blockData, config.wallet, workerSalt);
            
            Serial.print(F("RESULT,"));
            Serial.print(nonce);
            Serial.print(F(","));
            Serial.print(elapsed);
            Serial.print(F(","));
            Serial.print(hash);
            Serial.print(F(","));
            Serial.print(blockHMAC);
            Serial.print(F(","));
            Serial.print(workerSalt);
            Serial.print(F(","));
            Serial.print(miningSalt);
            Serial.print(F(","));
            Serial.println(blockSalt);
            
            if (difficulty >= config.difficulty) {
                totalBlocks++;
                config.totalBlocks++;
                blinkLED(2, 100);
            } else {
                blinkLED(1, 50);
            }
            
            saveConfig();
            
            Serial.print(F("DEBUG,Block found! Height: "));
            Serial.print(height);
            Serial.print(F(", Nonce: "));
            Serial.print(nonce);
            Serial.print(F(", Hash: "));
            Serial.print(hash.substring(0, 20));
            Serial.print(F("..., HMAC: "));
            Serial.print(blockHMAC.substring(0, 16));
            Serial.println(F("..."));
            
            break;
        }
        
        nonce++;
        
        if (nonce == 0) break;
        
        if (hashCount % 10000 == 0) {
            unsigned long elapsed = micros() - startTime;
            float hashrate = (hashCount * 1000000.0) / elapsed;
            
            Serial.print(F("PROGRESS,"));
            Serial.print(hashCount);
            Serial.print(F(","));
            Serial.print(hashrate / 1000.0);
            Serial.print(F(","));
            Serial.print(nonce);
            Serial.print(F(","));
            Serial.println(hash.substring(0, 8));
        }
    }
}

void processCommand(String cmd) {
    if (cmd.startsWith("PING")) {
        Serial.println(F("PONG"));
    }
    else if (cmd.startsWith("JOB,")) {
        processJob(cmd);
    }
    else if (cmd.startsWith("STATS")) {
        unsigned long uptime = millis() / 1000;
        Serial.print(F("STATS,"));
        Serial.print(config.totalHashes);
        Serial.print(F(","));
        Serial.print(config.totalBlocks);
        Serial.print(F(","));
        Serial.print(uptime / 3600);
        Serial.print(F("h"));
        Serial.print((uptime % 3600) / 60);
        Serial.print(F("m"));
        Serial.println();
    }
    else if (cmd.startsWith("CONFIG")) {
        printConfig();
    }
    else if (cmd.startsWith("RESET")) {
        config.totalHashes = 0;
        config.totalBlocks = 0;
        saveConfig();
        Serial.println(F("RESET_OK"));
    }
    else if (cmd.startsWith("DIFFICULTY,")) {
        int newDiff = cmd.substring(11).toInt();
        if (newDiff >= 1 && newDiff <= 10) {
            config.difficulty = newDiff;
            saveConfig();
            Serial.print(F("DIFFICULTY_OK,"));
            Serial.println(config.difficulty);
        } else {
            Serial.println(F("DIFFICULTY_ERROR"));
        }
    }
    else if (cmd.startsWith("STOP")) {
        mining = false;
        Serial.println(F("STOPPED"));
    }
    else if (cmd.startsWith("START")) {
        mining = true;
        Serial.println(F("STARTED"));
    }
    else if (cmd.startsWith("IDENTIFIER")) {
        Serial.print(F("IDENTIFIER,"));
        for (int i = 0; i < UniqueIDsize; i++) {
            if (_UniqueID.id[i] < 0x10) Serial.print('0');
            Serial.print(_UniqueID.id[i], HEX);
        }
        Serial.println();
    }
    else {
        Serial.println(F("UNKNOWN"));
    }
}

void setup() {
    Serial.begin(BAUD_RATE);
    while (!Serial) { ; }
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    randomSeed(analogRead(A0));
    
    loadConfig();
    
    Serial.println(F("\nWebCoin AVR Miner v2.0"));
    Serial.println(F("(SHA1 + HMAC + Salt)"));
    Serial.println();
    
    Serial.print(F("Board: "));
    Serial.println(F(__AVR_ARCH__ ? "AVR" : "Unknown"));
    
    Serial.print(F("Identifier: "));
    for (int i = 0; i < UniqueIDsize; i++) {
        if (_UniqueID.id[i] < 0x10) Serial.print('0');
        Serial.print(_UniqueID.id[i], HEX);
    }
    Serial.println();
    
    Serial.print(F("Firmware: "));
    Serial.println(F(__DATE__ " " __TIME__));
    
    Serial.print(F("Free RAM: "));
    Serial.print(freeRAM());
    Serial.println(F(" bytes"));
    
    Serial.println();
    printConfig();
    
    blinkLED(3, 200);
    
    Serial.println(F("READY"));
}

void loop() {
    static String inputBuffer = "";
    
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            inputBuffer.trim();
            if (inputBuffer.length() > 0) {
                processCommand(inputBuffer);
            }
            inputBuffer = "";
        } else {
            inputBuffer += c;
        }
    }
    
    if (!mining) {
        delay(100);
    }
    
    if (mining && millis() % 1000 < 50) {
        digitalWrite(LED_PIN, HIGH);
    } else {
        digitalWrite(LED_PIN, LOW);
    }
}

int freeRAM() {
    extern int __heap_start, *__brkval;
    int v;
    return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
