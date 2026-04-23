#pragma once
// QuinLED Dig-Octa v3 — ESP32, WiFi-only, 8 independent LED output channels.
// Reference board for WiFi-only fixture builds. No Ethernet, no RS-485.

#define BOARD_NAME  "QuinLED-Dig-Octa"
#define HW_VERSION  "v3"

// No RAVLIGHT_HAS_ETHERNET → network_manager uses WiFi only
// No RAVLIGHT_HAS_RS485   → DMX physical module not available

#define RAVLIGHT_HAS_RESET_BUTTON

// BOOT button acts as reset
#define HW_PIN_RESET     0

// 8 LED output channels
#define HW_PIN_LED_CH1   2
#define HW_PIN_LED_CH2   4
#define HW_PIN_LED_CH3   5
#define HW_PIN_LED_CH4  12
#define HW_PIN_LED_CH5  13
#define HW_PIN_LED_CH6  14
#define HW_PIN_LED_CH7  15
#define HW_PIN_LED_CH8  16
