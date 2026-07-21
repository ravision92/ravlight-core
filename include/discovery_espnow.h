#ifndef DISCOVERY_ESPNOW_H
#define DISCOVERY_ESPNOW_H

#include <Arduino.h>
#include <WiFi.h>

#define DISC_ESPNOW_RESP_DELAY_MIN   10     // ms — min random delay before response (anti-collision)
#define DISC_ESPNOW_RESP_DELAY_MAX   100    // ms — max random delay before response
#define DISC_ESPNOW_JSON_DOC_SIZE    256    // doc size for discovery response payload
#define DISC_ESPNOW_CMD_DOC_SIZE     192    // doc size for command/R_DISCOVER payload (CONNECT needs ssid+pwd)

void initESPNow();

// startESPNowDiscovery/sendESPNowCommand need both modules: RAVLIGHT_MODULE_ESPNOW
// (the transport is compiled in) and RAVLIGHT_MODULE_DISCOVERY (the scanner/
// Devices-panel logic that calls them) — see discovery_espnow.cpp's matching guard.
#if defined(RAVLIGHT_MODULE_DISCOVERY) && defined(RAVLIGHT_MODULE_ESPNOW)
void startESPNowDiscovery();
bool sendESPNowCommand(const String& hwMacStr, const String& command, const String& ssid = "", const String& password = "");
#endif

#ifdef RAVLIGHT_MASTER
bool isESPNowReady();
void startESPNowDiscovery();
void startESPNowDiscoveryAuto();
bool sendESPNowCommand(const String& macStr, const String& command, const String& ssid = "", const String& password = "");
bool sendESPNowCommand(const uint8_t* targetMac, const String& commandType, const String& ssid = "", const String& password = "");
bool sendESPNowCommandAuto(const uint8_t* targetMac, const String& commandType, const String& ssid, const String& password);
#endif

#endif // DISCOVERY_ESPNOW_H
