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

// Switches the panel to a dedicated "Firmware update" screen and draws it
// immediately (bypasses tickOled()'s normal throttled status rendering,
// which stays suppressed while this mode is active). Call once at OTA
// upload start (percent=0) and again on progress; internally time-throttled
// so frequent calls from the upload chunk handler don't hammer the I²C bus.
// percent<0 draws an indeterminate "please wait" screen.
void oledShowOtaProgress(int percent);

// Leaves OTA mode — tickOled() resumes normal status rendering on the next
// call. Call on OTA failure (a successful OTA reboots the device anyway).
void oledOtaEnd();

#else

static inline void initOled() {}
static inline void tickOled() {}
static inline const char* oledDiag() { return "(oled module not compiled)"; }
static inline void oledShowOtaProgress(int) {}
static inline void oledOtaEnd() {}

#endif // RAVLIGHT_MODULE_OLED
