#pragma once
#ifdef RAVLIGHT_FIXTURE_ORION
#include <stdint.h>
#include "core/output_config.h"

// Orion optionally drives WS281x LED strips alongside the motor — enabled when the
// board file defines LED output pins. Config + render reuse the shared led_output_cfg_t.
#if defined(HW_LED_OUTPUT_COUNT) && (HW_LED_OUTPUT_COUNT > 0)
#define ORION_HAS_LED 1
#endif

// Identity
#define ORION_FIXTURE_NAME    "Orion"
#define PROJECT_NAME          "Orion"
#define FIXTURE_STATUS        "alpha"

// ── DMX personalities ────────────────────────────────────────────────────────
//
// Position is 8-bit in P1 (0-255 across travel) or 16-bit in P2/P3 (MSB+LSB).
// Either way it maps linearly to [soft_min, soft_max].
//
// Function byte ranges:
//   0   – 49   idle / clear pending function
//   50  – 99   trigger homing               (edge-triggered)
//   100 – 149  clear fault                  (edge-triggered)
//   150 – 199  set DOWN limit               (Enable must = 0, held 5 s)
//   200 – 255  set UP limit                 (Enable must = 0, held 5 s)
//
// Enable byte: 0 = motor disarmed (estop on falling edge, also gate for limit-set
// commands), 1+ = armed. Holding Enable at 0 is the fail-safe DMX default.

enum class OrionPersonality : uint8_t {
    BASIC    = 1,   // 4 ch: enable, pos_8bit, speed, function
    BASIC_HD = 2,   // 5 ch: enable, pos_MSB, pos_LSB, speed, function
    STANDARD = 3,   // 6 ch: enable, pos_MSB, pos_LSB, speed, accel, function
};

static inline uint8_t orionPersonalityChannelCount(OrionPersonality p) {
    switch (p) {
        case OrionPersonality::BASIC:    return 4;
        case OrionPersonality::BASIC_HD: return 5;
        case OrionPersonality::STANDARD: return 6;
    }
    return 4;
}

// Action on DMX watchdog expiry — what the motor does when DMX traffic stops.
enum class OrionWatchdogAction : uint8_t {
    ESTOP       = 0,   // immediate stop + driver disable
    RETURN_HOME = 1,   // moveTo(0) at homing speed (only if homed; else estop)
};

// DMX-loss watchdog timeout (ms). Compile-time constant — not exposed in the web UI
// and not persisted to NVS, so a stale stored value can never silently override it.
// 0 = watchdog disabled.
#define ORION_DMX_WATCHDOG_MS 5000

// Motor tuning — not exposed in the web UI, not persisted to NVS. These depend on
// the winch motor + mechanics; update here once the calibrated values are dialed in.
#define ORION_RUN_CURRENT_MA      800   // mA — normal-motion RMS current
#define ORION_HOLD_CURRENT_MA     300   // mA — idle hold current
#define ORION_SGTHRS               50   // operational StallGuard threshold
#define ORION_HOMING_SPEED        500   // steps/s — homing travel speed
#define ORION_HOMING_SGTHRS        30   // StallGuard threshold during homing
#define ORION_HOMING_CURRENT_MA   200   // mA — reduced current during homing
#define ORION_HOMING_BACKOFF      200   // steps — retreat from the end stop

// ── Persistent config — serialised under config["fixture"] ───────────────────

struct OrionConfig {
    // DMX — two independent start addresses inside the global startUniverse:
    //   positionStart : Position MSB (and LSB in P2/P3) at this DMX address
    //   controlStart  : Enable + Speed (+ Accel in P3) + Function block
    // Both 1-indexed within the universe. Universe itself is dmxConfig.startUniverse.
    uint8_t  personality      = (uint8_t)OrionPersonality::BASIC;
    uint16_t positionStart    = 1;
    uint16_t controlStart     = 3;

    // Motion calibration
    // Semantic naming: DOWN = wire unwound (load low), UP = wire collected (load high).
    // Step values may be in any numeric order depending on motor wiring — the DMX
    // mapping uses (upPosition - downPosition) as signed travel, so it works either way.
    // The driver's safety clamp uses min/max derived from these.
    int32_t  downPosition     = 0;       // step count at the DOWN end (DMX 0)
    int32_t  upPosition       = 50000;   // step count at the UP end (DMX max)
    uint32_t maxSpeed         = 2000;    // steps/s (DMX speed=255)
    uint32_t maxAccel         = 1000;    // steps/s² (DMX accel=255)
    uint32_t jogSpeed         = 500;     // steps/s for hold-to-jog calibration moves

    // Mechanical calibration — converts motor steps <-> real fixture travel.
    // steps/cm = motorStepsPerRev x 16 microsteps x gearRatio / (pi x drum circumference)
    uint16_t drumDiameterMm   = 30;      // takeup drum diameter (mm)
    uint16_t motorStepsPerRev = 200;     // motor full steps/rev (1.8deg=200, 0.9deg=400)
    float    gearRatio        = 1.0f;    // motor:drum reduction (1.0 = direct drive)

    // Homing — direction is the only user-set homing param; speed / current /
    // StallGuard / backoff are the ORION_* compile-time constants defined above.
    int8_t   homingDirection  = 0;

    // Safety — watchdog timeout is the compile-time ORION_DMX_WATCHDOG_MS constant
    uint8_t  dmxWatchdogAction = (uint8_t)OrionWatchdogAction::ESTOP;

    // StallGuard operating threshold — tuned by the calibration wizard against the
    // real hung load; persisted in NVS. ORION_SGTHRS is only the factory default.
    uint8_t  operSgthrs = ORION_SGTHRS;

#ifdef ORION_HAS_LED
    // Optional WS281x/PWM/relay LED outputs — Elyon-style per-output config (NVS).
    led_output_cfg_t ledOutputs[HW_LED_OUTPUT_COUNT];
#endif
};

extern OrionConfig orionConfig;

// Steps per cm of real fixture travel, derived from the mechanical calibration.
float orionStepsPerCm();

// Driver accessor — webserver routes use this to query/command the motor.
// May return nullptr if init failed (TMC2209 not responding on UART).
class IMotorDriver;
IMotorDriver* orionGetDriver();

// Re-pushes homingDirection (and the rest of the homing block) to the TMC2209
// backend at runtime — called by /save when the user changes the direction.
void orionApplyHomingConfig();

// Set + persist the StallGuard operating threshold (from the calibration wizard).
// Clamps to 1..255, pushes to the driver, re-applies homing config, saves NVS.
void orionSetOperSgthrs(uint8_t threshold);

#ifdef ORION_HAS_LED
// Start a white-wipe identification on one LED output (no-op if disabled).
void orionHighlightLed(int idx);
#endif

// Recomputes the physical clamp bounds (min/max of downPosition/upPosition)
// and pushes them to the driver. Call after any change to down/up position.
void orionApplySoftLimitsExternal();

// Fixture-specific Enable byte (last seen). UI uses it (combined with the core's
// dmxIsActive()) to decide whether to allow jog/setlimit.
uint8_t orionDmxLastEnable();

// Manual override — DMX position commands suppressed while operator is jogging
// or hasn't yet released back to DMX. Enable/Function bytes still apply.
bool    orionManualOverride();
void    orionEnterManualOverride();
void    orionReleaseToDmx();

#endif // RAVLIGHT_FIXTURE_ORION
