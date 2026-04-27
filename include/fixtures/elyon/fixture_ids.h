#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON
#include <stdint.h>

// Elyon fixture — channel IDs
// section index = output index (0..7)
// Each output has its own section_start[] entry.
typedef enum : uint8_t {
    ID_ELYON_OUT0_PIXELS = 0x00,   // output 0 pixel block
    ID_ELYON_OUT1_PIXELS = 0x01,
    ID_ELYON_OUT2_PIXELS = 0x02,
    ID_ELYON_OUT3_PIXELS = 0x03,
    ID_ELYON_OUT4_PIXELS = 0x04,
    ID_ELYON_OUT5_PIXELS = 0x05,
    ID_ELYON_OUT6_PIXELS = 0x06,
    ID_ELYON_OUT7_PIXELS = 0x07,
} elyon_ch_id_t;

#endif // RAVLIGHT_FIXTURE_ELYON
