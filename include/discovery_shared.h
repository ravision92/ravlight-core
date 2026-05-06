#pragma once
#include <Arduino.h>
#include <vector>

#if defined(RAVLIGHT_MASTER) || defined(RAVLIGHT_MODULE_DISCOVERY)

#define DISC_SCAN_TOTAL_MS    4500   // total scan window
#define DISC_WAVE_INTERVAL_MS 1500   // interval between UDP broadcast waves (3 waves total)

struct DeviceInfo {
    String id;
    String mode;
    String ip;
    String mac;     // serial number (e.g. "RVA1B2") — used for dedup
    String hwMac;   // hardware MAC "AA:BB:CC:DD:EE:FF" — non-empty only if discovered via ESP-NOW
    String fixture;
    String fw;
    float    temp;
    uint32_t uptime;
    uint32_t lastSeen;
};

extern std::vector<DeviceInfo> ScannedDevices;

void clearDevices();
void startCombinedDiscovery(bool withESPNow = false);
void triggerESPNowScanStart();   // called by Ticker 200ms after HTTP response (WiFi STA case)
void updateCombinedDiscovery();
bool isDiscoveryRunning();
const std::vector<DeviceInfo>& getDiscoveredUDPDevices();
void printDiscoveredDevices();

#endif // RAVLIGHT_MASTER || RAVLIGHT_MODULE_DISCOVERY
