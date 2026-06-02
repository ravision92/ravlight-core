#pragma once
#ifdef RAVLIGHT_MODULE_TEST_PATTERN

// Synthetic DMX source for hardware bring-up without an ArtNet/sACN controller.
// Injects a deterministic walking-byte pattern into every registered universe
// at ~40 Hz so that LED driver output (RMT or I2S) can be probed on a logic
// analyzer and decoded frame-by-frame.

void initTestPattern();
void tickTestPattern();

#endif
