#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/output_config.h"

// Identity
#define ELYON_FIXTURE_NAME    "Elyon"
#define PROJECT_NAME          "Elyon"
#define FIXTURE_STATUS        "alpha"

#define ELYON_NUM_OUTPUTS         HW_LED_OUTPUT_COUNT
#define ELYON_MAX_PIXELS_TOTAL   8192  // firmware budget: sum of all pixel_count values
#define ELYON_MAX_PIXELS_PER_OUT 1024  // per-output cap: I2S chunked DMA, O(CHUNK_PX) RAM
#define ELYON_MAX_UNIVERSES        48  // 8 ch × 6 universes (1024 px RGB = 3072 ch/ch)

// Fixture runtime config — persisted via fixtureConfigSerialize/Deserialize
struct ElyonConfig {
    led_output_cfg_t outputs[ELYON_NUM_OUTPUTS];
};

extern ElyonConfig elyonConfig;

#endif // RAVLIGHT_FIXTURE_ELYON
