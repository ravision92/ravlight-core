#ifndef DISCOVERY_ESPNOW_H
#define DISCOVERY_ESPNOW_H

#include <Arduino.h>
#include <WiFi.h>

// Shared — initESPNow is called by both slave and master sections
void initESPNow();

#ifdef RAVLIGHT_MASTER
bool isESPNowReady();
void startESPNowDiscovery();
void startESPNowDiscoveryAuto();
bool sendESPNowCommand(const String& macStr, const String& command, const String& ssid = "", const String& password = "");
bool sendESPNowCommand(const uint8_t* targetMac, const String& commandType, const String& ssid = "", const String& password = "");
bool sendESPNowCommandAuto(const uint8_t* targetMac, const String& commandType, const String& ssid, const String& password);
#endif

#endif // DISCOVERY_ESPNOW_H
