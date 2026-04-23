#ifndef DISCOVERY_SHARED_H
#define DISCOVERY_SHARED_H

#include <Arduino.h>
#include <vector>

struct DeviceInfo {
  String id;
  String mode;
  String ip;
  String mac;
  String fw;
  float temp;
  uint32_t uptime;
  unsigned long lastSeen;  // timestamp for timeout detection
};


extern  std::vector<DeviceInfo> ScannedDevices;

void clearDevices();
const std::vector<DeviceInfo>& getDiscoveredUDPDevices();
void startCombinedDiscovery();
void updateCombinedDiscovery();
void printDiscoveredDevices();
void autoDiscoveryLoop();
#endif // DISCOVERY_SHARED_H