#pragma once
#ifdef RAVLIGHT_FIXTURE_AXON
#include <stdint.h>

// Axon fixture — channel IDs
// section 0 = RS-485 output
typedef enum : uint8_t {
    ID_AXON_PASSTHROUGH = 0x01,   // 512ch raw DMX passthrough
    ID_AXON_UNIVERSE_A  = 0x02,   // input universe A (ArtNet/sACN)
    ID_AXON_UNIVERSE_B  = 0x03,   // input universe B (HTP merge source)
} axon_ch_id_t;

#endif // RAVLIGHT_FIXTURE_AXON
