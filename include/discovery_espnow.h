#ifndef DISCOVERY_ESPNOW_H
#define DISCOVERY_ESPNOW_H

#include <Arduino.h>
#include <WiFi.h>

#define DISC_ESPNOW_RESP_DELAY_MIN   10     // ms — min random delay before response (anti-collision)
#define DISC_ESPNOW_RESP_DELAY_MAX   100    // ms — max random delay before response
#define DISC_ESPNOW_JSON_DOC_SIZE    256    // doc size for discovery response payload
#define DISC_ESPNOW_CMD_DOC_SIZE     192    // doc size for command/R_DISCOVER payload (CONNECT needs ssid+pwd)

void initESPNow();

// True once initESPNow() has actually brought the transport up — i.e.
// netConfig.espnowEnabled was on at boot (or SoftAP mode started it). Lets
// callers tell "ESP-NOW was requested for this scan" apart from "ESP-NOW is
// actually running" — startESPNowDiscovery() silently no-ops when this is
// false, which otherwise reads as a scan log claiming ESP-NOW ran when it didn't.
#if !defined(RAVLIGHT_MASTER) && defined(RAVLIGHT_MODULE_ESPNOW)
bool isESPNowReady();
#endif

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
