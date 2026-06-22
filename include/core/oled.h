#pragma once
// Optional SSD1309 (128×64 I²C) status display.
//
// Pin map comes from the board file: HW_PIN_OLED_SDA / HW_PIN_OLED_SCL. The
// module is opt-in per env via RAVLIGHT_MODULE_OLED — when not compiled in,
// the public API is no-op stubs so the call sites in main.cpp don't need
// their own #ifdef.

#ifdef RAVLIGHT_MODULE_OLED

// Probe the I²C bus, init the SSD1309 controller, draw the boot splash.
// Safe to call before the network is up. If the panel doesn't ACK, the
// module disables itself silently and subsequent tickOled() calls are
// no-ops — the firmware does not hang or log spam on missing hardware.
void initOled();

// Call from the main loop. Internally throttled to ~4 Hz; safe to call
// every iteration. Cheap when not due (single millis() compare).
void tickOled();

// Diagnostic — returns the last init status line (which I²C addresses
// answered, whether u8g2.begin() succeeded, etc.). Surfaced via the
// webserver's /api/i2c route so a blank panel can be debugged from a
// browser without serial access.
const char* oledDiag();

#else

static inline void initOled() {}
static inline void tickOled() {}
static inline const char* oledDiag() { return "(oled module not compiled)"; }

#endif // RAVLIGHT_MODULE_OLED
