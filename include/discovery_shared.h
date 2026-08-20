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
    String mac;     // hardware MAC "AA:BB:CC:DD:EE:FF" from the reply payload
                    // (getSerialNumber() = efuse base MAC) — used for dedup.
                    // NOT the "RVA1B2" short id, which lives in .id and is
                    // user-editable, hence unusable as an identity key.
    String hwMac;   // same hardware MAC, but learned from the ESP-NOW peer
                    // address instead of the payload — non-empty only for
                    // devices discovered over ESP-NOW
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
