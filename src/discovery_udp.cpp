// Slave discovery — compiled for all fixture environments (not Master)
#ifndef RAVLIGHT_MASTER
#include "discovery_udp.h"
#include "config.h"
#include "network_manager.h"
#include "runtime.h"
#include <AsyncUDP.h>
#include <ArduinoJson.h>

#ifdef RAVLIGHT_MODULE_TEMP
#include "temp_sensor.h"
#endif
#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "fixtures/veyron/dmx_fixture.h"
#endif

AsyncUDP udp;
AsyncUDP commandUDP;

void initUDP() {
  setupDiscoveryUDP();
  setupUDPCommandReceiver();
}

void setupDiscoveryUDP() {
  if (udp.listen(4210)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      String msg = (char *)packet.data();
      if (msg == "R_DISCOVER") {
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
        String response;
        serializeJson(doc, response);
        udp.writeTo((const uint8_t*)response.c_str(), response.length(), packet.remoteIP(), 4211);
      }
    });
    Serial.println("[UDP] Discovery listening on port 4210");
  } else {
    Serial.println("[UDP] Failed to start discovery UDP");
  }
}

void setupUDPCommandReceiver() {
  if (commandUDP.listen(4212)) {
    commandUDP.onPacket([](AsyncUDPPacket packet) {
      String msg = (char *)packet.data();
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, msg);
      if (error) {
        Serial.println("[UDP] Command JSON parse error");
        return;
      }

      String cmd = doc["cmd"] | "";
      Serial.printf("[UDP] Command received: %s\n", cmd.c_str());

      if (cmd == "RESET") {
        Serial.println("[UDP] Executing RESET");
        delay(200);
        loadDefaultConfig();
        ESP.restart();
      } else if (cmd == "APMODE") {
        Serial.println("[UDP] Starting AP mode");
        initWifiAP();
      } else if (cmd == "CONNECT") {
        String ssid = doc["ssid"] | "";
        String pwd  = doc["pwd"]  | "";
        if (!ssid.isEmpty()) {
          Serial.printf("[UDP] Connecting to SSID: %s\n", ssid.c_str());
          netConfig.wifiSSID     = ssid;
          netConfig.wifiPassword = pwd;
          netConfig.dhcp = true;
          saveConfig();
          delay(300);
          ESP.restart();
        }
      } else if (cmd == "HIGHLIGHT") {
        Serial.println("[UDP] Highlight command received");
#ifdef RAVLIGHT_FIXTURE_VEYRON
        startHighlight();
#endif
      }
    });
    Serial.println("[UDP] Command receiver listening on port 4212");
  } else {
    Serial.println("[UDP] Failed to start command receiver");
  }
}

#endif // !RAVLIGHT_MASTER

#ifdef RAVLIGHT_MASTER
#include <AsyncUDP.h>
#include <ArduinoJson.h>
#include "config.h"
#include "network_manager.h"
#include "discovery_shared.h"
#include "discovery_udp.h"
#include <vector>

static AsyncUDP udp;
static AsyncUDP commandUDP;

void updateUDPDiscovery() {
  if (udp.listen(4211)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, packet.data());
      if (!error) {
        String mac = doc["mac"] | "";
        bool duplicate = false;
        for (const auto& d : ScannedDevices) {
          if (d.mac == mac) { duplicate = true; break; }
        }
        if (!duplicate) {
          DeviceInfo info;
          info.id     = doc["id"]     | "n/a";
          info.ip     = doc["ip"]     | "n/a";
          info.mac    = mac;
          info.mode   = doc["mode"]   | "n/a";
          info.fw     = doc["fw"]     | "n/a";
          info.temp   = doc["temp"]   | 0.0;
          info.uptime = doc["uptime"] | 0;
          ScannedDevices.push_back(info);
        }
      }
    });
  }
}

void startUDPDiscovery() {
  udp.broadcastTo("R_DISCOVER", 4210);
  Serial.println("[UDP] Discovery broadcast sent");
}

bool sendUDPCommand(const IPAddress& targetIP, const String& commandType, const String& ssid, const String& password) {
  if (!commandUDP.listen(0)) return false;
  DynamicJsonDocument doc(256);
  doc["cmd"] = commandType;
  if (commandType == "CONNECT") { doc["ssid"] = ssid; doc["pwd"] = password; }
  String payload;
  serializeJson(doc, payload);
  return commandUDP.writeTo((const uint8_t*)payload.c_str(), payload.length(), targetIP, 4212);
}
#endif // RAVLIGHT_MASTER
