#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON
#include <stdint.h>

// LED protocol supported per output
typedef enum : uint8_t {
    ELYON_PROTO_WS2811   = 0,
    ELYON_PROTO_WS2812B  = 1,
    ELYON_PROTO_WS2814   = 2,
    ELYON_PROTO_APA102   = 3,
} elyon_protocol_t;

// Per-output runtime configuration — stored in NVS / config JSON
typedef struct {
    elyon_protocol_t protocol;    // LED strip protocol
    uint8_t          pin;         // data pin (clock pin for APA102 via separate field)
    uint8_t          pin_clk;     // clock pin (APA102 only, ignored otherwise)
    uint16_t         pixel_count; // number of pixels on this output
    uint8_t          universe;    // ArtNet/sACN universe for this output
    uint16_t         dmx_start;   // DMX start address (= patch_state_t.section_start[output_idx])
    uint8_t          invert;      // 1 = invert pixel order
} elyon_output_cfg_t;

#endif // RAVLIGHT_FIXTURE_ELYON
