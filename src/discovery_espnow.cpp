// Slave ESP-NOW discovery — compiled for all fixture environments (not Master)
#ifndef RAVLIGHT_MASTER
#include "discovery_udp.h"
#include "discovery_espnow.h"
#include <esp_now.h>
#include <WiFi.h>
#include <Arduino.h>
#include "config.h"
#include "network_manager.h"
#include <runtimeNVS.h>
#include <ArduinoJson.h>
#include "settings.h"

#ifdef RAVLIGHT_MODULE_TEMP
#include "temp_sensor.h"
#endif
#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "dmx_fixture.h"
#endif

uint8_t masterMac[6];

void sendESPNowDiscoveryResponse() {
  delay(random(10, 100));

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMac, 6);
  peerInfo.channel = AP_CHANNEL;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  DynamicJsonDocument doc(256);
  doc["id"]     = setConfig.ID_fixture;
  doc["mode"]   = getConnectionMode();
  doc["ip"]     = netConfig.currentip;
  doc["mac"]    = getSerialNumber();
  doc["fw"]     = FW_VERSION;
#ifdef RAVLIGHT_MODULE_TEMP
  doc["temp"]   = readTemperature();
#else
  doc["temp"]   = 0.0;
#endif
  doc["uptime"] = currentRuntime;

  String payload;
  serializeJson(doc, payload);
  esp_now_send(masterMac, (uint8_t*)payload.c_str(), payload.length());
  Serial.printf("[ESP-NOW] Discovery response sent to %02X:%02X:%02X:%02X:%02X:%02X\n",
                masterMac[0], masterMac[1], masterMac[2],
                masterMac[3], masterMac[4], masterMac[5]);
}

void handleESPNowCommand(const uint8_t *mac, const uint8_t *incomingData, int len) {
  Serial.printf("[ESP-NOW] Command received from %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  memcpy(masterMac, mac, 6);

  String jsonStr = String((char*)incomingData);
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) {
    Serial.println("[ESP-NOW] JSON parse failed");
    return;
  }

  String cmd = doc["cmd"] | "";
  if (cmd == "R_DISCOVER") {
    sendESPNowDiscoveryResponse();
  } else if (cmd == "RESET") {
    Serial.println("[ESP-NOW] Executing RESET");
    delay(200);
    loadDefaultConfig();
    ESP.restart();
  } else if (cmd == "APMODE") {
    Serial.println("[ESP-NOW] Switching to AP mode");
    initWifiAP();
  } else if (cmd == "CONNECT") {
    String ssid = doc["ssid"] | "";
    String pwd  = doc["pwd"]  | "";
    if (!ssid.isEmpty()) {
      Serial.printf("[ESP-NOW] Connecting to SSID: %s\n", ssid.c_str());
      netConfig.wifiSSID     = ssid;
      netConfig.wifiPassword = pwd;
      netConfig.dhcp = true;
      saveConfig();
      delay(300);
      ESP.restart();
    }
  } else if (cmd == "HIGHLIGHT") {
    Serial.println("[ESP-NOW] Highlight command received");
#ifdef RAVLIGHT_FIXTURE_VEYRON
    startHighlight();
#endif
  }
}

void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init failed");
    return;
  }
  esp_now_register_recv_cb(handleESPNowCommand);
  Serial.println("[ESP-NOW] Slave ready");
}
#endif // !RAVLIGHT_MASTER

#ifdef RAVLIGHT_MASTER
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "discovery_shared.h"
#include "discovery_espnow.h"
#include "discovery_udp.h"
#include "config.h"
#include "network_manager.h"
#include <esp_wifi.h>
#include <vector>
#include "webserver_manager.h"

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool s_espnowReady = false;

void onESPNowRecv(const uint8_t *macAddr, const uint8_t *incomingData, int len) {
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          macAddr[0], macAddr[1], macAddr[2],
          macAddr[3], macAddr[4], macAddr[5]);

  Serial.printf("[ESP-NOW] Packet received from %s\n", macStr);

  if (!esp_now_is_peer_exist(macAddr)) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macAddr, 6);
    peerInfo.channel = AP_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ESP-NOW] Failed to add dynamic peer");
    } else {
      Serial.printf("[ESP-NOW] Peer added: %s\n", macStr);
    }
  }

  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, incomingData, len);
  if (error) {
    Serial.println("[ESP-NOW] JSON parse failed");
    return;
  }

  Serial.println("[DISCOVERY] Device received:");
  Serial.printf("  ID: %s\n",      doc["id"]   | "n/a");
  Serial.printf("  MAC: %s\n",     doc["mac"]  | "n/a");
  Serial.printf("  IP: %s\n",      doc["ip"]   | "n/a");
  Serial.printf("  Mode: %s\n",    doc["mode"] | "n/a");
  Serial.printf("  FW: %s\n",      doc["fw"]   | "n/a");
  Serial.printf("  Temp: %.2f C\n", doc["temp"] | 0.0);
  Serial.printf("  Uptime: %lu min\n", doc["uptime"] | 0);

  String mac = doc["mac"] | "";
  for (const auto& d : ScannedDevices) {
    if (d.mac == mac) return;
  }

  DeviceInfo info;
  info.id     = doc["id"]     | "n/a";
  info.mac    = mac;
  info.ip     = doc["ip"]     | "n/a";
  info.mode   = doc["mode"]   | "n/a";
  info.fw     = doc["fw"]     | "n/a";
  info.temp   = doc["temp"]   | 0.0;
  info.uptime = doc["uptime"] | 0;
  ScannedDevices.push_back(info);
  Serial.println("[ESP-NOW] Device added to list");
}

void startESPNowDiscovery() {
  DynamicJsonDocument doc(256);
  doc["cmd"] = "R_DISCOVER";
  String message;
  serializeJson(doc, message);
  esp_now_send(broadcastAddress, (uint8_t*)message.c_str(), message.length());
  Serial.println("[ESPNOW] Discovery request broadcasted");
}

static void ensureEspNowChannel() {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(AP_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void startESPNowDiscoveryAuto() {
  const char* mode = getConnectionMode();
  if (strcmp(mode, "WiFi") == 0) {
    Serial.println("[ESPNOW] WiFi STA active: temporary swap for discovery");
    stopWebServer();
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    ensureEspNowChannel();
    if (!s_espnowReady) initESPNow();
    startESPNowDiscovery();
  } else {
    ensureEspNowChannel();
    if (!s_espnowReady) initESPNow();
    startESPNowDiscovery();
  }
}

void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] Init failed");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = AP_CHANNEL;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(broadcastAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ESPNOW] Failed to add broadcast peer");
    }
  }
  esp_now_register_recv_cb(onESPNowRecv);
  Serial.println("[ESPNOW] Discovery ready");
  s_espnowReady = true;
}

bool isESPNowReady() {
  return s_espnowReady;
}

bool sendESPNowCommandAuto(const uint8_t* targetMac, const String& commandType, const String& ssid, const String& password) {
  const char* mode = getConnectionMode();

  auto doSend = [&]() -> bool {
    ensureEspNowChannel();
    if (!s_espnowReady) initESPNow();
    return sendESPNowCommand(targetMac, commandType, ssid, password);
  };

  if (strcmp(mode, "WiFi") == 0) {
    Serial.println("[ESPNOW] WiFi STA active: temporary swap for command");
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    bool ok = doSend();
    initWiFi(netConfig.wifiSSID.c_str(), netConfig.wifiPassword.c_str());
    return ok;
  }
  return doSend();
}

bool sendESPNowCommand(const uint8_t* targetMac, const String& commandType, const String& ssid, const String& password) {
  DynamicJsonDocument doc(256);
  doc["cmd"] = commandType;
  if (commandType == "CONNECT") {
    doc["ssid"] = ssid;
    doc["pwd"]  = password;
  }
  String payload;
  serializeJson(doc, payload);

  if (!esp_now_is_peer_exist(targetMac)) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, targetMac, 6);
    peerInfo.channel = AP_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ESP-NOW] Failed to add peer for command");
      return false;
    }
  }

  esp_err_t result = esp_now_send(targetMac, (uint8_t*)payload.c_str(), payload.length());

  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          targetMac[0], targetMac[1], targetMac[2],
          targetMac[3], targetMac[4], targetMac[5]);

  const char* mode = getConnectionMode();
  if (strcmp(mode, "WiFi") == 0) {
    initWiFi(netConfig.wifiSSID.c_str(), netConfig.wifiPassword.c_str());
  }

  if (result == ESP_OK) {
    Serial.printf("[ESP-NOW] Command '%s' sent to %s\n", commandType.c_str(), macStr);
    return true;
  }
  Serial.printf("[ESP-NOW] Command send failed to %s\n", macStr);
  return false;
}

#endif // RAVLIGHT_MASTER
