#pragma once
// LED Lifter v5 — custom board with ESP32-WROOM-32E, LAN8720, TMC2209 on-board
// 8 MB Flash, 2 MB PSRAM, 24 V stepper supply

#define BOARD_NAME  "LED Lifter v5"
#define HW_VERSION  "v5"

// Hardware capabilities
#define RAVLIGHT_HAS_ETHERNET
#define RAVLIGHT_HAS_MOTOR

// Ethernet — LAN8720 (verify ETH_CLK_MODE from schematic; assuming GPIO17_OUT)
#define ETH_PHY_TYPE    ETH_PHY_LAN8720
#define ETH_PHY_ADDR    0
#define ETH_PHY_MDC     23
#define ETH_PHY_MDIO    18
#define ETH_PHY_POWER   -1
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT

// TMC2209 — pins as per LED Lifter v5 schematic
// WARNING: IO12 (STEP) is the ESP32 flash voltage strap pin.
// Keep TMC2209 EN high (driver disabled) until firmware is fully booted.
#define HW_PIN_MOTOR_STEP  12   // MTDI — see IO12 boot note in design doc
#define HW_PIN_MOTOR_DIR   13
#define HW_PIN_MOTOR_EN    32   // active LOW
#define HW_PIN_MOTOR_RX    33   // PDN_UART RX side
#define HW_PIN_MOTOR_TX    14   // PDN_UART TX side
#define HW_PIN_MOTOR_DIAG  34   // input-only; interrupt on stall
#define HW_MOTOR_UART_BAUD 50000
// TMC2209 UART address — set by MS1_AD0 / MS2_AD1 strapping on the schematic.
// LED Lifter v5: MS2_AD1 = +3V, MS1_AD0 = GND → address 2 (confirmed via serial: "addr=2 ok").
#define HW_MOTOR_TMC_ADDRESS 2

// Steps-per-cm is no longer a board constant — it is computed at runtime from the
// user's mechanical calibration (drum diameter / gear ratio / steps-per-rev).
// See orionStepsPerCm() in the Orion fixture.

// ── Addressable LED outputs (optional, Orion drives them alongside the motor) ──
// WS281x strips on these GPIOs, per-output config from the web UI (Elyon-style).
// PLACEHOLDER PINS — confirm against the real LED Lifter v5 routing. Free GPIO
// candidates after excluding: RMII (19,21,22,25,26,27), MDC/MDIO/clk (18,23,17),
// flash (6-11), motor (12,13,14,32,33,34), UART0 serial (1,3). GPIO 0/2/5/15 are
// boot-strap pins (usable as output after boot — don't let the strip pull them at
// reset). Set HW_LED_OUTPUT_COUNT to the number of outputs you actually wire.
static const int HW_LED_OUTPUT_PINS[] = { 4, 16, 5, 15 };
#define HW_LED_OUTPUT_COUNT  4
