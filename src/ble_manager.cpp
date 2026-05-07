#ifdef RAVLIGHT_MODULE_BLE
#include "ble_manager.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "config.h"
#include "network_manager.h"
#include "fixture_config.h"

#ifdef RAVLIGHT_MODULE_TEMP
#include "temp_sensor.h"
#endif

#define TAG "BLE"

#define BLE_SERVICE_UUID   "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define CHAR_INFO_UUID     "BEB54801-36E1-4688-B7F5-EA07361B26A8"
#define CHAR_SSID_UUID     "BEB54802-36E1-4688-B7F5-EA07361B26A8"
#define CHAR_PASS_UUID     "BEB54803-36E1-4688-B7F5-EA07361B26A8"
#define CHAR_CONNECT_UUID  "BEB54804-36E1-4688-B7F5-EA07361B26A8"
#define CHAR_CMD_UUID      "BEB54805-36E1-4688-B7F5-EA07361B26A8"

static bool               s_bleActive          = false;
static volatile bool      s_bleShutdownPending = false;
static esp_timer_handle_t s_bleTimer           = nullptr;
static char               s_wifiSsid[33]       = {0};
static char               s_wifiPass[65]       = {0};

// Characteristic pointers — valid during the active window, null otherwise
static NimBLECharacteristic* s_pSsid    = nullptr;
static NimBLECharacteristic* s_pPass    = nullptr;
static NimBLECharacteristic* s_pConnect = nullptr;
static NimBLECharacteristic* s_pCmd     = nullptr;

static void bleSleepCallback(void*) {
    s_bleShutdownPending = true;
}

static void bleRestartCallback(void*) {
    esp_restart();
}

static uint8_t fixtureTypeByte() {
#if defined(RAVLIGHT_FIXTURE_VEYRON)
    return 0;
#elif defined(RAVLIGHT_FIXTURE_ELYON)
    return 1;
#elif defined(RAVLIGHT_FIXTURE_AXON)
    return 2;
#else
    return 0xFF;
#endif
}

static uint8_t statusFlagsByte() {
    const char* mode = getConnectionMode();
    if (strcmp(mode, "ETH")     == 0) return 0x01;
    if (strcmp(mode, "WiFi")    == 0) return 0x02;
    if (strcmp(mode, "AP-WiFi") == 0) return 0x04;
    return 0x00;
}

// ── GATT write callbacks ──────────────────────────────────────────────────────

class ProvisionCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        std::string val = pChar->getValue();
        if (pChar == s_pSsid) {
            strncpy(s_wifiSsid, val.c_str(), sizeof(s_wifiSsid) - 1);
            ESP_LOGI(TAG, "SSID set: %s", s_wifiSsid);
        } else if (pChar == s_pPass) {
            strncpy(s_wifiPass, val.c_str(), sizeof(s_wifiPass) - 1);
            ESP_LOGI(TAG, "Password set (%d chars)", (int)val.size());
        } else if (pChar == s_pConnect) {
            ESP_LOGI(TAG, "WiFi connect triggered: ssid=%s", s_wifiSsid);
            netConfig.wifiSSID     = String(s_wifiSsid);
            netConfig.wifiPassword = String(s_wifiPass);
            netConfig.dhcp         = true;
            saveConfig();
            // Deferred restart — gives BLE stack time to send the write response
            static esp_timer_handle_t restartTimer = nullptr;
            const esp_timer_create_args_t args = { .callback = bleRestartCallback, .name = "ble_rst" };
            esp_timer_create(&args, &restartTimer);
            esp_timer_start_once(restartTimer, 500000ULL);  // 500 ms
        } else if (pChar == s_pCmd) {
            StaticJsonDocument<128> doc;
            if (!deserializeJson(doc, val)) {
                const char* cmd = doc["cmd"] | "";
                if (strcmp(cmd, "HIGHLIGHT") == 0) {
                    fixtureHighlight();
                } else if (strcmp(cmd, "RESET") == 0) {
                    loadDefaultConfig();
                    static esp_timer_handle_t restartTimer = nullptr;
                    const esp_timer_create_args_t args = { .callback = bleRestartCallback, .name = "ble_rst2" };
                    esp_timer_create(&args, &restartTimer);
                    esp_timer_start_once(restartTimer, 500000ULL);
                } else if (strcmp(cmd, "APMODE") == 0) {
                    initWifiAP();
                }
            }
        }
    }
};

static ProvisionCallbacks s_provCallbacks;

// ── Advertising ───────────────────────────────────────────────────────────────

void updateBleAdvertising() {
    if (!s_bleActive) return;

    uint8_t mfr[18];
    mfr[0] = 0xFF; mfr[1] = 0xFF;  // Company ID (prototype / unregistered)
    mfr[2] = 'R';  mfr[3] = 'V';   // RavLight magic
    mfr[4] = fixtureTypeByte();
    mfr[5] = statusFlagsByte();

    IPAddress ip;
    ip.fromString(netConfig.currentip.c_str());
    mfr[6] = ip[0]; mfr[7] = ip[1]; mfr[8] = ip[2]; mfr[9] = ip[3];

    const String& id = setConfig.ID_fixture;
    for (int i = 0; i < 6; i++)
        mfr[10 + i] = (i < (int)id.length()) ? (uint8_t)id[i] : 0;

    uint8_t fwMajor = (uint8_t)atoi(FW_VERSION);
    const char* dot = strchr(FW_VERSION, '.');
    uint8_t fwMinor = dot ? (uint8_t)atoi(dot + 1) : 0;
    mfr[16] = fwMajor;
    mfr[17] = fwMinor;

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->stop();
    pAdv->setManufacturerData(std::string((char*)mfr, sizeof(mfr)));
    pAdv->start();
}

// ── Init / Deinit ─────────────────────────────────────────────────────────────

void deinitBLE() {
    if (!s_bleActive) return;
    s_bleActive = false;
    s_pSsid = s_pPass = s_pConnect = s_pCmd = nullptr;
    NimBLEDevice::deinit(true);
    if (s_bleTimer) {
        esp_timer_stop(s_bleTimer);
        esp_timer_delete(s_bleTimer);
        s_bleTimer = nullptr;
    }
    ESP_LOGI(TAG, "BLE window closed");
}

void initBLE() {
    if (s_bleActive) {
        // Already active — restart the 10-minute window
        if (s_bleTimer) {
            esp_timer_stop(s_bleTimer);
            esp_timer_start_once(s_bleTimer, (uint64_t)BLE_ACTIVE_WINDOW_MS * 1000ULL);
        }
        ESP_LOGI(TAG, "BLE window restarted");
        return;
    }

    // ID_fixture is already "RVXXXX" — no prefix needed.
    // Total adv packet must stay ≤ 31 bytes: flags(3) + name(8) + mfrData(20) = 31.
    const String& devName = setConfig.ID_fixture;
    NimBLEDevice::init(devName.c_str());

    // GATT Server
    NimBLEServer*  pServer  = NimBLEDevice::createServer();
    NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

    // INFO — read-only: JSON snapshot of device state
    NimBLECharacteristic* pInfo = pService->createCharacteristic(CHAR_INFO_UUID, NIMBLE_PROPERTY::READ);
    {
        StaticJsonDocument<256> doc;
        doc["id"]      = setConfig.ID_fixture;
        doc["ip"]      = netConfig.currentip;
        doc["fixture"] = PROJECT_NAME;
        doc["fw"]      = FW_VERSION;
        doc["mode"]    = getConnectionMode();
#ifdef RAVLIGHT_MODULE_TEMP
        doc["temp"]    = readTemperature();
#else
        doc["temp"]    = 0.0f;
#endif
        String info;
        serializeJson(doc, info);
        pInfo->setValue(info.c_str());
    }

    // Provisioning write characteristics
    s_pSsid    = pService->createCharacteristic(CHAR_SSID_UUID,    NIMBLE_PROPERTY::WRITE);
    s_pPass    = pService->createCharacteristic(CHAR_PASS_UUID,    NIMBLE_PROPERTY::WRITE);
    s_pConnect = pService->createCharacteristic(CHAR_CONNECT_UUID, NIMBLE_PROPERTY::WRITE);
    s_pCmd     = pService->createCharacteristic(CHAR_CMD_UUID,     NIMBLE_PROPERTY::WRITE);
    s_pSsid->setCallbacks(&s_provCallbacks);
    s_pPass->setCallbacks(&s_provCallbacks);
    s_pConnect->setCallbacks(&s_provCallbacks);
    s_pCmd->setCallbacks(&s_provCallbacks);

    // Service UUID omitted from advertising packet (saves 18 bytes).
    // GATT services are discoverable after connection regardless.

    s_bleActive = true;
    updateBleAdvertising();

    // Schedule shutdown after active window
    const esp_timer_create_args_t timerArgs = { .callback = bleSleepCallback, .name = "ble_win" };
    esp_timer_create(&timerArgs, &s_bleTimer);
    esp_timer_start_once(s_bleTimer, (uint64_t)BLE_ACTIVE_WINDOW_MS * 1000ULL);

    ESP_LOGI(TAG, "BLE started as %s (window: 10 min)", setConfig.ID_fixture.c_str());
}

bool isBleActive() { return s_bleActive; }

void updateBLE() {
    if (s_bleShutdownPending) {
        s_bleShutdownPending = false;
        deinitBLE();
    }
}

#endif // RAVLIGHT_MODULE_BLE
