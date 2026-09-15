#pragma once
// LED Lifter v5 — custom board with ESP32-WROOM-32E, LAN8720, TMC2209 on-board
// 8 MB flash, 2 MB PSRAM, 24 V stepper supply.
// The N8R2 module variant includes 2 MB of in-package PSRAM. GPIO16 is used
// internally by that memory and is therefore unavailable as a board I/O.

#define BOARD_NAME  "LED Lifter v5"
#define HW_VERSION  "v5"

// Hardware capabilities
#define RAVLIGHT_HAS_ETHERNET
#define RAVLIGHT_HAS_MOTOR

// Ethernet — LAN8720 with an external 50 MHz oscillator shared by the PHY
// XTAL1/CLKIN input and ESP32 GPIO0. The oscillator is held off while EN is
// low so GPIO0 remains usable as a boot strap and programming input.
//
// This was GPIO17_OUT until somebody read the schematic: that setting clocks
// the MAC from the ESP32's own APLL while the PHY keeps running off the
// board oscillator, so the two ends of an interface that must share a clock
// were running on separate ones. It linked up anyway — two 50 MHz sources
// drift slowly past each other, which looks like a working link until the
// phase walks into the sampling window. Traced and tested on hardware by
// @cutmoney (PR #4).
#define ETH_PHY_TYPE    ETH_PHY_LAN8720
#define ETH_PHY_ADDR    0
#define ETH_PHY_MDC     23
#define ETH_PHY_MDIO    18
#define ETH_PHY_POWER   -1
#define ETH_CLK_MODE    ETH_CLOCK_GPIO0_IN

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
// External current sense resistor. LED Lifter v5 uses a dedicated 0.100 Ω external
// resistor (more stable than the internal reference). Must match the physical part on
// the PCB — wrong value scales all rms_current() calls proportionally (I_actual =
// I_set × R_fw / R_real), leading to real current that differs from the configured mA.
#define HW_MOTOR_R_SENSE  0.100f

// Steps-per-cm is no longer a board constant — it is computed at runtime from the
// user's mechanical calibration (drum diameter / gear ratio / steps-per-rev).
// See orionStepsPerCm() in the Orion fixture.

// ── Addressable LED outputs (optional, Orion drives them alongside the motor) ──
// Physical LED connector order, confirmed against the LED Lifter schematic:
// LED1 = GPIO15, LED2 = GPIO4, LED3 = GPIO2, LED4 = GPIO5.
// GPIO2/5/15 are boot-strap pins, but the board's level-shifter inputs do not
// override their reset state. GPIO16 is unavailable because N8R2 PSRAM uses it.
static const int HW_LED_OUTPUT_PINS[] = { 15, 4, 2, 5 };
#define HW_LED_OUTPUT_COUNT  4
