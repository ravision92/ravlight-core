#pragma once
// QuinLED AN-Penta Plus — 5 analog PWM + 1 digital level-shifted + 1 relay
// https://quinled.info/quinled-an-penta-plus-pinout-guide/

#define BOARD_NAME  "QuinLED AN-Penta Plus"
#define HW_VERSION  "v1"

// Hardware capabilities
#define RAVLIGHT_HAS_ETHERNET
// Note: GPIO 36/39/34 are hardware buttons but not wired to the firmware reset module.
// Define RAVLIGHT_HAS_RESET_BUTTON and HW_PIN_RESET to use one as a reset trigger.

// Ethernet — LAN8720, same scheme as QuinLED Dig-Octa
#define ETH_PHY_TYPE    ETH_PHY_LAN8720
#define ETH_PHY_ADDR    0
#define ETH_PHY_MDC     23
#define ETH_PHY_MDIO    18
#define ETH_PHY_POWER   -1
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT

// LED outputs (7 total):
//   CH1-5 (idx 0-4): GPIO 33,32,12,4,2 — analog LEDC PWM
//   CH6   (idx 5):   GPIO 5             — digital level-shifted 5V (WS2812B-capable), default PWM
//   CH7   (idx 6):   GPIO 13            — power relay for CH6 (LED_RELAY)
static const int HW_LED_OUTPUT_PINS[] = {33, 32, 12, 4, 2, 5, 13};
#define HW_LED_OUTPUT_COUNT  7

// Board preset applied on first boot (NVS empty) and after factory reset:
// all outputs default to LED_PWM 1kHz; CH7 (GPIO 13) set as LED_RELAY
#define BOARD_ELYON_PRESET_ALL_PWM
#define BOARD_ELYON_PWM_DEFAULT_FREQ  1000
#define BOARD_ELYON_RELAY_OUTPUT_IDX  6
#define BOARD_ELYON_RELAY_THRESHOLD   128
