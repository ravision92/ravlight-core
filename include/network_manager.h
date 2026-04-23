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

#ifdef RAVLIGHT_MODULE_ETHERNET
extern bool ethConnected;
#endif

extern bool WifiAPMode;

#endif
