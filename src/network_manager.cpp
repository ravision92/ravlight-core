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
// Deferred-reconnect state. ETH_DISCONNECTED fires from the WiFi/lwIP task
// and we must not block it with initWiFi()'s 10-second connect loop. Set a
// timestamp here and let checkNetwork() (main loop) start WiFi after the
// debounce window — short cable wiggles never reach a reconnect attempt.
static volatile uint32_t s_eth_lost_ms = 0;
#define ETH_FALLBACK_DEBOUNCE_MS  1500
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
      s_eth_lost_ms = 0;    // cancel any pending WiFi fallback — ETH is back
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
      ethConnected = false;
      // Drop the stale ETH-DHCP'd IP so the UI/OLED don't keep advertising
      // an address that's no longer routable. Reset to empty string —
      // getConnectionMode() and the OLED both treat that as "no link",
      // until WIFI_STA_GOT_IP repopulates it.
      netConfig.currentip = "";
      // Defer the WiFi fallback to checkNetwork() in the main loop —
      // initWiFi() blocks for ~10 s and must never run from a WiFi event.
      s_eth_lost_ms = millis();
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
#ifdef BOARD_ETH_POWER_REQUIRES_BOOT
  // QuinLED-ESP32-AE (and a few similar modules) ship the PHY regulator
  // disabled — the Arduino ETH driver expects the rail to be live before
  // it talks SMI, so we drive the pin HIGH here and give the regulator a
  // moment to stabilise before ETH.begin() probes the PHY.
  pinMode(BOARD_ETH_POWER_REQUIRES_BOOT, OUTPUT);
  digitalWrite(BOARD_ETH_POWER_REQUIRES_BOOT, HIGH);
  delay(50);
#endif
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
  Serial.println("Ethernet initialized, waiting for link...");
  String hostname = "Ravlight-" + setConfig.ID_fixture;
  ETH.setHostname(hostname.c_str());
  // ETH.begin() is async — ARDUINO_EVENT_ETH_CONNECTED fires seconds later.
  // Boards with ETH_CLOCK_GPIO17_OUT (Octa, QuinLED-ESP32-AE, XDMX v1.4)
  // generate the PHY reference clock from the ESP32 itself and the PHY
  // needs 3–4 seconds after power to negotiate link with the switch.
  // The previous 2 s window was borderline: a cable that was even
  // slightly late linking at boot would time out and the device would
  // fall back to WiFi, staying on WiFi for the whole session.
  unsigned long ethWaitStart = millis();
  while (!ethConnected && millis() - ethWaitStart < 5000) {
    delay(100);
  }
  if (!ethConnected) {
    Serial.println("Ethernet link timeout, starting WiFi");
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
#ifdef RAVLIGHT_MODULE_ETHERNET
  // Deferred WiFi fallback after ETH cable unplug. The event handler only
  // sets a timestamp; we wait the debounce window to ride out cable
  // wiggles, then bring up WiFi STA. If WiFi STA is already up (e.g. the
  // device booted on WiFi and ETH just dropped without ever being primary)
  // we skip — it's already serving requests.
  if (s_eth_lost_ms != 0 && !ethConnected &&
      millis() - s_eth_lost_ms > ETH_FALLBACK_DEBOUNCE_MS) {
    uint32_t lost = s_eth_lost_ms;
    s_eth_lost_ms = 0;
    if (WiFi.status() != WL_CONNECTED && !WifiAPMode) {
      Serial.printf("[NET] ETH down %u ms — falling back to WiFi STA\n",
                    (unsigned)(millis() - lost));
      initWiFi(netConfig.wifiSSID.c_str(), netConfig.wifiPassword.c_str());
    }
  }
#endif

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

void suspendWiFiSTA() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false);
    Serial.println("[NET] WiFi STA suspended for ESP-NOW scan");
  }
}

void resumeWiFiSTA() {
  WiFi.reconnect();
  Serial.println("[NET] WiFi STA reconnecting after ESP-NOW scan");
}

const char* getConnectionMode() {
#ifdef RAVLIGHT_MODULE_ETHERNET
  if (ethConnected) return "ETH";
#endif
  if (WifiAPMode) return "AP-WiFi";
  if (WiFi.status() == WL_CONNECTED) return "WiFi";
  return "not connected";
}
