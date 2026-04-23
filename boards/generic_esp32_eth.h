#pragma once
// Generic ESP32 + LAN8720 Ethernet board — reference/template.
// Adjust ETH_PHY_* values and HW_PIN_* to match your specific hardware.

#define BOARD_NAME  "Generic-ESP32-ETH"
#define HW_VERSION  "v1"

#define RAVLIGHT_HAS_ETHERNET

// Ethernet PHY — adjust to match your LAN8720 wiring
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_PHY_ADDR   0
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   18
#define ETH_PHY_POWER  -1
#define ETH_CLK_MODE   ETH_CLOCK_GPIO17_OUT

// Adjust these pins to your board layout
#define HW_PIN_LED_STATUS  2
#define HW_PIN_RESET       0
#define HW_PIN_TEMP        -1
#define HW_PIN_DMX_TX      17
#define HW_PIN_DMX_RX      16
#define HW_PIN_DMX_EN      -1
