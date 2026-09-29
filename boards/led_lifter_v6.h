#pragma once
// LED Lifter v6 — ESP32-WROOM-32E-N8R2, LAN8720 and TMC2209.
// The module has 8 MB flash and 2 MB in-package PSRAM. GPIO16 is reserved
// for PSRAM and has the required external pull-up on this PCB. PSRAM remains
// disabled in the initial firmware target pending first-article testing.

#define BOARD_NAME  "LED Lifter v6"
#define HW_VERSION  "v6"

#define RAVLIGHT_HAS_ETHERNET
#define RAVLIGHT_HAS_MOTOR

// The 50 MHz oscillator feeds both LAN8720 XTAL1 and ESP32 GPIO0. Unlike v5,
// its OE is controlled by GPIO17, with a hardware-delayed PHY reset. GPIO17
// must be driven high and the reset sequence completed before ETH.begin().
#define ETH_PHY_TYPE    ETH_PHY_LAN8720
#define ETH_PHY_ADDR    0
#define ETH_PHY_MDC     23
#define ETH_PHY_MDIO    18
#define ETH_PHY_POWER   -1
#define ETH_CLK_MODE    ETH_CLOCK_GPIO0_IN
#define BOARD_ETH_OSC_ENABLE_PIN 17

// R41 = 47 kOhm and C38 = 1 uF on the final v6 PCB. Hold OE low long enough
// to discharge the reset capacitor on a software restart, then allow ample
// time for the oscillator and hardware PHY reset to finish before ETH.begin().
#define BOARD_ETH_OSC_LOW_HOLD_MS 150
#define BOARD_ETH_OSC_READY_MS    200

// TMC2209. GPIO12 is a flash-voltage strapping pin; the board holds STEP low
// and keeps the motor driver disabled until the firmware initializes it.
#define HW_PIN_MOTOR_STEP  12
#define HW_PIN_MOTOR_DIR   13
#define HW_PIN_MOTOR_EN    32   // active LOW
#define HW_PIN_MOTOR_RX    33
#define HW_PIN_MOTOR_TX    14
#define HW_PIN_MOTOR_DIAG  34
#define HW_MOTOR_UART_BAUD 50000
#define HW_MOTOR_TMC_ADDRESS 2
// U3/U4 on the final v6 PCB are 0.100-ohm external sense resistors.
#define HW_MOTOR_R_SENSE  0.100f

// LED connector order: LED1=GPIO15, LED2=GPIO4, LED3=GPIO2, LED4=GPIO5.
// GPIO2/5/15 are boot straps; the level-shifter inputs must not override
// their reset state. GPIO16 is reserved for in-package PSRAM.
static const int HW_LED_OUTPUT_PINS[] = { 15, 4, 2, 5 };
#define HW_LED_OUTPUT_COUNT  4
