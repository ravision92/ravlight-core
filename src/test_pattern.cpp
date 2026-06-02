#ifdef RAVLIGHT_MODULE_TEST_PATTERN
#include <Arduino.h>
#include "test_pattern.h"
#include "dmx_manager.h"
#include "esp_log.h"

static const char* TAG = "TEST";

static uint32_t s_lastMs = 0;
static uint8_t  s_counter = 0;
static uint8_t  s_buf[512];

// Frame interval: 25 ms → 40 fps. Slow enough that a logic analyzer triggering
// on the first WS281x bit can capture a full frame; fast enough that a scope
// FFT still shows clean refresh harmonics.
static const uint32_t FRAME_INTERVAL_MS = 25;

// Try this many universe IDs each frame. injectDmxUniverse() is a no-op for
// unregistered universes, so brute-forcing a small range is cheap and avoids
// exposing the universe pool iterator API.
static const uint16_t MAX_PROBED_UNIVERSES = 48;

void initTestPattern() {
    s_lastMs = millis();
    ESP_LOGI(TAG, "Test pattern enabled — walking byte @ 40 fps");
}

void tickTestPattern() {
    uint32_t now = millis();
    if (now - s_lastMs < FRAME_INTERVAL_MS) return;
    s_lastMs = now;
    s_counter++;

    // Walking-byte pattern: channel i = (counter + i) & 0xFF.
    // Every channel has a unique value within a frame, and the value at any
    // channel advances by 1 each frame — both axes are decodable on a scope.
    for (int i = 0; i < 512; i++) s_buf[i] = (uint8_t)(s_counter + i);

    for (uint16_t u = 0; u < MAX_PROBED_UNIVERSES; u++) {
        injectDmxUniverse(u, s_buf, 512);
    }
}

#endif
