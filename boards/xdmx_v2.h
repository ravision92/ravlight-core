#pragma once
// XDMX rev2.2 — WT32-ETH01 + HW-519 RS-485
// Custom Ravision PCB: Ethernet-first, RS-485 DMX, analog temp sensor, reset button.

#define BOARD_NAME  "XDMX v2.2"
#define HW_VERSION  "HW 2.2"

// Hardware capabilities present on this board
#define RAVLIGHT_HAS_ETHERNET
#define RAVLIGHT_HAS_RS485
#define RAVLIGHT_HAS_TEMP_ANALOG
#define RAVLIGHT_HAS_RESET_BUTTON

// Ethernet PHY — LAN8720 via WT32-ETH01
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_PHY_ADDR   1
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   18
#define ETH_PHY_POWER  16
#define ETH_CLK_MODE   ETH_CLOCK_GPIO0_IN

// Board-level pins (hardware-fixed, not user-configurable via UI)
#define HW_PIN_LED_STATUS  17
#define HW_PIN_RESET       36
#define HW_PIN_TEMP        32
#define HW_PIN_DMX_TX      33
#define HW_PIN_DMX_RX      35
#define HW_PIN_DMX_EN      -1
