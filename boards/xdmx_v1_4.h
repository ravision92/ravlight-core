#pragma once
// XDMX v1.4 — QuinLED-ESP32-AE + HW-0519 RS-485 + 74HCT245 level shifter
// Custom Ravision PCB (KiCad source: R&D/Xlaser/Xlaser3.kicad_*).
// Drives one RS-485 DMX port + 7 5V-level-shifted LED outputs routed to a
// Phoenix screw terminal. The Axon fixture uses the first two of these for
// pixel/PWM/relay outputs and ignores the rest.

#define BOARD_NAME  "XDMX v1.4"
#define HW_VERSION  "HW 1.4"

// Hardware capabilities present on this board
#define RAVLIGHT_HAS_ETHERNET
#define RAVLIGHT_HAS_RS485
// No analog temp sensor / no dedicated reset button footprint on this rev.
// IO35 has a test point (J11) that could host a button; not enforced here.

// Ethernet PHY — LAN8720A via QuinLED-ESP32-AE module.
//
// Verified against the QuinLED-ESP32-Ethernet wiki page. Three things
// differ from the WT32-ETH01 used on XDMX rev2.2 and must not be copied
// over without checking:
//
//   1. PHY_ADDR = 0  (WT32-ETH01 uses 1).
//   2. Clock mode = ETH_CLOCK_GPIO17_OUT — the ESP32 *generates* the
//      50 MHz reference clock for the PHY on GPIO 17 (WT32-ETH01 instead
//      receives an external crystal on GPIO 0). Wrong clock mode → PHY
//      never links and the ETH library silently times out.
//   3. The PHY regulator is OFF at power-up. GPIO 5 must be driven HIGH
//      *before* ETH.begin() — the Arduino ETH library doesn't always do
//      it early enough. network_manager.cpp pulls the pin high
//      explicitly for boards that define BOARD_ETH_POWER_REQUIRES_BOOT.
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_PHY_ADDR   0
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   18
#define ETH_PHY_POWER  5
#define ETH_CLK_MODE   ETH_CLOCK_GPIO17_OUT
#define BOARD_ETH_POWER_REQUIRES_BOOT  5    // GPIO to drive HIGH before ETH init

// RS-485 DMX wired I/O (HW-0519 module on the board)
//   HW1/2 (TXD) ← GPIO 33  — esp_dmx TX
//   HW1/3 (RXD) → GPIO 34  — esp_dmx RX (ESP32 input-only pin, OK for RX)
//   No DE/RE control line: HW-0519 auto-direction, so DMX_EN = -1.
// Note: the board file name says v1.4 but the physically produced and
// tested revision is v1.3. Keeping the file name until we bump to a
// real v1.4 spin. All pin assignments below track the v1.3 PCB.
#define HW_PIN_DMX_TX      33
#define HW_PIN_DMX_RX      34
// MAX485 DE/RE tied together and driven by GPIO 16 (XDMX v1.3 PCB).
// esp_dmx pulls this HIGH before TX and LOW after — so wired-in DMX RX
// works whenever the port is not actively transmitting, without any
// extra logic on our side.
#define HW_PIN_DMX_EN      16

// I²C bus on the J4 pin header — exposed at 3.3 V (no level shifter).
//   IO2 → J4 pin 3 (SDA)
//   IO4 → J4 pin 4 (SCL)
// Used for optional accessories: SSD1309 OLED (RAVLIGHT_MODULE_OLED), PN532
// NFC reader (RAVLIGHT_MODULE_NFC), etc. 5V VCC for the accessory comes
// from J6 / J7 (3-pin JST). If the accessory pulls SDA/SCL up to 5 V, add
// a bidirectional level shifter or move the pull-ups to 3.3 V.
#define HW_PIN_I2C_SDA  2
#define HW_PIN_I2C_SCL  4
#define HW_PIN_OLED_SDA HW_PIN_I2C_SDA
#define HW_PIN_OLED_SCL HW_PIN_I2C_SCL

// Status LED — not present on the bare PCB.
// The QuinLED-ESP32-AE module itself has an onboard LED on GPIO 17 used by
// the Ethernet clock-out path on some variants; we don't claim it here.
// If you wire a status LED externally, redefine HW_PIN_LED_STATUS in the env.

// Addressable LED output index — first two channels of the 74HCT245 level
// shifter. Both come out on the Phoenix terminal J3 at 5 V (74HCT outputs
// drive WS281x logic levels directly off a 3.3 V ESP32 input).
//
// Channel mapping (74HCT245 B-side → A-side → screw terminal):
//   index 0 → GPIO 12 → B1 → A1
//   index 1 → GPIO 13 → B2 → A2
//
// The board level-shifts another 5 GPIOs (14, 15, 16, 32, 0) routed to J3.
// They're left out of HW_LED_OUTPUT_PINS by design: GPIO 0 is bootstrap +
// the Ethernet 50 MHz clock input, GPIO 14/15 are also bootstraps, and
// GPIO 32 is unused. Axon today uses 2 outputs; expanding later is just a
// matter of appending pins here (and not regressing GPIO 0).
static const int HW_LED_OUTPUT_PINS[] = { 12, 13 };
#define HW_LED_OUTPUT_COUNT  2
