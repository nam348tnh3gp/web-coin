#include <Arduino.h>
#include <EEPROM.h>
#include "duco_hash.h"
#include "uniqueID.h"

#define LED_PIN LED_BUILTIN
#define BAUD_RATE 115200
#define EEPROM_CONFIG_ADDR 0
#define CONFIG_VERSION 0x01

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

String bytesToHex(uint8_t* bytes, size_t len) {
    String hex = "";
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] < 0x10) hex += "0";
        hex += String(bytes[i], HEX);
    }
    return hex;
}

bool checkHashTarget(uint8_t* hash, uint8_t difficulty) {
    uint8_t leadingZeros = 0;
    for (int i = 0; i < 20; i++) {
        uint8_t byte = hash[i];
        
        if ((byte >> 4) == 0) {
            leadingZeros++;
            if ((byte & 0x0F) == 0) {
                leadingZeros++;
            } else {
                break;
            }
        } else {
            break;
        }
    }
    
    return leadingZeros >= difficulty;
}

void processJob(String jobData) {
    int comma1 = jobData.indexOf(',');
    int comma2 = jobData.indexOf(',', comma1 + 1);
    int comma3 = jobData.indexOf(',', comma2 + 1);
    int comma4 = jobData.indexOf(',', comma3 + 1);
    int comma5 = jobData.indexOf(',', comma4 + 1);
    
    if (comma1 == -1 || comma2 == -1 || comma3 == -1 || comma4 == -1 || comma5 == -1) {
        Serial.println(F("ERROR,Invalid job format"));
        return;
    }
    
    String heightStr = jobData.substring(comma1 + 1, comma2);
    String prevHash = jobData.substring(comma2 + 1, comma3);
    String timestampStr = jobData.substring(comma3 + 1, comma4);
    String difficultyStr = jobData.substring(comma4 + 1, comma5);
    String rewardStr = jobData.substring(comma5 + 1);
    
    uint32_t height = heightStr.toInt();
    uint32_t timestamp = timestampStr.toInt();
    uint8_t difficulty = difficultyStr.toInt();
    uint32_t reward = rewardStr.toInt();
    
    char dataToHash[128];
    unsigned long nonce = 0;
    unsigned long startTime = micros();
    unsigned long hashCount = 0;
    
    duco_hash_init(&hasher, prevHash.c_str());
    
    while (mining) {
        sprintf(dataToHash, "%lu%s%lu%lu", height, prevHash.c_str(), timestamp, nonce);
        
        uint8_t const* hashResult = duco_hash_try_nonce(&hasher, dataToHash + strlen(prevHash.c_str()));
        
        hashCount++;
        totalHashes++;
        config.totalHashes++;
        
        if (checkHashTarget((uint8_t*)hashResult, difficulty)) {
            unsigned long elapsed = micros() - startTime;
            float hashrate = (hashCount * 1000000.0) / elapsed;
            
            Serial.print(F("RESULT,"));
            Serial.print(nonce);
            Serial.print(F(","));
            Serial.print(elapsed);
            Serial.print(F(","));
            Serial.println(bytesToHex((uint8_t*)hashResult, 20));
            
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
            Serial.print(F(", Time: "));
            Serial.print(elapsed / 1000.0);
            Serial.print(F("ms, Hashrate: "));
            Serial.print(hashrate / 1000.0);
            Serial.println(F(" kH/s"));
            
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
            Serial.println(bytesToHex((uint8_t*)hashResult, 8));
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
    
    loadConfig();
    
    Serial.println(F("\nWebCoin AVR Miner v1.0"));
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
