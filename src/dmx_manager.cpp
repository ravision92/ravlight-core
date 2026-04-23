#include <ArtnetETH.h>
#include <ESPAsyncE131.h>
#include "config.h"
#include <dmx_manager.h>
#include "settings.h"

#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
#include <esp_dmx.h>
dmx_port_t dmxPort = DMX_NUM_1;
bool dmxIsConnected = false;
#endif

extern bool handleDMXenable;

bool ledState = false;
#define LED_TIMEOUT 1000
unsigned long lastDMXReceivedTime = 0;

ArtnetReceiver artnet;
ArtPollReplyConfig ArtPollConfig;

#define UNIVERSE_COUNT 1
ESPAsyncE131 e131(UNIVERSE_COUNT);

uint8_t dmxBuffer[DMX_BUFFER_SIZE];

void initDmxInputs() {
  pinMode(HW_PIN_LED_STATUS, OUTPUT);
  digitalWrite(HW_PIN_LED_STATUS, LOW);

  switch (dmxConfig.dmxInput) {
    case DMX_PHYSICAL:
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
      initWiredDmx();
#endif
      break;
    case ARTNET:
      initArtnet();
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
      if (dmxConfig.dmxOutputEnabled) initWiredDmx();
#endif
      break;
    case SACN:
      initE131();
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
      if (dmxConfig.dmxOutputEnabled) initWiredDmx();
#endif
      break;
    case AUTO_SCENE:
      break;
    default:
      Serial.println("[DMX] Invalid DMX input type");
      break;
  }
}

void initArtnet() {
  ArtPollConfig.oem      = {0x00FF};
  ArtPollConfig.esta_man = {0x0000};
  ArtPollConfig.status1  = {0x00};
  ArtPollConfig.status2  = {0x08}; // sACN capable
  ArtPollConfig.short_name = setConfig.ID_fixture;
  char longName[64];
  snprintf(longName, sizeof(longName), "Ravision %s %s", PROJECT_NAME, FW_VERSION);
  ArtPollConfig.long_name  = longName;
  ArtPollConfig.node_report = {""};
  ArtPollConfig.sw_in[4]   = {0};

  artnet.begin();
  artnet.setArtPollReplyConfig(ArtPollConfig);
  artnet.subscribeArtDmxUniverse(dmxConfig.startUniverse,
    [&](const uint8_t *data, uint16_t size, const ArtDmxMetadata &metadata, const ArtNetRemoteInfo &remote) {
      for (int i = 1; i < size; i++) {
        dmxBuffer[i] = data[i - 1];
      }
      DMXLedRun();
    });
  Serial.println("[DMX] ArtNet initialized");
}

void initE131() {
  if (e131.begin(E131_MULTICAST, dmxConfig.startUniverse, UNIVERSE_COUNT)) {
    Serial.println("[DMX] sACN/E1.31 initialized");
  } else {
    Serial.println(F("[DMX] sACN/E1.31 init failed"));
  }
}

void receiveDmxData() {
  if (handleDMXenable) {
    switch (dmxConfig.dmxInput) {
      case DMX_PHYSICAL:
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
        getWiredDMX();
#endif
        break;
      case ARTNET:
        getArtnetDMX();
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
        sendDmxData();
#endif
        break;
      case SACN:
        get131DMX();
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL
        sendDmxData();
#endif
        break;
      default:
        break;
    }
  }
  if (millis() - lastDMXReceivedTime >= LED_TIMEOUT) {
    if (ledState) {
      ledState = false;
      digitalWrite(HW_PIN_LED_STATUS, LOW);
    }
  }
}

void getArtnetDMX() {
  artnet.parse();
}

void get131DMX() {
  if (!e131.isEmpty()) {
    e131_packet_t packet;
    e131.pull(&packet);
    for (int i = 0; i < DMX_BUFFER_SIZE; i++) {
      dmxBuffer[i] = packet.property_values[i];
    }
    DMXLedRun();
  }
}

void DMXLedRun() {
  static unsigned long lastToggleTime = 0;
  const unsigned long toggleInterval = 100;

  if (millis() - lastToggleTime >= toggleInterval) {
    ledState = !ledState;
    digitalWrite(HW_PIN_LED_STATUS, ledState ? HIGH : LOW);
    lastToggleTime = millis();
  }
  lastDMXReceivedTime = millis();
}

// --- Physical RS-485 DMX module ---
#ifdef RAVLIGHT_MODULE_DMX_PHYSICAL

void initWiredDmx() {
  dmx_config_t dmx_config = DMX_CONFIG_DEFAULT;
  dmx_personality_t personalities[] = { {1, "Default Personality"} };
  int personality_count = 1;
  dmx_driver_install(dmxPort, &dmx_config, personalities, personality_count);
  dmx_set_pin(dmxPort, HW_PIN_DMX_TX, HW_PIN_DMX_RX, HW_PIN_DMX_EN);
  Serial.println("[DMX] Wired DMX initialized");
}

void getWiredDMX() {
  dmx_packet_t packet;
  if (dmx_receive(dmxPort, &packet, DMX_TIMEOUT_TICK)) {
    if (!packet.err) {
      if (!dmxIsConnected) {
        dmxIsConnected = true;
      }
      dmx_read(dmxPort, dmxBuffer, packet.size);
      DMXLedRun();
    } else {
      Serial.println("[DMX] Packet error received");
    }
  } else if (dmxIsConnected) {
    Serial.println("[DMX] DMX signal lost");
  }
}

// Sends the DMX buffer to the physical output (only if output is enabled and input is not wired DMX)
void sendDmxData() {
  if (dmxConfig.dmxOutputEnabled && dmxConfig.dmxInput != DMX_PHYSICAL) {
    dmx_write(dmxPort, dmxBuffer, DMX_BUFFER_SIZE);
    dmx_send_num(dmxPort, DMX_PACKET_SIZE);
    dmx_wait_sent(dmxPort, DMX_TIMEOUT_TICK);
  }
}

#endif // RAVLIGHT_MODULE_DMX_PHYSICAL

// --- Live reinit helpers (called by /save endpoint without MCU restart) ---

void reinitDMXInput() {
    switch (dmxConfig.dmxInput) {
        case ARTNET: initArtnet(); break;
        case SACN:   initE131();   break;
        default: break;  // DMX_PHYSICAL and AUTO_SCENE need no re-init
    }
    Serial.printf("[DMX] Input reinitialized: mode %d\n", dmxConfig.dmxInput);
}

void reinitUniverse(uint16_t universe) {
    switch (dmxConfig.dmxInput) {
        case ARTNET: initArtnet(); break;  // re-subscribe with new universe
        case SACN:   initE131();   break;  // re-begin with new universe
        default: break;
    }
    Serial.printf("[DMX] Universe reinitialized: %d\n", universe);
}

void reinitDMXOutput(bool enable) {
    // sendDmxData() reads dmxConfig.dmxOutputEnabled on every call — no driver action needed
    Serial.printf("[DMX] Output %s\n", enable ? "enabled" : "disabled");
}
