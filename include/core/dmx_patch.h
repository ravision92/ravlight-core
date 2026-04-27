#pragma once
#include <stdint.h>

// Semantic channel types — shared across all fixtures
typedef enum : uint8_t {
    CH_NONE = 0,
    CH_INTENSITY,
    CH_RED, CH_GREEN, CH_BLUE, CH_WHITE, CH_AMBER, CH_UV,
    CH_STROBE,
    CH_EFFECT,
    CH_SPEED,
    CH_PIXEL_RGB,
    CH_PIXEL_CAST,
    CH_PIXEL_MIRROR,
    CH_PIXEL_GROUP2,
    CH_ACCENT_RGB,
    CH_ACCENT_WHITE,
    CH_PAN, CH_PAN_FINE, CH_TILT, CH_TILT_FINE,
    CH_FOCUS, CH_ZOOM, CH_IRIS, CH_GOBO, CH_GOBO_ROT,
    CH_UART_OUT,
    CH_CUSTOM_1 = 0xF0, CH_CUSTOM_2, CH_CUSTOM_3, CH_CUSTOM_4,
} ch_type_t;

// One entry in a personality's channel table
typedef struct {
    ch_type_t  type;
    uint8_t    id;          // unique ID within the fixture (veyron_ch_id_t etc.)
    uint8_t    section;     // which section_start[] base address to use
    char       name[24];
    uint8_t    default_val;
    uint8_t    snap;        // 0=interpolatable, 1=immediate
    uint8_t    count;       // DMX channels occupied (>1 for pixel blocks)
} dmx_channel_t;

// Personality descriptor
typedef struct {
    char                  name[32];
    uint8_t               ch_count;    // total DMX footprint per section (informational)
    const dmx_channel_t*  channels;
    uint8_t               n_channels;
} personality_t;

// Runtime patch state — one per active fixture
typedef struct {
    const personality_t*  table;             // pointer to fixture's personality array
    uint8_t               personality_idx;   // 0-based index into table
    uint8_t               universe;
    uint16_t              section_start[8];  // base DMX address per section
                                             // Veyron: [0]=strip [1]=accent [2]=strobe
                                             // Elyon:  [0..7]=output 0..7
} patch_state_t;

// Helpers — implemented in src/dmx_patch.cpp
// Return the DMX value for a channel identified by its fixture-local id.
uint8_t        getChannelById(const patch_state_t* state, const uint8_t* dmxBuf, uint8_t id);

// Return pointer to the first byte of a multi-channel block; writes block size to out_count.
const uint8_t* getChannelBlockById(const patch_state_t* state, const uint8_t* dmxBuf,
                                   uint8_t id, uint8_t* out_count);

// Return the default value for a channel id within a personality.
uint8_t        getChannelDefault(const personality_t* pers, uint8_t id);
