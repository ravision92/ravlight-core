#pragma once
// Gledopto Elite 4D-EXMU — GL-C-618WL
// ESP32 + LAN8720 Ethernet, 4 LED data outputs, function button (reset).
// Same ETH configuration as GL-C-616WL (Elite 2D).

#define BOARD_NAME  "Gledopto Elite 4D"
#define HW_VERSION  "GL-C-618WL"

// Hardware capabilities
#define RAVLIGHT_HAS_ETHERNET
#define RAVLIGHT_HAS_RESET_BUTTON
// No RAVLIGHT_HAS_RS485
// No RAVLIGHT_HAS_TEMP_ANALOG

// Ethernet PHY — LAN8720 (identical to GL-C-616WL)
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_PHY_ADDR   1
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   33
#define ETH_PHY_POWER   5
#define ETH_CLK_MODE   ETH_CLOCK_GPIO0_IN

// Function button — long 10s = factory reset (RAVLIGHT_MODULE_RESET)
#define HW_PIN_RESET   17

// Addressable LED outputs — level-shifted (5V logic via integrated buffer)
// IO16 = CH1, IO12 = CH2, IO4 = CH3, IO2 = CH4
static const int HW_LED_OUTPUT_PINS[] = { 16, 12, 4, 2 };
#define HW_LED_OUTPUT_COUNT  4
