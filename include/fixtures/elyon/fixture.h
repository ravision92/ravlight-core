#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON
#include "fixtures/elyon/output_config.h"

// Identity
#define ELYON_FIXTURE_NAME    "Elyon"
#define PROJECT_NAME          "Elyon"
#define FIXTURE_STATUS        "alpha"

#define ELYON_NUM_OUTPUTS         8
#define ELYON_MAX_PIXELS_TOTAL  4096   // firmware budget: sum of all pixel_count values
#define ELYON_MAX_PIXELS_PER_OUT 500   // per-output cap: 300×48 B RMT ≈ 14 KB, leaves heap for web UI
#define ELYON_MAX_UNIVERSES       16   // max distinct universes dmx_receiver tracks

// Fixture runtime config — persisted via fixtureConfigSerialize/Deserialize
struct ElyonConfig {
    elyon_output_cfg_t outputs[ELYON_NUM_OUTPUTS];
};

extern ElyonConfig elyonConfig;

#endif // RAVLIGHT_FIXTURE_ELYON
