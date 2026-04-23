#include <Arduino.h>
#include "settings.h"
#include "config.h"
#include "network_manager.h"
#include "webserver_manager.h"
#include "runtimeNVS.h"
#include "discovery_udp.h"
#include "discovery_espnow.h"
#include <WiFi.h>
#include <ElegantOTA.h>
#include "dmx_manager.h"

#ifdef RAVLIGHT_MODULE_RECORDER
  #include "dmx_recorder.h"
#endif
#ifdef RAVLIGHT_FIXTURE_VEYRON
  #include "dmx_fixture.h"
#endif
#ifdef RAVLIGHT_MODULE_TEMP
  #include "temp_sensor.h"
#endif

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("");
    Serial.println("RavLight " + String(PROJECT_NAME) + " " + FW_VERSION);
    Serial.println("Limitless creativity");
    Serial.println("R&D by @Ravision92");
    Serial.println("=^.^=");

    intiConfig();
    initNVS();

    #ifdef RAVLIGHT_FIXTURE_VEYRON
      initFixture();
    #endif

    initEthernet();
    initDmxInputs();

    #ifdef RAVLIGHT_MODULE_TEMP
      initTemperatureSensor();
    #endif

    initWebServer();
    initUDP();

    delay(300);
}

void loop() {
    receiveDmxData();

    #ifdef RAVLIGHT_FIXTURE_VEYRON
      handleDMX();
    #endif

    #ifdef RAVLIGHT_MODULE_TEMP
      updateTemperature();
    #endif

    #ifdef RAVLIGHT_MODULE_RESET
      checkResetButton();
    #endif

    ElegantOTA.loop();
    checkNetwork();
    updateRuntime();
}
