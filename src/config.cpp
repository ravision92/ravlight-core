#include <LittleFS.h>
#include <ArduinoJson.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include "config.h"
#include "fixture_config.h"

#define TAG                    "CFG"
#define RESET_HOLD_TIME        10000   // ms — factory reset
#define BLE_SHORT_PRESS_TIME   1000    // ms — re-open BLE window
#define CONFIG_VERSION         2
#define NVS_NAMESPACE    "ravlight"
#define NVS_KEY          "config"
#define NVS_BUF_SIZE     2048   // headroom for Elyon 8-output JSON

NetworkConfig netConfig;
SetConfig     setConfig;
DmxConfig     dmxConfig;

#ifdef RAVLIGHT_MASTER
InfoConfig infoConfig;
#endif

// ── Defaults ────────────────────────────────────────────────────────────────

static String generateMacId() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char id[7];
    snprintf(id, sizeof(id), "RV%02X%02X", mac[4], mac[5]);
    return String(id);
}

static void applyDefaults() {
    setConfig.ID_fixture       = generateMacId();
    netConfig.wifiSSID         = "";
    netConfig.wifiPassword     = "";
    netConfig.dhcp             = true;
    netConfig.ip               = "192.168.1.100";
    netConfig.subnet           = "255.255.255.0";
    netConfig.gateway          = "192.168.1.1";
    dmxConfig.dmxInput         = ARTNET;
    dmxConfig.dmxOutputEnabled = false;
    dmxConfig.startUniverse    = 0;
    dmxConfig.autoSceneSlot    = 0;
    fixtureConfigDefaults();
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
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    dmx["output"]   = dmxConfig.dmxOutputEnabled;
#endif
#ifdef RAVLIGHT_MODULE_RECORDER
    dmx["autoSceneSlot"] = dmxConfig.autoSceneSlot;
#endif
}

static void serializeFixture(JsonObject& fix) {
    fixtureConfigSerialize(fix);
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
    dmxConfig.dmxInput      = dmx["input"]   | (int)ARTNET;
    dmxConfig.startUniverse = dmx["universe"] | 0;
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    dmxConfig.dmxOutputEnabled = dmx["output"] | false;
#endif
#ifdef RAVLIGHT_MODULE_RECORDER
    dmxConfig.autoSceneSlot = dmx["autoSceneSlot"] | 0;
#endif
}

static void deserializeFixture(const JsonObject& fix) {
    fixtureConfigDeserialize(fix);
}

// ── Legacy v1 flat-format migration ─────────────────────────────────────────

static void migrateV1(const DynamicJsonDocument& doc) {
    ESP_LOGI(TAG, "Migrating config from v1 flat format");
    setConfig.ID_fixture    = doc["ID_fixture"]    | "RV1";
    netConfig.wifiSSID      = doc["ssid"]          | "";
    netConfig.wifiPassword  = doc["password"]      | "";
    netConfig.dhcp          = doc["dhcp"]          | true;
    netConfig.ip            = doc["ip"]            | "192.168.1.100";
    netConfig.subnet        = doc["subnet"]        | "255.255.255.0";
    netConfig.gateway       = doc["gateway"]       | "192.168.1.1";
    dmxConfig.dmxInput      = doc["dmxInput"]      | (int)ARTNET;
    dmxConfig.startUniverse = doc["startUniverse"] | 0;
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
    dmxConfig.dmxOutputEnabled = doc["dmxOutput"]  | false;
#endif
    // v1 had no fixture section — apply fixture defaults
    fixtureConfigDefaults();
    saveConfig();
}

// ── Public JSON import (used by /upload_config) ──────────────────────────────

void applyConfigJson(DynamicJsonDocument& doc) {
    int version = doc["version"] | 0;
    if (version >= CONFIG_VERSION) {
        deserializeNetwork(doc["network"].as<JsonObject>());
        deserializeDmx(doc["dmx"].as<JsonObject>());
        deserializeFixture(doc["fixture"].as<JsonObject>());
    } else {
        migrateV1(doc);
    }
}

// ── NVS helpers ─────────────────────────────────────────────────────────────

static void initNVSFlash() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

// ── Public API ───────────────────────────────────────────────────────────────

void loadDefaultConfig() {
    File file = LittleFS.open("/default_config.json", "r");
    if (!file) {
        ESP_LOGW(TAG, "Default config not found, using built-in defaults");
        applyDefaults();
        saveConfig();
        return;
    }

    String content;
    while (file.available()) content += (char)file.read();
    file.close();

    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, content)) {
        ESP_LOGW(TAG, "Default config parse error, using built-in defaults");
        applyDefaults();
        saveConfig();
        return;
    }

    int version = doc["version"] | 0;
    if (version >= CONFIG_VERSION) {
        deserializeNetwork(doc["network"].as<JsonObject>());
        deserializeDmx(doc["dmx"].as<JsonObject>());
        deserializeFixture(doc["fixture"].as<JsonObject>());
    } else {
        migrateV1(doc);
        // migrateV1 calls saveConfig() internally; override ID and re-save
        setConfig.ID_fixture = generateMacId();
        saveConfig();
        return;
    }

    // Always use MAC-based ID on factory reset — never the static value from the JSON file
    setConfig.ID_fixture = generateMacId();
    ESP_LOGI(TAG, "Default config loaded");
    saveConfig();
}

void intiConfig() {
#ifdef RAVLIGHT_MODULE_RESET
    gpio_set_direction((gpio_num_t)HW_PIN_RESET, GPIO_MODE_INPUT);
    gpio_pullup_en((gpio_num_t)HW_PIN_RESET);
#endif
    loadConfig();
}

void loadConfig() {
    initNVSFlash();
    LittleFS.begin(true);   // needed for web assets and scene files

    char* buf = (char*)malloc(NVS_BUF_SIZE);
    if (!buf) { ESP_LOGE(TAG, "loadConfig: out of memory"); applyDefaults(); return; }
    size_t len = NVS_BUF_SIZE;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, NVS_KEY, buf, &len);
        nvs_close(h);
    }

    if (err != ESP_OK) {
        free(buf);
        ESP_LOGW(TAG, "No config in NVS, loading defaults");
        loadDefaultConfig();
        return;
    }

    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, buf, len)) {
        free(buf);
        ESP_LOGW(TAG, "Config parse error, using defaults");
        loadDefaultConfig();
        return;
    }
    free(buf);

    ESP_LOGI(TAG, "Config loaded from NVS");

    int version = doc["version"] | 0;
    if (version >= CONFIG_VERSION) {
        deserializeNetwork(doc["network"].as<JsonObject>());
        deserializeDmx(doc["dmx"].as<JsonObject>());
        deserializeFixture(doc["fixture"].as<JsonObject>());
    } else {
        migrateV1(doc);
    }
}

void saveConfig() {
    DynamicJsonDocument doc(4096);
    doc["version"] = CONFIG_VERSION;

    JsonObject net = doc.createNestedObject("network");
    serializeNetwork(net);

    JsonObject dmx = doc.createNestedObject("dmx");
    serializeDmx(dmx);

    JsonObject fix = doc.createNestedObject("fixture");
    serializeFixture(fix);

    char* buf = (char*)malloc(NVS_BUF_SIZE);
    if (!buf) { ESP_LOGE(TAG, "saveConfig: out of memory"); return; }
    size_t len = serializeJson(doc, buf, NVS_BUF_SIZE);

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY, buf, len);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Config saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
    }
    free(buf);
}

void resetConfig() {
    ESP_LOGI(TAG, "Resetting to defaults");
    loadDefaultConfig();
}

#ifdef RAVLIGHT_MODULE_BLE
  #include "ble_manager.h"
#endif

#ifdef RAVLIGHT_MODULE_RESET
void checkResetButton() {
    static uint32_t buttonPressStart = 0;
    static bool     buttonWasHeld    = false;

    if (gpio_get_level((gpio_num_t)HW_PIN_RESET) == 0) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (buttonPressStart == 0) {
            buttonPressStart = now;
            buttonWasHeld    = false;
        } else if (!buttonWasHeld && now - buttonPressStart >= RESET_HOLD_TIME) {
            buttonWasHeld = true;
            ESP_LOGW(TAG, "Reset button held — resetting config");
            resetConfig();
            esp_restart();
        }
    } else {
        if (buttonPressStart != 0 && !buttonWasHeld) {
            uint32_t now  = (uint32_t)(esp_timer_get_time() / 1000ULL);
            uint32_t held = now - buttonPressStart;
            if (held >= BLE_SHORT_PRESS_TIME) {
                ESP_LOGI(TAG, "Short press — re-opening BLE window");
#ifdef RAVLIGHT_MODULE_BLE
                initBLE();
#endif
            }
        }
        buttonPressStart = 0;
        buttonWasHeld    = false;
    }
}
#endif // RAVLIGHT_MODULE_RESET
