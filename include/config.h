#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

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

enum FixturePersonality {
    PERSONALITY_1 = 1,
    PERSONALITY_2,
    PERSONALITY_3,
    PERSONALITY_4,
    PERSONALITY_5
};

// Values match HTML form options (1-4)
typedef enum {
    LINEAR = 1,
    SQUARE,
    INVERSE_SQUARE,
    S_CURVE
} DimmingCurve;

struct DmxConfig {
    uint16_t dmxInput;
    bool dmxOutputEnabled;
    uint16_t startUniverse;
    uint16_t RGBWstartAddress;
    uint16_t strobeStartAddress;
    uint16_t WhStartAddress;
    FixturePersonality selectedPersonality = PERSONALITY_1;
};

struct SetConfig {
    uint16_t DimCurves;
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

#ifdef RAVLIGHT_MODULE_RESET
void checkResetButton();
#endif

extern NetworkConfig netConfig;
extern SetConfig setConfig;
extern DmxConfig dmxConfig;

#endif
