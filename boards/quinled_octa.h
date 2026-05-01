#pragma once
// QuinLED Dig-Octa Brainboard-32-8L — ESP32-WROOM-32UE + LAN8720A Ethernet, 8 LED outputs.
// Reference: https://quinled.info/quinled-dig-octa-brainboard-32-8l/

#define BOARD_NAME  "QuinLED Dig-Octa"
#define HW_VERSION  "BB-32-8L"

#define RAVLIGHT_HAS_ETHERNET
// No RAVLIGHT_HAS_RS485   — no RS-485 DMX physical
// No RAVLIGHT_HAS_RESET_BUTTON — GPIO 0 doubles as LED CH1; BOOT behavior handled by bootloader

// Ethernet PHY — LAN8720A
// GPIO 5 is NOT Ethernet power; it is a LED output on this board.
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_PHY_ADDR   1
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   18
#define ETH_PHY_POWER  -1
#define ETH_CLK_MODE   ETH_CLOCK_GPIO17_OUT

// Addressable LED output index — use HW_LED_OUTPUT_PINS[i] in fixture code.
// WARNING: GPIO 0 (CH1) and GPIO 1 (CH2) share the UART0 TX/boot-strapping lines.
// When USB serial is connected during development, RMT on GPIO 1 conflicts with UART0 TX.
// In production (no USB), all 8 channels work correctly.
// During development, use CH3+ (GPIO 2..13) and leave CH1/CH2 at 0 pixels.
static const int HW_LED_OUTPUT_PINS[] = { 0, 1, 2, 3, 4, 5, 12, 13 };
#define HW_LED_OUTPUT_COUNT  8
