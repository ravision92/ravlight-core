#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

// FW_VERSION is defined once in version.h for the entire codebase.
// Board identity (BOARD_NAME, HW_VERSION, HW_PIN_*) comes from boards/<name>.h via platformio.ini.
// Fixture identity (PROJECT_NAME, FIXTURE_STATUS, fixture constants) comes from fixture.h below.
#include "version.h"
#include "fixture_config.h"
#ifdef RAVLIGHT_FIXTURE_VEYRON
  #include "fixtures/veyron/fixture.h"
#elif defined(RAVLIGHT_FIXTURE_ELYON)
  #include "fixtures/elyon/fixture.h"
#else
  #define PROJECT_NAME    "RavLight"
  #define FIXTURE_STATUS  "unknown"
#endif

struct NetworkConfig {
    String wifiSSID;
    String wifiPassword;
    String ip;
    String currentip;
    String subnet;
    String gateway;
    bool dhcp;
};

enum DmxInputType {
    DMX_PHYSICAL = 1,
    ARTNET,
    SACN,
    AUTO_SCENE
};

// DMX transport config — fixture-specific parameters live in each fixture's own config struct.
struct DmxConfig {
    uint16_t dmxInput;
    bool     dmxOutputEnabled;
    uint16_t startUniverse;
    uint8_t  autoSceneSlot;
};

struct SetConfig {
    String ID_fixture;
};

// Master-only device registry struct (compiled only with RAVLIGHT_MASTER)
#ifdef RAVLIGHT_MASTER
struct InfoConfig {
    char id[16];
    char mode[8];
    char ip[16];
    char fw[8];
    float temp;
    uint32_t uptime;
};
extern InfoConfig infoConfig;
#endif

void intiConfig();
void loadDefaultConfig();
void loadConfig();
void saveConfig();
void resetConfig();
void applyConfigJson(DynamicJsonDocument& doc);

#ifdef RAVLIGHT_MODULE_RESET
void checkResetButton();
#endif

extern NetworkConfig netConfig;
extern SetConfig setConfig;
extern DmxConfig dmxConfig;

#endif
