#ifdef RAVLIGHT_MODULE_ETHERNET
  #ifndef RAVLIGHT_HAS_ETHERNET
    #error "RAVLIGHT_MODULE_ETHERNET requires a board with RAVLIGHT_HAS_ETHERNET (define it in the board file)"
  #endif
#include <ETH.h>
#endif

#include <WiFi.h>
#include "webserver_manager.h"
#include "network_manager.h"
#include <config.h>
#include <ESPmDNS.h>
#include "runtime.h"
#include "discovery_espnow.h"
#include "dmx_manager.h"
#include <esp_wifi.h>
#include <ESP32Ping.h>

bool WifiAPMode = false;

#ifdef RAVLIGHT_MODULE_ETHERNET
bool ethConnected = false;
#endif

int16_t WiFiScanStatus = 0;
String ScannedSSID;

#ifdef RAVLIGHT_MODULE_ETHERNET
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Ethernet started");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("Ethernet connected");
      ethConnected = true;
      WifiAPMode = false;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("Ethernet IP: ");
      netConfig.currentip = ETH.localIP().toString();
      Serial.println(netConfig.currentip);
      setMDNSHost(setConfig.ID_fixture);
      reinitDMXInput();   // (re)start ArtNet/sACN now that ETH has an IP
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet disconnected");
      initWiFi(netConfig.wifiSSID.c_str(), netConfig.wifiPassword.c_str());
      ethConnected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("Ethernet stopped");
      ethConnected = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("WiFi STA started");
      WifiAPMode = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("WiFi STA connected");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("WiFi IP: ");
      netConfig.currentip = WiFi.localIP().toString();
      Serial.println(netConfig.currentip);
      setMDNSHost(setConfig.ID_fixture);
      WifiAPMode = false;
      reinitDMXInput();   // (re)start ArtNet/sACN now that WiFi STA has an IP
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi STA disconnected");
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      Serial.println("WiFi STA stopped");
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("WiFi AP started");
      reinitDMXInput();   // (re)start ArtNet/sACN on AP interface; no-op if called before setup
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.println("WiFi AP client connected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("WiFi AP client disconnected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("WiFi AP stopped");
      break;
    case ARDUINO_EVENT_SC_SCAN_DONE:
      Serial.println("WiFi scan done");
      if (!ethConnected && !WifiAPMode)
        initWiFi(netConfig.wifiSSID.c_str(), netConfig.wifiPassword.c_str());
      break;
    default:
      break;
  }
}
#endif // RAVLIGHT_MODULE_ETHERNET

void initEthernet() {
#ifdef RAVLIGHT_MODULE_ETHERNET
  WiFi.onEvent(WiFiEvent);
  if (!netConfig.dhcp) {
    ETH.begin();
    IPAddress localIP, subnet, gateway;
    localIP.fromString(netConfig.ip.c_str());
    subnet.fromString(netConfig.subnet.c_str());
    gateway.fromString(netConfig.gateway.c_str());
    ETH.config(localIP, gateway, subnet);
  } else {
    ETH.begin();
  }
  Serial.println("Ethernet initialized");
  String hostname = "Ravlight-" + setConfig.ID_fixture;
  ETH.setHostname(hostname.c_str());
  if (!ethConnected) {
    Serial.println("Ethernet not connected, starting WiFi");
    initWiFi(netConfig.wifiSSID.c_str(), netConfig.wifiPassword.c_str());
  }
#else
  // WiFi-only board: skip Ethernet, go directly to WiFi
  initWiFi(netConfig.wifiSSID.c_str(), netConfig.wifiPassword.c_str());
#endif
}


void initWiFi(const char* ssid, const char* password) {
  if (netConfig.wifiSSID.length() > 0 && netConfig.wifiPassword.length() > 0) {
    Serial.println("WiFi STA starting");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    String hostname = "Ravlight-" + setConfig.ID_fixture;
    WiFi.setHostname(hostname.c_str());
    if (netConfig.dhcp) {
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    } else {
      IPAddress localIP, subnet, gateway;
      localIP.fromString(netConfig.ip.c_str());
      subnet.fromString(netConfig.subnet.c_str());
      gateway.fromString(netConfig.gateway.c_str());
      WiFi.config(localIP, gateway, subnet);
    }
    Serial.println("WiFi STA initialized");

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("\nWiFi STA failed, starting AP");
      initWifiAP();
      return;
    }

    IPAddress ip = WiFi.localIP();
    IPAddress gw = WiFi.gatewayIP();
    IPAddress mask = WiFi.subnetMask();

    bool subnetMatch = true;
    for (int i = 0; i < 4; i++) {
      if ((ip[i] & mask[i]) != (gw[i] & mask[i])) { subnetMatch = false; break; }
    }
    if (!subnetMatch) {
      Serial.printf("[NET] Subnet mismatch: IP %s GW %s — switching to DHCP\n",
                    ip.toString().c_str(), gw.toString().c_str());
      netConfig.dhcp = true;
      saveConfig();
      delay(500);
      ESP.restart();
    }
    if (!Ping.ping(gw, 2)) {
      Serial.printf("[NET] Gateway %s not reachable — switching to DHCP\n", gw.toString().c_str());
      netConfig.dhcp = true;
      saveConfig();
      delay(500);
      ESP.restart();
    }

  } else if (!WifiAPMode) {
    Serial.println("Starting WiFi AP");
    initWifiAP();
  }
}


void initWifiAP() {
  if (!WifiAPMode) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(false);
    char apName[20];
    snprintf(apName, sizeof(apName), "%s-%s", PROJECT_NAME, setConfig.ID_fixture.c_str());
    WiFi.softAP(apName, "123456789", AP_CHANNEL);
    netConfig.currentip = WiFi.softAPIP().toString();
    Serial.printf("WiFi AP: %s, IP: %s\n", apName, netConfig.currentip.c_str());
    WifiAPMode = true;
    setMDNSHost(setConfig.ID_fixture);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(AP_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    initESPNow();
  }
}

void checkNetwork() {
  if (WiFiScanStatus == WIFI_SCAN_RUNNING) {
    WiFiScanStatus = WiFi.scanComplete();
    if (WiFiScanStatus == 0) Serial.println("No networks found");
  } else if (WiFiScanStatus > 0) {
    printScannedNetworks(WiFiScanStatus);
  } else if (WiFiScanStatus == WIFI_SCAN_FAILED) {
    Serial.println("WiFi scan failed");
  }
}

void scanWiFiNetworks() {
  Serial.println("Scanning for WiFi networks...");
  WiFi.scanNetworks(true, false, true);
  WiFiScanStatus = WiFi.scanComplete();
}

void printScannedNetworks(uint16_t networksFound) {
  Serial.printf("\n%d networks found\n", networksFound);
  ScannedSSID = "[";
  for (int i = 0; i < networksFound; ++i) {
    if (i) ScannedSSID += ",";
    ScannedSSID += "\"" + WiFi.SSID(i) + "\"";
    Serial.printf("%2d | %-32.32s | %4ld | %2ld\n",
                  i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
    delay(10);
  }
  ScannedSSID += "]";
  WiFi.scanDelete();
  WiFiScanStatus = 0;
}

void setMDNSHost(const String& fixtureID) {
  String mdnsName = "rav" + fixtureID;
  if (MDNS.begin(mdnsName.c_str())) {
    Serial.printf("mDNS: %s.local\n", mdnsName.c_str());
  } else {
    Serial.println("[NET] mDNS setup failed");
  }
}

const char* getConnectionMode() {
#ifdef RAVLIGHT_MODULE_ETHERNET
  if (ethConnected) return "ETH";
#endif
  if (WifiAPMode) return "AP-WiFi";
  if (WiFi.status() == WL_CONNECTED) return "WiFi";
  return "not connected";
}
