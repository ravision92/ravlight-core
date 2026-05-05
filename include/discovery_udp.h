#ifndef DISCOVERY_UDP_H
#define DISCOVERY_UDP_H

#include <Arduino.h>

#define DISC_UDP_DISCOVER_PORT   4210        // slave listens for R_DISCOVER broadcasts
#define DISC_UDP_RESPONSE_PORT   4211        // scanner listens for JSON responses
#define DISC_UDP_COMMAND_PORT    4212        // slave listens for command JSON
#define DISC_UDP_DISCOVER_MSG    "R_DISCOVER"
#define DISC_UDP_JSON_DOC_SIZE   256

// Slave discovery — available for all fixture environments (not Master)
#ifndef RAVLIGHT_MASTER
void initUDP();
void setupDiscoveryUDP();
void setupUDPCommandReceiver();
#endif

#if defined(RAVLIGHT_MASTER) || defined(RAVLIGHT_MODULE_DISCOVERY)
#include <vector>
#include <AsyncUDP.h>
#include "discovery_shared.h"

void startUDPDiscovery();
void updateUDPDiscovery();
void resetUDPDiscoveryListener();
bool sendUDPCommand(const IPAddress& targetIP, const String& commandType, const String& ssid = "", const String& password = "");
#endif

#endif // DISCOVERY_UDP_H
