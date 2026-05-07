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

AsyncUDP udp;
AsyncUDP commandUDP;

void initUDP() {
  setupDiscoveryUDP();
  setupUDPCommandReceiver();
}

void setupDiscoveryUDP() {
  if (udp.listen(DISC_UDP_DISCOVER_PORT)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      String msg = String((char*)packet.data(), packet.length());
      Serial.printf("[UDP] Received %d bytes from %s\n", (int)packet.length(), packet.remoteIP().toString().c_str());
      if (msg == DISC_UDP_DISCOVER_MSG) {
        Serial.printf("[UDP] R_DISCOVER → replying to %s:%d\n", packet.remoteIP().toString().c_str(), DISC_UDP_RESPONSE_PORT);
        DynamicJsonDocument doc(DISC_UDP_JSON_DOC_SIZE);
        doc["id"]     = setConfig.ID_fixture;
        doc["mode"]   = getConnectionMode();
        doc["ip"]     = netConfig.currentip;
        doc["mac"]    = getSerialNumber();
        doc["fw"]     = FW_VERSION;
        doc["fixture"] = PROJECT_NAME;
#ifdef RAVLIGHT_MODULE_TEMP
        doc["temp"]   = readTemperature();
#else
        doc["temp"]   = 0.0;
#endif
        doc["uptime"] = currentRuntime;
        String response;
        serializeJson(doc, response);
        udp.writeTo((const uint8_t*)response.c_str(), response.length(), packet.remoteIP(), DISC_UDP_RESPONSE_PORT);
      }
    });
    Serial.printf("[UDP] Discovery listening on port %d\n", DISC_UDP_DISCOVER_PORT);
  } else {
    Serial.println("[UDP] Failed to start discovery UDP");
  }
}

void setupUDPCommandReceiver() {
  if (commandUDP.listen(DISC_UDP_COMMAND_PORT)) {
    commandUDP.onPacket([](AsyncUDPPacket packet) {
      String msg = String((char*)packet.data(), packet.length());
      StaticJsonDocument<DISC_UDP_JSON_DOC_SIZE> doc;
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
        fixtureHighlight();
      }
    });
    Serial.printf("[UDP] Command receiver listening on port %d\n", DISC_UDP_COMMAND_PORT);
  } else {
    Serial.println("[UDP] Failed to start command receiver");
  }
}

#endif // !RAVLIGHT_MASTER

#if defined(RAVLIGHT_MASTER) || defined(RAVLIGHT_MODULE_DISCOVERY)
#include <AsyncUDP.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "config.h"
#include "network_manager.h"
#include "discovery_shared.h"
#include "discovery_udp.h"
#include <vector>

// Separate variable names from the slave-section AsyncUDP objects to avoid
// symbol collision when both sections compile (MODULE_DISCOVERY + !MASTER).
// s_rxUDP is ONLY used for listen(4211) + onPacket — never for sending,
// so the lwIP PCB state is never changed by broadcastTo/writeTo and unicast
// replies from any device are always accepted.
static AsyncUDP s_rxUDP;    // listens on DISC_UDP_RESPONSE_PORT — collects discovery responses (RX only)
static AsyncUDP s_txUDP;    // sends R_DISCOVER broadcasts (TX only)
static AsyncUDP s_cmdUDP;   // sends commands to DISC_UDP_COMMAND_PORT

static bool s_rxArmed = false;

void resetUDPDiscoveryListener() {
    s_rxArmed = false;
}

void updateUDPDiscovery() {
    if (s_rxArmed) return;  // already listening, don't re-arm on every wave
    s_rxArmed = s_rxUDP.listen(DISC_UDP_RESPONSE_PORT);
    if (!s_rxArmed) {
        ESP_LOGW("UDP", "Failed to listen on port %d", DISC_UDP_RESPONSE_PORT);
        return;
    }
    s_rxUDP.onPacket([](AsyncUDPPacket packet) {
        DynamicJsonDocument doc(DISC_UDP_JSON_DOC_SIZE);
        if (deserializeJson(doc, packet.data(), packet.length())) return;
        String mac = doc["mac"] | "";
        if (mac.isEmpty()) return;
        if (mac == getSerialNumber()) return;  // skip self
        for (const auto& d : ScannedDevices) {
            if (d.mac == mac) return;   // duplicate
        }
        DeviceInfo info;
        info.id       = doc["id"]      | "n/a";
        info.ip       = doc["ip"]      | "n/a";
        info.mac      = mac;
        info.mode     = doc["mode"]    | "n/a";
        info.fixture  = doc["fixture"] | "";
        info.fw       = doc["fw"]      | "n/a";
        info.temp     = doc["temp"]   | 0.0f;
        info.uptime   = doc["uptime"] | (uint32_t)0;
        info.lastSeen = (uint32_t)(esp_timer_get_time() / 1000ULL);
        ScannedDevices.push_back(info);
        ESP_LOGI("UDP", "Device added: %s @ %s", info.id.c_str(), info.ip.c_str());
    });
    ESP_LOGI("UDP", "Listening for responses on port %d", DISC_UDP_RESPONSE_PORT);
}

void startUDPDiscovery() {
    s_txUDP.listen(0);  // bind to ephemeral port so the PCB is valid before sending
    size_t sent = s_txUDP.broadcastTo(DISC_UDP_DISCOVER_MSG, DISC_UDP_DISCOVER_PORT);
    ESP_LOGI("UDP", "Discovery broadcast sent (%d bytes)", (int)sent);
}

bool sendUDPCommand(const IPAddress& targetIP, const String& commandType,
                    const String& ssid, const String& password) {
    if (!s_cmdUDP.listen(0)) return false;
    DynamicJsonDocument doc(DISC_UDP_JSON_DOC_SIZE);
    doc["cmd"] = commandType;
    if (commandType == "CONNECT") { doc["ssid"] = ssid; doc["pwd"] = password; }
    String payload;
    serializeJson(doc, payload);
    bool ok = s_cmdUDP.writeTo((const uint8_t*)payload.c_str(), payload.length(), targetIP, DISC_UDP_COMMAND_PORT);
    ESP_LOGI("UDP", "Command '%s' to %s: %s", commandType.c_str(),
             targetIP.toString().c_str(), ok ? "sent" : "failed");
    return ok;
}
#endif // RAVLIGHT_MASTER || RAVLIGHT_MODULE_DISCOVERY
