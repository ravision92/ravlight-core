#include <Arduino.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <WiFi.h>
#include <runtimeNVS.h>


#define UPDATE_INTERVAL 60000 // 1 minute in ms

uint32_t currentRuntime =  0;// loadFromNVS("current_hours", 0);
uint32_t totalRuntime = 0;

RTC_DATA_ATTR unsigned long sessionStartTime;
nvs_handle_t my_handle;


void saveToNVS(const char* key, uint32_t value) {
    esp_err_t err = nvs_set_u32(my_handle, key, value);
    if (err == ESP_OK) {
        nvs_commit(my_handle);
    }
}

uint32_t loadFromNVS(const char* key, uint32_t defaultValue) {
    uint32_t value = defaultValue;
    esp_err_t err = nvs_get_u32(my_handle, key, &value);
    return (err == ESP_OK) ? value : defaultValue;
}

void updateRuntime() {
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = millis();
        
        //uint32_t totalRuntime = loadFromNVS("total_hours", 0); 
        currentRuntime += 1;
        totalRuntime += 1;
        
       // saveToNVS("current_hours", currentRuntime);
        saveToNVS("total_hours", totalRuntime);   
        uint32_t currentHours = currentRuntime / 60;
        uint32_t currentMins = currentRuntime % 60;
        uint32_t totalHours = totalRuntime / 60;
        uint32_t totalMins = totalRuntime % 60;
        Serial.printf("Work hours - Current: %u:%u, Total: %u:%u\n", currentHours, currentMins, totalHours, totalMins); 
    }
}

void initNVS() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        Serial.println("[NVS] Failed to open NVS storage");
        return;
    }
    
    sessionStartTime = millis();
    currentRuntime = 0;
    totalRuntime = loadFromNVS("total_hours", 0);

    uint8_t mac[6];
    WiFi.macAddress(mac);
    char serial[13];
    snprintf(serial, sizeof(serial), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    setSerialNumber(serial);
    Serial.printf("Serial Number: %s\n", serial);
}


void setSerialNumber(const char* serial) {
    esp_err_t err = nvs_set_str(my_handle, "serial_number", serial);
    if (err == ESP_OK) {
        nvs_commit(my_handle);
    }
}

/*
String getSerialNumber() {
    size_t required_size;
    if (nvs_get_str(my_handle, "serial_number", NULL, &required_size) == ESP_OK) {
        char* serial = (char*)malloc(required_size);
        nvs_get_str(my_handle, "serial_number", serial, &required_size);
        String result = String(serial);
        free(serial);
        return result;
    }
    return "N/A";
}*/
String getSerialNumber(){
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return macStr;
}
