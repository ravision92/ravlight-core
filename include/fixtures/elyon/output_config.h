#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON
#include <stdint.h>

// LED strip protocol — determines channels per pixel and RMT timing
typedef enum : uint8_t {
    LED_WS2811  = 0,   // 800 kHz RGB  (same timing as WS2812B on ESP32)
    LED_WS2812B = 1,   // 800 kHz RGB
    LED_SK6812  = 2,   // 800 kHz RGBW (4 channels/pixel)
    LED_WS2814  = 3,   // 800 kHz RGBW (4 channels/pixel, same NZR timing as WS2812B)
    // 4-49 reserved for future pixel protocols
    LED_PWM     = 50,  // LEDC hardware PWM — 1 DMX ch (8-bit) or 2 DMX ch (16-bit MSB+LSB)
    LED_RELAY   = 51,  // GPIO relay/switch: 1 DMX ch, ON when val >= relay_threshold
} led_protocol_t;

// Returns channels per physical pixel (or DMX ch per slot for PWM).
static inline uint8_t led_ch_per_pixel(led_protocol_t p) {
    if (p == LED_PWM || p == LED_RELAY) return 1;
    return (p == LED_SK6812 || p == LED_WS2814) ? 4 : 3;
}

// Per-output runtime configuration.
// Pins are board-defined (HW_LED_OUTPUT_PINS[]) — not stored here.
typedef struct {
    led_protocol_t protocol;       // strip type (or LED_PWM for LEDC dimmer)
    uint16_t       pixel_count;    // physical pixels; 0 = disabled (unused for LED_PWM)
    uint16_t       universe_start; // first ArtNet/sACN universe
    uint16_t       dmx_start;      // starting DMX channel within universe_start (1-512)
    uint8_t        grouping;       // physical pixels sharing one DMX slot (1 = 1:1)
    uint8_t        invert;         // 1 = reverse pixel chain order (unused for LED_PWM)
    uint8_t        brightness;     // 0-255 maximum output brightness
    // color_order[i] = logical source channel for wire byte i.
    // Logical: 0=R 1=G 2=B 3=W.  Example: BRWG → {2,0,3,1}
    // RGB strips use only [0..2]; [3] is ignored.  Unused for LED_PWM.
    uint8_t        color_order[4];
    // PWM-specific fields — valid only when protocol == LED_PWM
    uint16_t       pwm_freq_hz;    // LEDC frequency in Hz; 0 = output disabled
    uint8_t        pwm_curve;      // dimming curve: 0=linear, 1=quadratic γ2.0, 2=cubic γ3.0
    uint8_t        pwm_16bit;      // 0=8-bit (1 DMX ch), 1=16-bit (2 DMX ch MSB+LSB)
    uint8_t        pwm_invert;     // 1 = invert PWM duty (active-low drivers: 0→full, 255→off)
    uint8_t        relay_threshold; // LED_RELAY: DMX value at which GPIO goes HIGH (default 128)
} elyon_output_cfg_t;

// Helpers to convert between color_order array and human-readable string ("RGBW", "BRWG", …)
static inline void color_order_to_str(const uint8_t order[4], uint8_t channels, char out[5]) {
    const char names[] = "RGBW";
    for (uint8_t i = 0; i < channels && i < 4; i++) out[i] = names[order[i] & 3];
    out[channels] = '\0';
}
static inline void color_order_from_str(const char* s, uint8_t out[4]) {
    for (uint8_t i = 0; i < 4; i++) {
        switch (s[i]) {
            case 'R': case 'r': out[i] = 0; break;
            case 'G': case 'g': out[i] = 1; break;
            case 'B': case 'b': out[i] = 2; break;
            case 'W': case 'w': out[i] = 3; break;
            default:            out[i] = i; break;
        }
        if (!s[i]) break;
    }
}

// Number of DMX slots needed for an output.
// For pixel protocols: ceil(pixel_count / grouping). For LED_PWM: 1 (8-bit) or 2 (16-bit).
static inline uint32_t elyon_dmx_slots(const elyon_output_cfg_t* c) {
    if (!c) return 0;
    if (c->protocol == LED_RELAY) return 1;
    if (c->protocol == LED_PWM) {
        if (c->pwm_freq_hz == 0) return 0;
        return c->pwm_16bit ? 2 : 1;
    }
    if (c->pixel_count == 0 || c->grouping == 0) return 0;
    return ((uint32_t)c->pixel_count + c->grouping - 1) / c->grouping;
}

// Total DMX channels consumed by an output (may span multiple universes).
static inline uint32_t elyon_dmx_channels(const elyon_output_cfg_t* c) {
    return elyon_dmx_slots(c) * led_ch_per_pixel(c->protocol);
}

// Number of consecutive universes an output spans.
// flat_last = (dmx_start - 1) + total_channels - 1
// n_universes = (flat_last / 512) - ((dmx_start - 1) / 512) + 1
static inline uint8_t elyon_universe_count(const elyon_output_cfg_t* c) {
    if (!c || elyon_dmx_slots(c) == 0) return 0;
    uint32_t first = c->dmx_start - 1;                        // 0-based flat index
    uint32_t last  = first + elyon_dmx_channels(c) - 1;
    return (uint8_t)(last / 512 - first / 512 + 1);
}

#endif // RAVLIGHT_FIXTURE_ELYON
