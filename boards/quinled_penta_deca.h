#pragma once
// QuinLED AN-Penta Deca — 15 uscite PWM analogiche, no Ethernet
// https://quinled.info/quinled-an-penta-deca-pinout-guide/

#define BOARD_NAME  "QuinLED AN-Penta Deca"
#define HW_VERSION  "v1"

// No RAVLIGHT_HAS_ETHERNET — GPIO 17,18,23 usati come LED output, non ETH

// LED outputs (15 totali):
//   CH1-8  (idx 0-7):  GPIO 27,26,25,23,22,21,19,18 — LEDC low-speed  (ch_idx 0-7)
//   CH9-15 (idx 8-14): GPIO 17,14,13,12,5,4,2       — LEDC high-speed (ch_idx 8-14)
// Gruppi timer che condividono la frequenza: LS {0,4},{1,5},{2,6},{3,7}; HS {8,12},{9,13},{10,14}
static const int HW_LED_OUTPUT_PINS[] = {27,26,25,23,22,21,19,18,17,14,13,12,5,4,2};
#define HW_LED_OUTPUT_COUNT  15

// Board preset applicato al primo boot (NVS vuoto) e al factory reset:
// tutte le 15 uscite pre-impostate a LED_PWM 1kHz
#define BOARD_ELYON_PRESET_ALL_PWM
#define BOARD_ELYON_PWM_DEFAULT_FREQ  1000
