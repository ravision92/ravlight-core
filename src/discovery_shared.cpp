#if defined(RAVLIGHT_MASTER) || defined(RAVLIGHT_MODULE_DISCOVERY)
#include "discovery_shared.h"
#include "discovery_udp.h"
#include "config.h"
#include "network_manager.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <vector>

#ifdef RAVLIGHT_MASTER
#include "discovery_espnow.h"
#include "webserver_manager.h"
#endif

#define TAG "DISC"

std::vector<DeviceInfo> ScannedDevices;

static bool     s_running      = false;
static uint32_t s_startMs      = 0;
static int      s_wave         = 0;   // broadcast wave counter (1–3)

static const uint32_t SCAN_TOTAL_MS  = 4500;
static const uint32_t WAVE_INTERVAL  = 1500;

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

void startCombinedDiscovery() {
    if (s_running) return;
    clearDevices();
    updateUDPDiscovery();   // arm listener on port 4211 before first broadcast
    startUDPDiscovery();    // wave 1
    s_wave    = 1;
    s_running = true;
    s_startMs = nowMs();
    ESP_LOGI(TAG, "Scan started — wave 1/3");
}

// Call from loop() — sends remaining broadcast waves and closes the scan window.
void updateCombinedDiscovery() {
    if (!s_running) return;
    uint32_t elapsed = nowMs() - s_startMs;

    if (s_wave == 1 && elapsed >= WAVE_INTERVAL) {
        startUDPDiscovery();
        s_wave = 2;
        ESP_LOGI(TAG, "Scan wave 2/3");
    }
    if (s_wave == 2 && elapsed >= WAVE_INTERVAL * 2) {
        startUDPDiscovery();
        s_wave = 3;
        ESP_LOGI(TAG, "Scan wave 3/3");
    }
    if (elapsed >= SCAN_TOTAL_MS) {
        s_running = false;
        s_wave    = 0;
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
