#ifdef RAVLIGHT_MASTER
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "discovery_shared.h"
#include "discovery_espnow.h"
#include "discovery_udp.h"
#include "config.h"
#include "network_manager.h"
#include <vector>
#include "webserver_manager.h"

std::vector<DeviceInfo> ScannedDevices;

bool isScanRunning = false;
bool wasWiFi = false;
unsigned long ScanStartTime = 0;
const unsigned long ScanDuration = 4000;
unsigned long lastAutoScanTime = 0;
const unsigned long autoScanInterval = 10000;

void startCombinedDiscovery() {
  if (isScanRunning) return;

  isScanRunning = true;
  ScanStartTime = millis();

  Serial.println("[DISCOVERY] Starting ESP-NOW discovery");
  if (strcmp(getConnectionMode(), "WiFi") == 0) {
    wasWiFi = true;
  }
  clearDevices();
  //startUDPDiscovery();
  //stopWebServer();
  startESPNowDiscoveryAuto();
}

// Call from loop() to check scan timeout and restore connectivity
void updateCombinedDiscovery() {
  if (!isScanRunning) return;

  if (millis() - ScanStartTime > ScanDuration) {
    if (wasWiFi) {
      Serial.println("[DISCOVERY] ESP-NOW timeout — restoring WiFi STA");
      initWiFi(netConfig.wifiSSID.c_str(), netConfig.wifiPassword.c_str());
      wasWiFi = false;
      startWebServer();
    } else {
      Serial.println("[DISCOVERY] ESP-NOW scan timeout");
    }
    isScanRunning = false;
  }
}

void autoDiscoveryLoop() {
  if (isScanRunning) return;
  const char* mode = getConnectionMode();
  if (strcmp(mode, "ETH") == 0 || strcmp(mode, "WiFi") == 0) {
    if (millis() - lastAutoScanTime > autoScanInterval) {
      lastAutoScanTime = millis();
      startUDPDiscovery();
    }
  }
}

void clearDevices() {
  ScannedDevices.clear();
}

const std::vector<DeviceInfo>& getDiscoveredUDPDevices() {
  printDiscoveredDevices();
  return ScannedDevices;
}

void printDiscoveredDevices() {
  Serial.printf("[DISCOVERY] Total devices found: %d\n", ScannedDevices.size());
  for (const auto& d : ScannedDevices) {
    Serial.println("------");
    Serial.printf("  ID: %s\n",     d.id.c_str());
    Serial.printf("  MAC: %s\n",    d.mac.c_str());
    Serial.printf("  IP: %s\n",     d.ip.c_str());
    Serial.printf("  Mode: %s\n",   d.mode.c_str());
    Serial.printf("  FW: %s\n",     d.fw.c_str());
    Serial.printf("  Temp: %.1f °C\n", d.temp);
    Serial.printf("  Uptime: %lu min\n", d.uptime);
  }
}

#endif // RAVLIGHT_MASTER
