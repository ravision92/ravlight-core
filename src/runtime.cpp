#include <Arduino.h>
#include <nvs.h>
#include <esp_efuse.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "runtime.h"

#define TAG             "RT"
#define UPDATE_INTERVAL 60000   // ms
#define NVS_NAMESPACE   "runtime"

uint32_t currentRuntime = 0;
uint32_t totalRuntime   = 0;

RTC_DATA_ATTR unsigned long sessionStartTime;

static nvs_handle_t rtHandle;

static void saveToNVS(const char* key, uint32_t value) {
    esp_err_t err = nvs_set_u32(rtHandle, key, value);
    if (err == ESP_OK) nvs_commit(rtHandle);
}

static uint32_t loadFromNVS(const char* key, uint32_t defaultValue) {
    uint32_t value = defaultValue;
    nvs_get_u32(rtHandle, key, &value);
    return value;
}

// nvs_flash_init() is owned by config.cpp (called before initRuntime())
void initRuntime() {
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &rtHandle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS runtime namespace");
        return;
    }

    sessionStartTime = (uint32_t)(esp_timer_get_time() / 1000ULL);
    currentRuntime   = 0;

    // One-shot migration from legacy "storage" namespace (old firmware)
    uint32_t saved = 0;
    if (nvs_get_u32(rtHandle, "total_hours", &saved) == ESP_ERR_NVS_NOT_FOUND) {
        nvs_handle_t legacyHandle;
        if (nvs_open("storage", NVS_READONLY, &legacyHandle) == ESP_OK) {
            uint32_t legacy = 0;
            if (nvs_get_u32(legacyHandle, "total_hours", &legacy) == ESP_OK && legacy > 0) {
                saved = legacy;
                nvs_set_u32(rtHandle, "total_hours", saved);
                nvs_commit(rtHandle);
                ESP_LOGI(TAG, "Migrated total_hours from legacy NVS: %u min", saved);
            }
            nvs_close(legacyHandle);
        }
    }
    totalRuntime = saved;

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    ESP_LOGI(TAG, "Serial: %02X%02X%02X", mac[3], mac[4], mac[5]);
}

void updateRuntime() {
    static uint32_t lastUpdate = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (now - lastUpdate < UPDATE_INTERVAL) return;
    lastUpdate = now;

    currentRuntime++;
    totalRuntime++;
    saveToNVS("total_hours", totalRuntime);

    ESP_LOGI(TAG, "Uptime — current: %u:%02u, total: %u:%02u",
        currentRuntime / 60, currentRuntime % 60,
        totalRuntime   / 60, totalRuntime   % 60);
}

String getSerialNumber() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}
