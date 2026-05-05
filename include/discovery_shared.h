#pragma once
#include <Arduino.h>
#include <vector>

#if defined(RAVLIGHT_MASTER) || defined(RAVLIGHT_MODULE_DISCOVERY)

struct DeviceInfo {
    String id;
    String mode;
    String ip;
    String mac;
    String fw;
    float    temp;
    uint32_t uptime;
    uint32_t lastSeen;
};

extern std::vector<DeviceInfo> ScannedDevices;

void clearDevices();
void startCombinedDiscovery();
void updateCombinedDiscovery();
bool isDiscoveryRunning();
const std::vector<DeviceInfo>& getDiscoveredUDPDevices();
void printDiscoveredDevices();

#endif // RAVLIGHT_MASTER || RAVLIGHT_MODULE_DISCOVERY
