#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#define AP_CHANNEL 12

void initEthernet();
void checkNetwork();
void initWiFi(const char* ssid, const char* password);
void initWifiAP();
void setMDNSHost(const String& fixtureID);
void scanWiFiNetworks();
void printScannedNetworks(uint16_t networksFound);
const char* getConnectionMode();
void suspendWiFiSTA();   // disconnect from AP, keep WIFI_STA mode (for ESP-NOW scan)
void resumeWiFiSTA();    // reconnect to saved AP after ESP-NOW scan

#ifdef RAVLIGHT_MODULE_ETHERNET
extern bool ethConnected;
#endif

extern bool WifiAPMode;

#endif
