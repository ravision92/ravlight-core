#if defined(RAVLIGHT_MASTER) || defined(RAVLIGHT_MODULE_DISCOVERY)
#include "discovery_shared.h"
#include "discovery_udp.h"
#include "discovery_espnow.h"
#include "config.h"
#include "network_manager.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <vector>

#ifdef RAVLIGHT_MASTER
#include "webserver_manager.h"
#endif

#define TAG "DISC"

std::vector<DeviceInfo> ScannedDevices;

static bool     s_running             = false;
static uint32_t s_startMs             = 0;
static int      s_wave                = 0;
static bool     s_espnowEnabled       = false;
static bool     s_wifiReconnectNeeded = false;

static inline uint32_t nowMs() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void clearDevices() {
    ScannedDevices.clear();
    resetUDPDiscoveryListener();
}

bool isDiscoveryRunning() {
    return s_running;
}

const std::vector<DeviceInfo>& getDiscoveredUDPDevices() {
    return ScannedDevices;
}

void startCombinedDiscovery(bool withESPNow) {
    if (s_running) return;
    clearDevices();
    s_espnowEnabled       = withESPNow;
    s_wifiReconnectNeeded = false;

    bool wifiSTA = (strcmp(getConnectionMode(), "WiFi") == 0);

    if (withESPNow && wifiSTA) {
        // WiFi STA + ESP-NOW: skip UDP (different subnet — won't reach AP-mode devices).
        // Actual disconnect + ESP-NOW broadcast deferred 200 ms via Ticker in webserver so
        // the HTTP response can fly before WiFi drops. triggerESPNowScanStart() sets the
        // timer and advances s_wave once the Ticker fires.
        s_wifiReconnectNeeded = true;
        s_wave    = 0;   // 0 = waiting for Ticker to fire
        s_running = true;
        ESP_LOGI(TAG, "Scan pending — WiFi will be suspended for ESP-NOW");
    } else {
        // ETH, AP, or UDP-only: start immediately
        updateUDPDiscovery();
        startUDPDiscovery();
        if (withESPNow) startESPNowDiscovery();
        s_wave    = 1;
        s_running = true;
        s_startMs = nowMs();
        ESP_LOGI(TAG, "Scan started — wave 1/3 (UDP%s)", withESPNow ? " + ESP-NOW" : "");
    }
}

// Called by the Ticker in webserver_manager.cpp, 200 ms after the HTTP response,
// once WiFi is already disconnected and ESP-NOW broadcast has been sent.
void triggerESPNowScanStart() {
    s_startMs = nowMs();
    s_wave    = 1;
    ESP_LOGI(TAG, "Scan started — wave 1/3 (ESP-NOW, WiFi suspended)");
}

// Call from loop() — sends remaining broadcast waves and closes the scan window.
void updateCombinedDiscovery() {
    if (!s_running || s_wave == 0) return;  // s_wave 0 = waiting for Ticker

    uint32_t elapsed = nowMs() - s_startMs;

    if (s_wave == 1 && elapsed >= DISC_WAVE_INTERVAL_MS) {
        if (!s_wifiReconnectNeeded) startUDPDiscovery();
        if (s_espnowEnabled)        startESPNowDiscovery();
        s_wave = 2;
        ESP_LOGI(TAG, "Scan wave 2/3 (%s)",
                 s_wifiReconnectNeeded ? "ESP-NOW" : s_espnowEnabled ? "UDP + ESP-NOW" : "UDP");
    }
    if (s_wave == 2 && elapsed >= DISC_WAVE_INTERVAL_MS * 2) {
        if (!s_wifiReconnectNeeded) startUDPDiscovery();
        s_wave = 3;
        ESP_LOGI(TAG, "Scan wave 3/3 (UDP)");
    }
    if (elapsed >= DISC_SCAN_TOTAL_MS) {
        s_running = false;
        s_wave    = 0;
        if (s_wifiReconnectNeeded) {
            s_wifiReconnectNeeded = false;
            resumeWiFiSTA();
        }
        ESP_LOGI(TAG, "Scan complete — %d device(s)", (int)ScannedDevices.size());
        printDiscoveredDevices();
    }
}

void printDiscoveredDevices() {
    ESP_LOGI(TAG, "Found %d device(s):", (int)ScannedDevices.size());
    for (const auto& d : ScannedDevices) {
        ESP_LOGI(TAG, "  %s @ %s [%s] fw:%s temp:%.1f up:%lum",
                 d.id.c_str(), d.ip.c_str(), d.mode.c_str(),
                 d.fw.c_str(), d.temp, (unsigned long)d.uptime);
    }
}

#endif // RAVLIGHT_MASTER || RAVLIGHT_MODULE_DISCOVERY
