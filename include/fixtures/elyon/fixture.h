#pragma once
#ifdef RAVLIGHT_FIXTURE_ELYON

#define ELYON_FIXTURE_NAME     "Elyon"
#define ELYON_FIXTURE_VERSION  "1.0.0"
#define ELYON_NUM_OUTPUTS      8

// Elyon — 8-output LED controller
// Each output is independently configurable: protocol, pin, pixel count,
// universe, DMX start address, channel inversion.
// patch_state_t.section_start[0..7] maps directly to the 8 output DMX starts.

#endif // RAVLIGHT_FIXTURE_ELYON
