#include "serial_console.h"

#ifndef RAVLIGHT_DISABLE_SERIAL

#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "network_manager.h"
#include "version.h"

static String s_line;

static void printHelp() {
    Serial.println(F("\nRavLight serial console:"));
    Serial.println(F("  help                 this list"));
    Serial.println(F("  status               fw / mode / ip / heap / espnow"));
    Serial.println(F("  reboot               restart the device"));
    Serial.println(F("  wifi <ssid> <pass>   set WiFi credentials (DHCP) + reboot"));
    Serial.println(F("  espnow on|off        toggle ESP-NOW discovery + reboot"));
    Serial.println(F("  factory              reset config to defaults + reboot"));
}

static void handle(String line) {
    line.trim();
    if (line.length() == 0) return;

    if (line == "help") {
        printHelp();

    } else if (line == "status") {
        Serial.printf("fw=%s mode=%s ip=%s heap=%u espnow=%d id=%s\n",
                      FW_VERSION, getConnectionMode(), netConfig.currentip.c_str(),
                      (unsigned)ESP.getFreeHeap(), netConfig.espnowEnabled ? 1 : 0,
                      setConfig.ID_fixture.c_str());

    } else if (line == "reboot") {
        Serial.println(F("rebooting..."));
        delay(150);
        esp_restart();

    } else if (line.startsWith("wifi ")) {
        String rest = line.substring(5);
        rest.trim();
        int sp = rest.indexOf(' ');
        if (sp <= 0) { Serial.println(F("usage: wifi <ssid> <pass>")); return; }
        netConfig.wifiSSID     = rest.substring(0, sp);
        netConfig.wifiPassword = rest.substring(sp + 1);
        netConfig.dhcp         = true;
        saveConfig();
        Serial.printf("wifi set (ssid=%s) — rebooting...\n", netConfig.wifiSSID.c_str());
        delay(150);
        esp_restart();

    } else if (line.startsWith("espnow ")) {
        String v = line.substring(7); v.trim();
        if (v == "on")       netConfig.espnowEnabled = true;
        else if (v == "off") netConfig.espnowEnabled = false;
        else { Serial.println(F("usage: espnow on|off")); return; }
        saveConfig();
        Serial.printf("espnow %s — rebooting...\n", netConfig.espnowEnabled ? "ON" : "OFF");
        delay(150);
        esp_restart();

    } else if (line == "factory") {
        Serial.println(F("factory reset — rebooting..."));
        loadDefaultConfig();
        saveConfig();
        delay(150);
        esp_restart();

    } else {
        Serial.printf("unknown: '%s' (type 'help')\n", line.c_str());
    }
}

void checkSerialConsole() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_line.length()) { handle(s_line); s_line = ""; }
        } else if (s_line.length() < 160) {
            s_line += c;
        }
    }
}

#else
void checkSerialConsole() {}
#endif
