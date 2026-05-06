#pragma once
// Gledopto Elite 2D-EXMU — GL-C-616WL
// ESP32 + LAN8720 Ethernet, 2 LED data outputs, function button (reset).
// Reference: https://www.gledopto.eu/gledopto-wled-controller-esp32-elite-2d-exmu_1

#define BOARD_NAME  "Gledopto Elite 2D"
#define HW_VERSION  "GL-C-616WL"

// Hardware capabilities
#define RAVLIGHT_HAS_ETHERNET
#define RAVLIGHT_HAS_RESET_BUTTON
// No RAVLIGHT_HAS_RS485   — no RS-485 on this board
// No RAVLIGHT_HAS_TEMP_ANALOG — no onboard temperature sensor

// Ethernet PHY — LAN8720
// Clock: GPIO 0 receives 50 MHz REF_CLK from the PHY (ETH_CLOCK_GPIO0_IN, same as WT32-ETH01)
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_PHY_ADDR   1
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   33
#define ETH_PHY_POWER   5
#define ETH_CLK_MODE   ETH_CLOCK_GPIO0_IN

// Function button — long 10s = factory reset (RAVLIGHT_MODULE_RESET)
#define HW_PIN_RESET   17

// Addressable LED outputs — level-shifted (5V logic via integrated buffer)
// IO16 = CH1 (connector V+/IO16/GND)
// IO2  = CH2 (connector V+/IO2/GND)
// IO13 is a raw 3.3V GPIO (not under level shifter) — not suitable for LED strips
static const int HW_LED_OUTPUT_PINS[] = { 16, 2 };
#define HW_LED_OUTPUT_COUNT  2
