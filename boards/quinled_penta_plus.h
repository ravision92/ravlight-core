#pragma once
// QuinLED AN-Penta Plus — 5 PWM analogici + 1 digital level-shifted + 1 relay
// https://quinled.info/quinled-an-penta-plus-pinout-guide/

#define BOARD_NAME  "QuinLED AN-Penta Plus"
#define HW_VERSION  "v1"

// Hardware capabilities
#define RAVLIGHT_HAS_ETHERNET
// Note: GPIO 36/39/34 sono pulsanti hardware ma non collegati al modulo reset firmware.
// Definire RAVLIGHT_HAS_RESET_BUTTON e HW_PIN_RESET se si vuole usarne uno come reset.

// Ethernet — LAN8720, stesso schema QuinLED Dig-Octa
#define ETH_PHY_TYPE    ETH_PHY_LAN8720
#define ETH_PHY_ADDR    0
#define ETH_PHY_MDC     23
#define ETH_PHY_MDIO    18
#define ETH_PHY_POWER   -1
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT

// LED outputs (7 totali):
//   CH1-5 (idx 0-4): GPIO 33,32,12,4,2 — PWM analogico LEDC
//   CH6   (idx 5):   GPIO 5             — digitale level-shifted 5V (WS2812B-capable), default PWM
//   CH7   (idx 6):   GPIO 13            — relay di potenza per CH6 (LED_RELAY)
static const int HW_LED_OUTPUT_PINS[] = {33, 32, 12, 4, 2, 5, 13};
#define HW_LED_OUTPUT_COUNT  7

// Board preset applicato al primo boot (NVS vuoto) e al factory reset:
// tutte le uscite pre-impostate a LED_PWM 1kHz; CH7 (GPIO 13) impostata come LED_RELAY
#define BOARD_ELYON_PRESET_ALL_PWM
#define BOARD_ELYON_PWM_DEFAULT_FREQ  1000
#define BOARD_ELYON_RELAY_OUTPUT_IDX  6
#define BOARD_ELYON_RELAY_THRESHOLD   128
