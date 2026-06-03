#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/output_config.h"

// Identity
#define ELYON_FIXTURE_NAME    "Elyon"
#define PROJECT_NAME          "Elyon"
#define FIXTURE_STATUS        "alpha"

#define ELYON_NUM_OUTPUTS         HW_LED_OUTPUT_COUNT
#ifdef RAVLIGHT_MODULE_I2S_LED
// I2S parallel path — chunked DMA, RAM scales with chunk size not strip length.
#define ELYON_MAX_PIXELS_TOTAL   8192  // firmware budget: sum of all pixel_count values
#define ELYON_MAX_PIXELS_PER_OUT 1024  // per-output cap: I2S chunked DMA, O(CHUNK_PX) RAM
#define ELYON_MAX_UNIVERSES        48  // 8 ch × 6 universes (1024 px RGB = 3072 ch/ch)
#else
// RMT default path — per-output buffer in DRAM, conservative caps to leave heap for the web UI.
#define ELYON_MAX_PIXELS_TOTAL   4096  // firmware budget: sum of all pixel_count values
#define ELYON_MAX_PIXELS_PER_OUT  500  // per-output cap: 500×48 B RMT ≈ 24 KB, leaves heap for web UI
#define ELYON_MAX_UNIVERSES        16  // max distinct universes dmx_receiver tracks
#endif

// Fixture runtime config — persisted via fixtureConfigSerialize/Deserialize
struct ElyonConfig {
    led_output_cfg_t outputs[ELYON_NUM_OUTPUTS];
};

extern ElyonConfig elyonConfig;

#endif // RAVLIGHT_FIXTURE_ELYON
