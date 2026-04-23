#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "settings.h"
#ifdef RAVLIGHT_FIXTURE_VEYRON
#include "dmx_fixture.h"
#endif

#define RESET_HOLD_TIME  10000
#define CONFIG_VERSION   2

NetworkConfig netConfig;
SetConfig     setConfig;
DmxConfig     dmxConfig;

#ifdef RAVLIGHT_MASTER
InfoConfig infoConfig;
#endif

// ── Defaults ────────────────────────────────────────────────────────────────

static void applyDefaults() {
    setConfig.ID_fixture = "RV1";
    netConfig.wifiSSID     = "";
    netConfig.wifiPassword = "";
    netConfig.dhcp         = true;
    netConfig.ip           = "192.168.1.100";
    netConfig.subnet       = "255.255.255.0";
    netConfig.gateway      = "192.168.1.1";
    setConfig.DimCurves    = LINEAR;
    dmxConfig.dmxInput         = DMX_PHYSICAL;
    dmxConfig.dmxOutputEnabled = false;
    dmxConfig.startUniverse    = 0;
    dmxConfig.RGBWstartAddress  = 1;
    dmxConfig.WhStartAddress    = 121;
    dmxConfig.strobeStartAddress = 127;
    dmxConfig.selectedPersonality = PERSONALITY_1;
}

// ── Serialize (current state → JSON sections) ───────────────────────────────

static void serializeNetwork(JsonObject& net) {
    net["id"]       = setConfig.ID_fixture;
    net["ssid"]     = netConfig.wifiSSID;
    net["password"] = netConfig.wifiPassword;
    net["dhcp"]     = netConfig.dhcp;
    net["ip"]       = netConfig.ip;
    net["subnet"]   = netConfig.subnet;
    net["gateway"]  = netConfig.gateway;
}

static void serializeDmx(JsonObject& dmx) {
    dmx["input"]    = dmxConfig.dmxInput;
    dmx["universe"] = dmxConfig.startUniverse;
    dmx["output"]   = dmxConfig.dmxOutputEnabled;
    dmx["dimCurve"] = setConfig.DimCurves;
}

static void serializeFixture(JsonObject& fix) {
    fix["personality"] = (int)dmxConfig.selectedPersonality;
    fix["rgbw"]        = dmxConfig.RGBWstartAddress;
    fix["white"]       = dmxConfig.WhStartAddress;
    fix["strobe"]      = dmxConfig.strobeStartAddress;
}

// ── Deserialize (JSON sections → current state) ──────────────────────────────

static void deserializeNetwork(const JsonObject& net) {
    setConfig.ID_fixture   = net["id"]       | "RV1";
    netConfig.wifiSSID     = net["ssid"]     | "";
    netConfig.wifiPassword = net["password"] | "";
    netConfig.dhcp         = net["dhcp"]     | true;
    netConfig.ip           = net["ip"]       | "192.168.1.100";
    netConfig.subnet       = net["subnet"]   | "255.255.255.0";
    netConfig.gateway      = net["gateway"]  | "192.168.1.1";
}

static void deserializeDmx(const JsonObject& dmx) {
    dmxConfig.dmxInput         = dmx["input"]    | (int)DMX_PHYSICAL;
    dmxConfig.startUniverse    = dmx["universe"] | 0;
    dmxConfig.dmxOutputEnabled = dmx["output"]   | false;
    setConfig.DimCurves        = dmx["dimCurve"] | (int)LINEAR;
}

static void deserializeFixture(const JsonObject& fix) {
    dmxConfig.RGBWstartAddress   = fix["rgbw"]   | 1;
    dmxConfig.WhStartAddress     = fix["white"]  | 121;
    dmxConfig.strobeStartAddress = fix["strobe"] | 127;
#ifdef RAVLIGHT_FIXTURE_VEYRON
    setPersonality(static_cast<FixturePersonality>(fix["personality"] | (int)PERSONALITY_1));
#endif
}

// ── Legacy v1 flat-format migration ─────────────────────────────────────────

static void migrateV1(const DynamicJsonDocument& doc) {
    Serial.println("[CFG] Migrating config from v1 flat format");
    setConfig.ID_fixture   = doc["ID_fixture"] | "RV1";
    netConfig.wifiSSID     = doc["ssid"]       | "";
    netConfig.wifiPassword = doc["password"]   | "";
    netConfig.dhcp         = doc["dhcp"]       | true;
    netConfig.ip           = doc["ip"]         | "192.168.1.100";
    netConfig.subnet       = doc["subnet"]     | "255.255.255.0";
    netConfig.gateway      = doc["gateway"]    | "192.168.1.1";
    setConfig.DimCurves    = doc["dimCurves"]  | (int)LINEAR;
    dmxConfig.dmxInput         = doc["dmxInput"]          | (int)DMX_PHYSICAL;
    dmxConfig.dmxOutputEnabled = doc["dmxOutput"]         | false;
    dmxConfig.startUniverse    = doc["startUniverse"]     | 0;
    dmxConfig.RGBWstartAddress  = doc["RGBWstartAddress"] | 1;
    dmxConfig.WhStartAddress    = doc["WhStartAddress"]   | 121;
    dmxConfig.strobeStartAddress = doc["strobeStartAddress"] | 127;
#ifdef RAVLIGHT_FIXTURE_VEYRON
    setPersonality(static_cast<FixturePersonality>(doc["personality"] | (int)PERSONALITY_1));
#endif
    // Save in new format immediately
    saveConfig();
}

// ── Public API ───────────────────────────────────────────────────────────────

void intiConfig() {
#ifdef RAVLIGHT_MODULE_RESET
    pinMode(HW_PIN_RESET, INPUT_PULLUP);
#endif
    loadConfig();
}

void loadDefaultConfig() {
    File file = SPIFFS.open("/default_config.json", "r");
    if (!file) {
        Serial.println("[CFG] Default config not found, using built-in defaults");
        applyDefaults();
        saveConfig();
        return;
    }

    String content;
    while (file.available()) content += (char)file.read();
    file.close();

    DynamicJsonDocument doc(768);
    if (deserializeJson(doc, content)) {
        Serial.println("[CFG] Default config parse error, using built-in defaults");
        applyDefaults();
        saveConfig();
        return;
    }

    int version = doc["version"] | 0;
    if (version >= CONFIG_VERSION) {
        deserializeNetwork(doc["network"].as<JsonObject>());
        deserializeDmx(doc["dmx"].as<JsonObject>());
#ifdef RAVLIGHT_FIXTURE_VEYRON
        deserializeFixture(doc["fixture"].as<JsonObject>());
#endif
    } else {
        migrateV1(doc);
        return;
    }

    Serial.println("[CFG] Default config loaded");
    saveConfig();
}

void loadConfig() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[CFG] SPIFFS init failed");
        return;
    }

    File file = SPIFFS.open("/config.json", "r");
    if (!file) {
        Serial.println("[CFG] Config not found, loading defaults");
        loadDefaultConfig();
        return;
    }

    String content;
    while (file.available()) content += (char)file.read();
    file.close();

    Serial.println("[CFG] Config:");
    Serial.println(content);

    DynamicJsonDocument doc(768);
    if (deserializeJson(doc, content)) {
        Serial.println("[CFG] Config parse error, using defaults");
        loadDefaultConfig();
        return;
    }

    int version = doc["version"] | 0;
    if (version >= CONFIG_VERSION) {
        deserializeNetwork(doc["network"].as<JsonObject>());
        deserializeDmx(doc["dmx"].as<JsonObject>());
#ifdef RAVLIGHT_FIXTURE_VEYRON
        deserializeFixture(doc["fixture"].as<JsonObject>());
#endif
    } else {
        migrateV1(doc);
    }
}

void saveConfig() {
    DynamicJsonDocument doc(768);
    doc["version"] = CONFIG_VERSION;

    JsonObject net = doc.createNestedObject("network");
    serializeNetwork(net);

    JsonObject dmx = doc.createNestedObject("dmx");
    serializeDmx(dmx);

#ifdef RAVLIGHT_FIXTURE_VEYRON
    JsonObject fix = doc.createNestedObject("fixture");
    serializeFixture(fix);
#endif

    File file = SPIFFS.open("/config.json", "w");
    if (!file) {
        Serial.println("[CFG] Failed to open config for writing");
        return;
    }
    serializeJsonPretty(doc, file);
    file.close();
    Serial.println("[CFG] Config saved");
}

void resetConfig() {
    if (SPIFFS.begin(true)) {
        Serial.println("[CFG] Resetting to defaults");
        loadDefaultConfig();
    } else {
        Serial.println("[CFG] SPIFFS init failed during reset");
    }
}

#ifdef RAVLIGHT_MODULE_RESET
void checkResetButton() {
    static unsigned long buttonPressStart = 0;
    if (digitalRead(HW_PIN_RESET) == LOW) {
        if (buttonPressStart == 0) {
            buttonPressStart = millis();
        } else if (millis() - buttonPressStart >= RESET_HOLD_TIME) {
            Serial.println("[CFG] Reset button held — resetting config");
            resetConfig();
            ESP.restart();
        }
    } else {
        buttonPressStart = 0;
    }
}
#endif // RAVLIGHT_MODULE_RESET
