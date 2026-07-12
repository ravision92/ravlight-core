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
//   50  – 99   trigger homing               (Enable must = 0, held 5 s)
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
    DO_NOTHING  = 2,   // ignore DMX loss — motor keeps whatever it was doing
};

// Max consecutive auto-rehome attempts before hard FAULT. Resets on successful homing.
#define ORION_MAX_AUTO_REHOME 3

// DMX-loss watchdog timeout (ms). Compile-time constant — not exposed in the web UI
// and not persisted to NVS, so a stale stored value can never silently override it.
// 0 = watchdog disabled.
#define ORION_DMX_WATCHDOG_MS 5000

// Motor tuning — not exposed in the web UI, not persisted to NVS. These depend on
// the winch motor + mechanics; update here once the calibrated values are dialed in.
// Run current. The "overtemp" reports that originally pushed us to 800 mA
// turned out to be a JS label bug mapping the STALL bit to the "overtemp"
// string. 500 mA + CoolStep below keeps the motor cool during continuous
// DMX tracking without losing torque.
// Board file can override these with HW_MOTOR_RUN_CURRENT_MA / HW_MOTOR_HOLD_CURRENT_MA
// to match a different motor's rated current without touching this file.
#ifdef HW_MOTOR_RUN_CURRENT_MA
#  define ORION_RUN_CURRENT_MA  HW_MOTOR_RUN_CURRENT_MA
#else
#  define ORION_RUN_CURRENT_MA      500   // mA — normal-motion RMS current
#endif
// Low hold current — pairs with the boot warmup that calibrates pwm_autoscale
// so the chopper stays quiet even at this level. 50 mA dampens the residual
// StealthChop chopper hum at standstill while keeping minimal holding torque
// (rope+gear reduction does the heavy lifting). Motor stays near ambient.
#ifdef HW_MOTOR_HOLD_CURRENT_MA
#  define ORION_HOLD_CURRENT_MA HW_MOTOR_HOLD_CURRENT_MA
#else
#  define ORION_HOLD_CURRENT_MA      50   // mA — idle hold current
#endif
#define ORION_SGTHRS               50   // operational StallGuard threshold
// StallGuard4 (StealthChop) needs the motor running above ~60 RPM full-step to
// produce a valid SG_RESULT. At 1/16 microstep on a 200-step/rev motor this is
// ~3200 step/s minimum. Lower values give noise (SG ≈ 0-2) and immediate fake
// stall trips. The previous default of 500 step/s was below the valid range.
#define ORION_HOMING_SPEED       3200   // steps/s — homing travel speed (StallGuard-valid)
#define ORION_HOMING_SGTHRS        30   // StallGuard threshold during homing
// StallGuard sensitivity vs. holding torque trade-off: lower homing current
// makes the load estimate swing wider (the motor feels the load instead of
// overpowering it). 300 mA on TMC2209 is the sweet spot — enough torque to
// drive the unloaded winch but the SG drops sharply under real load.
#define ORION_HOMING_CURRENT_MA   300   // mA — reduced current during homing/SGCal
// After the homing stall trip, the back-off needs to clear the end stop
// firmly so subsequent operation has working room. Default 400 steps ≈ 10 cm
// at stepsPerCm ≈ 40 (drum 16 mm, no reduction); scale as needed for other
// mechanics. The motor returns to position counter 0 at the backed-off spot.
#define ORION_HOMING_BACKOFF      400   // steps — retreat from the end stop

// ── Motion envelope hard caps ────────────────────────────────────────────────
//
// Backend clamps for the operator-configurable motion parameters. The UI
// input `max` attributes mirror these via the /api/motor-limits route so
// invalid values never reach the driver. Values are picked to stay well
// inside TMC2209 stall-safe territory for typical stage lift geometry
// (30 mm drum, 10:1 gearing, 200 step/rev NEMA17/23). Tune per board file
// if a specific rig can go faster safely.
#define ORION_MAX_SPEED_STEPS   25000   // steps/s — top of the envelope
#define ORION_MAX_ACCEL_STEPS   60000   // steps/s² — 0.4 s ramp to max
#define ORION_MAX_JOG_STEPS     15000   // steps/s — jog cap (< maxSpeed by design)

// ── Adaptive StallGuard profile ──────────────────────────────────────────────
//
// Single-point homing calibration sets one SGTHRS valid only at homing speed.
// For stall detection during jog / DMX moves we need a speed-dependent
// baseline of "what SG_RESULT looks like under no load" — measured per
// direction (rope going down vs up have different loads on the motor).
// The profile is captured by a sweep wizard: motor runs at each bin speed
// for ~1 s, samples SG, records mean + standard deviation. Runtime detection
// then trips a stall when current SG falls below (bin.mean - N · stddev)
// for sustained samples, at any speed in the valid StallGuard range.

#define ORION_SGP_BINS 8           // number of speed bins per direction

struct SgProfilePoint {
    uint16_t mean   = 0;           // SG_RESULT mean at this bin
    uint16_t stddev = 0;           // SG_RESULT standard deviation
};

struct SgProfile {
    bool     valid       = false;  // false until a successful sweep runs
    uint32_t base_speed  = 0;      // lowest binned step rate (step/s)
    uint32_t speed_step  = 0;      // increment per bin (step/s)
    SgProfilePoint up  [ORION_SGP_BINS];  // SG vs speed, motor turning UP   (+ dir)
    SgProfilePoint down[ORION_SGP_BINS];  // SG vs speed, motor turning DOWN (- dir)
};

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

    // DMX position mapping orientation. false: DMX 0 → downPosition, 255 → upPosition.
    // true: inverted so DMX 0 sits at the homing side (typical user expectation:
    // "0 = retracted/safe, 255 = fully extended").
    bool     dmxInvertPosition = false;

    // Safety — watchdog timeout is the compile-time ORION_DMX_WATCHDOG_MS constant
    uint8_t  dmxWatchdogAction = (uint8_t)OrionWatchdogAction::ESTOP;

    // Auto-rehome after a stall during a DMX move. Disabled by default.
    // Only acts on STALL faults during MOVING state (not jog, not hardware faults).
    // After ORION_MAX_AUTO_REHOME consecutive stalls the fixture goes to hard FAULT
    // and waits for manual clear + homing.
    bool     autoRehomeOnStall = false;

    // Motor currents — set to the motor's rated RMS current. Persisted in NVS so
    // different motors on the same board don't require a firmware rebuild.
    // ORION_RUN/HOLD_CURRENT_MA are the factory defaults (from board file or fixture.h).
    uint16_t runCurrentMa  = ORION_RUN_CURRENT_MA;
    uint16_t holdCurrentMa = ORION_HOLD_CURRENT_MA;

    // StallGuard operating threshold — tuned by the calibration wizard against the
    // real hung load; persisted in NVS. ORION_SGTHRS is only the factory default.
    uint8_t  operSgthrs = ORION_SGTHRS;

    // Adaptive SG profile — captured by the sweep wizard, used at runtime for
    // speed-aware stall detection. valid=false until the wizard completes.
    SgProfile sgProfile;

    // First-run setup wizard completion flag. False until the operator
    // finishes the guided 5-step procedure (or skips). The UI uses this
    // to decide whether to surface the onboarding modal on load.
    bool     setupComplete = false;

    // Manual override mode — disables every automatic safety feature:
    //   - StallGuard trip is ignored (no fault, no auto-rehome)
    //   - DMX watchdog action is skipped on DMX loss
    //   - The /home route refuses to run automatic homing
    // Only manual jog and manual set-home remain. Intended for setups where
    // StallGuard calibration is impossible (fixture already mounted at a
    // travel limit, unknown motor characteristics, load too heavy for the
    // default hold current). The operator takes full responsibility for
    // stall protection while this mode is active.
    bool     manualMode = false;

    // Auto-home at boot. When true, after homeAtBootDelayMs the motor task
    // treats the CURRENT physical position as home (setHomePosition() →
    // position=0, _homed=true). Intended for direct-drive rigs where the
    // load drops off-power and mechanical homing is not practical when
    // the fixture is deployed. The delay lets any residual load motion
    // settle before zeroing.
    bool     homeAtBoot        = false;
    uint16_t homeAtBootDelayMs = 1500;   // 100..5000

    // Keep the homed flag through a STALL fault. On direct-drive rigs a
    // stall means the shaft slipped → position untrustworthy → re-home
    // required (default). Worm-gear / self-locking drives can't back-drive
    // under load, so the position reference physically survives a stall
    // and forcing a re-home is just friction for the operator.
    bool     keepHomeOnStall = false;

    // Drop-and-rehome recovery after a STALL fault. When true, and
    // autoRehomeOnStall is also true, the recovery flow is:
    //    clearFault() → driverOff() → wait dropWaitMs → driverOn() → setHomePosition()
    // Only makes sense on direct-drive rigs whose load falls to a known
    // physical resting point (e.g. floor / hardstop). On worm-gear rigs
    // the load doesn't drop so the wait is wasted; leave dropAndRehome=false.
    bool     dropAndRehome = false;
    uint16_t dropWaitMs    = 3000;       // 100..10000

    // StallGuard profile confidence — how many sigmas below the mean the
    // trip line sits (higher = more tolerant). Applied at sweep completion
    // when deriving operSgthrs from the captured profile.
    //   2 = Aggressive (trips easily, catches real stalls fastest)
    //   3 = Balanced (default, was hardcoded before)
    //   4 = Tolerant
    //   5 = Very tolerant (small-motor / direct-drive rigs)
    uint8_t  sgConfidenceSigma = 3;      // range 2..5

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

// Concrete TMC2209 backend accessor — for TMC-specific diagnostics only
// (SG ring-buffer dump). Returns nullptr if the backend isn't TMC2209.
class Tmc2209LocalDriver;
Tmc2209LocalDriver* orionGetTmcDriver();

// Re-pushes homingDirection (and the rest of the homing block) to the TMC2209
// backend at runtime — called by /save when the user changes the direction.
void orionApplyHomingConfig();

// Pushes runCurrentMa / holdCurrentMa from orionConfig to the live driver.
// No-op if the driver is not initialised.
void orionApplyMotorCurrents();

// Clear the active fault and, if autoRehomeOnStall is enabled, automatically
// start homing. Single entry point for both /clearfault UI route and DMX
// function-byte CLEAR_FAULT. Returns false if clearFault() refused.
bool orionClearFaultAndMaybeHome();

// Set + persist the StallGuard operating threshold (from the calibration wizard).
// Clamps to 1..255, pushes to the driver, re-applies homing config, saves NVS.
void orionSetOperSgthrs(uint8_t threshold);

// ── Adaptive SG profile sweep API ────────────────────────────────────────────
// Start a sweep: motor runs at base_speed, base_speed+step, ... base_speed+7*step
// in the given direction (+1 or -1). Each bin samples SG for ~1 s. At the end
// the profile is saved to orionConfig.sgProfile and persisted to NVS.
// Returns false if not idle / not homed / a sweep is already running.
bool orionStartSgSweep(int8_t direction, uint32_t base_speed, uint32_t speed_step);

// Sweep progress for UI polling. Phase 0 = idle, 1 = running, 2 = complete,
// 3 = aborted. current_bin / total_bins lets the UI render a progress bar.
struct OrionSgSweepProgress {
    uint8_t  phase;
    uint8_t  current_bin;
    uint8_t  total_bins;
    int8_t   direction;
    uint16_t last_sg;
    uint32_t base_speed;     // base of bin 0 (step/s) — confirms what's running
    uint32_t speed_step;     // increment per bin (step/s, post-cap)
    bool     capped;         // true if step had to be reduced to fit travel
};
OrionSgSweepProgress orionGetSgSweepProgress();
void orionAbortSgSweep();

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

// Toggle soft-limit enforcement for the next jog session. Used by the
// "override" toggle in the manual jog UI so the operator can drive past the
// saved travel limits to define new ones.
void    orionSetJogIgnoreLimits(bool b);
// Toggle StallGuard stall trip for the next jog session — companion to the
// above, also gated by the operator's "override" toggle. Prevents false
// stall trips when the SG profile isn't calibrated for this rig yet.
void    orionSetJogIgnoreStall(bool b);

#endif // RAVLIGHT_FIXTURE_ORION
