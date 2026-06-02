#include <Arduino.h>
#include "config.h"
#include "network_manager.h"
#include "webserver_manager.h"
#include "runtime.h"
#include "discovery_udp.h"
#include "discovery_espnow.h"
#include <WiFi.h>
#include <ElegantOTA.h>
#include "dmx_manager.h"
#include <esp_ota_ops.h>



#ifdef RAVLIGHT_MODULE_RECORDER
  #include "dmx_recorder.h"
#endif
#ifdef RAVLIGHT_MODULE_DISCOVERY
  #include "discovery_shared.h"
#endif
#ifdef RAVLIGHT_MODULE_TEMP
  #include "temp_sensor.h"
#endif
#ifdef RAVLIGHT_MODULE_BLE
  #include "ble_manager.h"
#endif
#ifdef RAVLIGHT_MODULE_NFC
  #include "nfc.h"
#endif
#ifdef RAVLIGHT_MODULE_TEST_PATTERN
  #include "test_pattern.h"
#endif

void setup() {
#ifndef RAVLIGHT_DISABLE_SERIAL
    Serial.begin(115200);
#endif
#ifdef RAVLIGHT_MODULE_DISCOVERY
    esp_log_level_set("DISC", ESP_LOG_INFO);
    esp_log_level_set("UDP",  ESP_LOG_INFO);
#endif
    delay(200);
#ifndef RAVLIGHT_DISABLE_SERIAL
    Serial.println("");
    Serial.println("RavLight " + String(PROJECT_NAME) + " " + FW_VERSION);
    Serial.println("Limitless creativity");
    Serial.println("R&D by @Ravision92");
    Serial.println("=^.^=");
#endif

    intiConfig();
    initRuntime();

#ifdef RAVLIGHT_MODULE_NFC
    initNFC();
    nfcBootSync();
#endif

    initFixture();

    initEthernet();
    initDmxInputs();

#ifdef RAVLIGHT_MODULE_TEST_PATTERN
    initTestPattern();
#endif

    #ifdef RAVLIGHT_MODULE_TEMP
      initTemperatureSensor();
    #endif

    initWebServer();
    initUDP();
    initESPNow();
#ifdef RAVLIGHT_MODULE_BLE
    initBLE();
    saveConfig();  // re-save RAM config to NVS in case NimBLE erased it during init
#endif

    delay(300);

    // Mark this firmware as valid so the bootloader does not roll back to the
    // previous OTA slot on the next reset (e.g. after a filesystem upload).
    esp_ota_mark_app_valid_cancel_rollback();
}

void loop() {
    receiveDmxData();

#ifdef RAVLIGHT_MODULE_TEST_PATTERN
    tickTestPattern();
#endif

    handleDMX();

    #ifdef RAVLIGHT_MODULE_TEMP
      updateTemperature();
    #endif

    #ifdef RAVLIGHT_MODULE_RESET
      checkResetButton();
    #endif

    ElegantOTA.loop();
    checkNetwork();
    updateRuntime();
#ifdef RAVLIGHT_MODULE_DISCOVERY
    updateCombinedDiscovery();
#endif
#ifdef RAVLIGHT_MODULE_BLE
    updateBLE();
#endif
#ifdef RAVLIGHT_MODULE_NFC
    nfcLoop();
#endif
}




