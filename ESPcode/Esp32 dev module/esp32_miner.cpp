#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "DSHA1.h"

// Định nghĩa LED
#define LED_PIN GPIO_NUM_2
#define SERVER_URL "https://webcoin-1n9d.onrender.com/api"

static const char *TAG = "WEBCOIN_MINER";

// Cấu hình WiFi (sửa lại theo mạng của bạn)
const char* WIFI_SSID = "THANH PHONG";
const char* WIFI_PASS = "matkhauwifi";

int cpuThreads = 2;
int cpuPercent = 100;
int difficultyOverride = 0;

// Biến toàn cục
static char walletAddress[100] = "";
static char walletPassword[64] = "";
static char walletPublicKey[256] = "";
static char authCookie[128] = "";

static volatile unsigned long totalHashes = 0;
static volatile int blocksMined = 0;
static volatile int totalReward = 0;

static bool running = true;
static unsigned long startTime;

static SemaphoreHandle_t mux;

// Lớp String đơn giản (thay thế Arduino String)
class String {
private:
    char* buffer;
    size_t len;
    
public:
    String() : buffer(nullptr), len(0) {}
    
    String(const char* str) {
        len = strlen(str);
        buffer = (char*)malloc(len + 1);
        if (buffer) {
            strcpy(buffer, str);
        }
    }
    
    String(const String& other) {
        len = other.len;
        buffer = (char*)malloc(len + 1);
        if (buffer) {
            strcpy(buffer, other.buffer);
        }
    }
    
    ~String() {
        if (buffer) free(buffer);
    }
    
    String& operator=(const char* str) {
        if (buffer) free(buffer);
        len = strlen(str);
        buffer = (char*)malloc(len + 1);
        if (buffer) strcpy(buffer, str);
        return *this;
    }
    
    String operator+(const char* str) const {
        String result;
        size_t newLen = len + strlen(str);
        result.buffer = (char*)malloc(newLen + 1);
        if (result.buffer) {
            strcpy(result.buffer, buffer);
            strcat(result.buffer, str);
            result.len = newLen;
        }
        return result;
    }
    
    String operator+(const String& other) const {
        return *this + other.buffer;
    }
    
    void operator+=(const char* str) {
        *this = *this + str;
    }
    
    const char* c_str() const { return buffer ? buffer : ""; }
    size_t length() const { return len; }
    
    void toLowerCase() {
        if (buffer) {
            for (size_t i = 0; i < len; i++) {
                buffer[i] = tolower(buffer[i]);
            }
        }
    }
};

// Hàm băm
String calculateBlockHash(int height, String prevHash, unsigned long timestamp, cJSON* transactions, unsigned long nonce) {
    static char txBuffer[4096];
    static char dataBuffer[1024];
    
    txBuffer[0] = '\0';
    
    if (transactions) {
        char *txStr = cJSON_Print(transactions);
        if (txStr) {
            strncat(txBuffer, txStr, sizeof(txBuffer) - strlen(txBuffer) - 1);
            free(txStr);
        }
    }
    
    snprintf(dataBuffer, sizeof(dataBuffer), "%d%s%lu%s%lu",
             height, prevHash.c_str(), timestamp, txBuffer, nonce);
    
    DSHA1 sha1;
    sha1.write((const unsigned char*)dataBuffer, strlen(dataBuffer));
    
    unsigned char hashResult[20];
    sha1.finalize(hashResult);
    
    String hash = "";
    for (int i = 0; i < 20; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", hashResult[i]);
        hash += hex;
    }
    hash.toLowerCase();
    return hash;
}

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!evt->user_data) {
                evt->user_data = malloc(evt->data_len + 1);
                if (evt->user_data) {
                    memcpy(evt->user_data, evt->data, evt->data_len);
                    ((char*)evt->user_data)[evt->data_len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Hàm login
static bool login() {
    esp_http_client_config_t config = {
        .url = SERVER_URL "/login",
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
        .cert_pem = NULL,
        .skip_cert_common_name_check = true,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char payload[512];
    snprintf(payload, sizeof(payload), "{\"displayAddress\":\"%s\",\"password\":\"%s\"}", 
             walletAddress, walletPassword);
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));
    
    ESP_LOGI(TAG, "\n----- DEBUG LOGIN -----");
    ESP_LOGI(TAG, "Gui len server: %s", payload);
    
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Ma phan hoi HTTP: %d", status_code);
        
        if (status_code == 200) {
            char *response = (char*)esp_http_client_get_user_data(client);
            if (response) {
                ESP_LOGI(TAG, "Server tra ve: %s", response);
                
                cJSON *doc = cJSON_Parse(response);
                if (doc) {
                    cJSON *error = cJSON_GetObjectItem(doc, "error");
                    if (!error || cJSON_IsNull(error)) {
                        
                        cJSON *pk = cJSON_GetObjectItem(doc, "publicKey");
                        if (pk && cJSON_IsString(pk)) {
                            strncpy(walletPublicKey, pk->valuestring, sizeof(walletPublicKey) - 1);
                            ESP_LOGI(TAG, "PublicKey nhan duoc: %s", walletPublicKey);
                        }
                        
                        cJSON_Delete(doc);
                        free(response);
                        esp_http_client_cleanup(client);
                        ESP_LOGI(TAG, "----- DANG NHAP THANH CONG -----\n");
                        return true;
                    } else {
                        ESP_LOGE(TAG, "Loi tu server: %s", error->valuestring);
                        cJSON_Delete(doc);
                    }
                }
                free(response);
            }
        } else {
            ESP_LOGE(TAG, "Loi response code: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "Khong the ket noi den server: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    ESP_LOGE(TAG, "----- DANG NHAP THAT BAI -----\n");
    return false;
}

// Các hàm API khác (giả lập - cần implement thêm)
static bool getNetwork(cJSON *doc) {
    // TODO: Implement call /info
    return false;
}

static bool getPending(cJSON *doc) {
    // TODO: Implement call /pending
    return false;
}

static bool submitBlock(int height, unsigned long nonce, String hash, String prevHash, int reward, cJSON *transactions) {
    // TODO: Implement call /blocks/submit
    return false;
}

// Kiểm tra WiFi
static bool checkWiFi() {
    // TODO: Implement WiFi check
    return true;
}

// Task đào
static void miningTask(void *pvParameters) {
    int id = (int)pvParameters;
    char targetStr[32];
    
    while (running) {
        if (!checkWiFi()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        cJSON *info = cJSON_CreateObject();
        if (!getNetwork(info)) {
            cJSON_Delete(info);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        
        // TODO: Xử lý đào block
        
        cJSON_Delete(info);
    }
    
    vTaskDelete(NULL);
}

// Reset thông tin ví
static void resetWalletInfo() {
    ESP_LOGI(TAG, "\n----- RESET THONG TIN VI -----");
    
    nvs_handle_t nvs;
    if (nvs_open("webcoin", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    
    memset(walletAddress, 0, sizeof(walletAddress));
    memset(walletPassword, 0, sizeof(walletPassword));
    memset(walletPublicKey, 0, sizeof(walletPublicKey));
    memset(authCookie, 0, sizeof(authCookie));
    
    ESP_LOGI(TAG, "Da xoa thong tin cu. Khoi dong lai de nhap moi!");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

// Hàm chính
extern "C" void app_main(void) {
    // Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Khởi tạo mạng
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Kết nối WiFi (cần cấu hình menuconfig)
    ESP_ERROR_CHECK(example_connect());
    
    // Khởi tạo LED
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);
    
    // Tạo mutex
    mux = xSemaphoreCreateMutex();
    
    // Đọc thông tin từ NVS
    nvs_handle_t nvs;
    if (nvs_open("webcoin", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(walletAddress);
        nvs_get_str(nvs, "wallet", walletAddress, &len);
        len = sizeof(walletPassword);
        nvs_get_str(nvs, "pass", walletPassword, &len);
        len = sizeof(walletPublicKey);
        nvs_get_str(nvs, "pubkey", walletPublicKey, &len);
        nvs_close(nvs);
    }
    
    ESP_LOGI(TAG, "\n\n=================================");
    ESP_LOGI(TAG, "   WebCoin Miner cho ESP32");
    ESP_LOGI(TAG, "=================================");
    
    ESP_LOGI(TAG, "Thong tin hien tai:");
    ESP_LOGI(TAG, "Dia chi vi: %s", strlen(walletAddress) > 0 ? walletAddress : "Chua co");
    ESP_LOGI(TAG, "Mat khau: %s", strlen(walletPassword) > 0 ? "Da luu" : "Chua co");
    ESP_LOGI(TAG, "PublicKey: %s", strlen(walletPublicKey) > 0 ? walletPublicKey : "Chua co");
    
    if (strlen(walletAddress) == 0 || strlen(walletPassword) == 0) {
        ESP_LOGI(TAG, "\n----- NHAP THONG TIN VI -----");
        ESP_LOGI(TAG, "Vui long nhap dia chi vi (bat dau bang W_...): ");
        
        // TODO: Đọc từ UART (console)
        // Tạm thời dùng giá trị mẫu
        strcpy(walletAddress, "W_0492a74e1cf24579ad22c758400e30a0ddbcb9e2");
        strcpy(walletPassword, "16052012");
        
        if (nvs_open("webcoin", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "wallet", walletAddress);
            nvs_set_str(nvs, "pass", walletPassword);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        ESP_LOGI(TAG, "[OK] Da luu thong tin vi!");
    } else {
        ESP_LOGI(TAG, "[OK] Da doc thong tin vi tu bo nho");
    }
    
    // Đợi WiFi kết nối
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Đăng nhập
    ESP_LOGI(TAG, "\n[LOGIN] Dang dang nhap...");
    if (!login()) {
        ESP_LOGE(TAG, "\n[LOGIN] That bai! Kiem tra lai dia chi vi va mat khau.");
        return;
    }
    
    ESP_LOGI(TAG, "[LOGIN] Thanh cong!");
    startTime = esp_timer_get_time() / 1000;
    
    // Tạo task đào
    ESP_LOGI(TAG, "\n[MINER] Bat dau dao voi %d luong...\n", cpuThreads);
    for (int i = 0; i < cpuThreads; i++) {
        xTaskCreatePinnedToCore(
            miningTask,
            "miner",
            4096,
            (void*)i,
            1,
            NULL,
            i % 2
        );
    }
    
    ESP_LOGI(TAG, "\n[Miner] San sang! Dang dao...\n");
    
    // Loop chính
    static unsigned long lastStats = 0;
    static unsigned long lastLedBlink = 0;
    static bool ledState = false;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        unsigned long now = esp_timer_get_time() / 1000;
        
        if (lastStats > 0) {
            float speed = (totalHashes - lastStats) / 5.0;
            
            ESP_LOGI(TAG, "\n[THONG KE] %.2f H/s | Blocks: %d | Reward: %d",
                     speed, blocksMined, totalReward);
            
            lastStats = totalHashes;
            
            int blinkTime = (speed > 100) ? 100 : (speed > 10) ? 500 : 1000;
            if (now - lastLedBlink > blinkTime) {
                ledState = !ledState;
                gpio_set_level(LED_PIN, ledState ? 1 : 0);
                lastLedBlink = now;
            }
        } else {
            lastStats = totalHashes;
        }
    }
}
