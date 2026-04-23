#ifndef DISCOVERY_UDP_H
#define DISCOVERY_UDP_H

#include <Arduino.h>

// Slave discovery — available for all fixture environments (not Master)
#ifndef RAVLIGHT_MASTER
void initUDP();
void setupDiscoveryUDP();
void setupUDPCommandReceiver();
#endif

#ifdef RAVLIGHT_MASTER
#include <vector>
#include <AsyncUDP.h>
#include "discovery_shared.h"

void startUDPDiscovery();
void updateUDPDiscovery();
bool sendUDPCommand(const IPAddress& targetIP, const String& commandType, const String& ssid = "", const String& password = "");
#endif

#endif // DISCOVERY_UDP_H
