#pragma once
// Generic ESP32-WROOM bring-up board — no Ethernet, no DMX RS-485, no temp sensor.
// Designed for logic-analyzer validation of the LED output backends (RMT or I2S)
// without needing a controller, network or production-grade hardware.
//
// Pair with -D RAVLIGHT_MODULE_TEST_PATTERN to drive every registered universe
// with a synthetic walking-byte pattern at 40 fps (see test_pattern.cpp).
//
// Pin choices are all output-capable GPIOs on a generic ESP32-WROOM-32 dev board,
// chosen on adjacent header pins so probing 8 channels with a 16-ch analyzer is
// straightforward. None are strapping or flash-shared.

#define BOARD_NAME  "Generic-ESP32-Test"
#define HW_VERSION  "v1"

// No capability flags: this board exposes nothing except LED outputs.

// 8 LED data pins for Elyon. Adjacent on most WROOM dev boards.
static const int HW_LED_OUTPUT_PINS[] = { 18, 19, 21, 22, 23, 25, 26, 27 };
#define HW_LED_OUTPUT_COUNT  8

// Stubs required by core modules that always reference these symbols.
#define HW_PIN_LED_STATUS  2
#define HW_PIN_RESET      -1
#define HW_PIN_TEMP       -1
#define HW_PIN_DMX_TX     -1
#define HW_PIN_DMX_RX     -1
#define HW_PIN_DMX_EN     -1
