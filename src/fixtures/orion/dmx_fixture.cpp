#ifdef RAVLIGHT_FIXTURE_ORION
#include "fixture_config.h"
#include "fixtures/orion/fixture.h"
#include "core/motor/IMotorDriver.h"
#include "core/motor/backends/Tmc2209LocalDriver.h"
#include "config.h"
#include <HardwareSerial.h>
#include "core/motor/utils/WatchdogTimer.h"
#include "dmx_manager.h"
#include "core/led_output.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char* TAG = "ORION";

bool handleDMXenable = true;

// ── State ────────────────────────────────────────────────────────────────────

// We hold both a typed pointer (for TMC-specific calls like setHomingConfig)
// and an interface pointer (for the abstract motion API). Same object, two views.
// dynamic_cast is unavailable under -fno-rtti, so this is the workaround.
static Tmc2209LocalDriver* g_tmc           = nullptr;
static IMotorDriver*       g_driver        = nullptr;
static TaskHandle_t        g_motor_task    = nullptr;
static WatchdogTimer       g_dmx_wd(5000);

#ifdef ORION_HAS_LED
// WS281x LED strip outputs driven alongside the motor (pins: HW_LED_OUTPUT_PINS[]).
static led_output_t ledStrips[HW_LED_OUTPUT_COUNT];
static bool         ledActive[HW_LED_OUTPUT_COUNT];
// Per-output highlight wipe (identification). 0 = inactive, else millis() at start.
static uint32_t     ledHlStart[HW_LED_OUTPUT_COUNT] = {0};
#define ORION_LED_HL_MS 1500
#endif

static bool     g_dmx_was_enabled  = false; // starts false: a boot frame with Enable=0 is not a falling edge → no spurious e-stop
static bool     g_wd_action_fired  = false; // watchdog action fires once per expiry

// Last seen DMX Enable byte — fixture-specific (each fixture interprets Enable
// at its own DMX channel). Exposed via /motorstatus, used to gate jog/setlimit.
// Note: "DMX active" (traffic alive) lives in dmx_manager core — use dmxIsActive().
static uint8_t  g_last_dmx_enable  = 0;
static int32_t  g_dmx_last_target  = INT32_MIN;  // dead-zone tracking for handleDMX

// Manual override: once the operator jogs from the UI, DMX position commands
// are suppressed until /release-dmx is called. Enable/Function bytes still apply
// so the console can still e-stop or trigger homing if needed.
static bool     g_manual_override  = false;

// Map DMX function byte (0-255) to action class — ranges defined in fixture.h
enum class OrionFunction : uint8_t {
    IDLE,
    HOMING,
    CLEAR_FAULT,
    SET_DOWN_LIMIT,
    SET_UP_LIMIT,
};

static OrionFunction decodeFunction(uint8_t v) {
    if (v < 50)  return OrionFunction::IDLE;
    if (v < 100) return OrionFunction::HOMING;
    if (v < 150) return OrionFunction::CLEAR_FAULT;
    if (v < 200) return OrionFunction::SET_DOWN_LIMIT;
    return OrionFunction::SET_UP_LIMIT;
}

// Function-byte hold state: edge-triggered actions fire once on entry;
// limit-set actions fire only if held 5 s while Enable=0.
static OrionFunction g_function_holding   = OrionFunction::IDLE;
static uint32_t      g_function_hold_start = 0;
static bool          g_function_fired      = false;

// Capture current position as a soft limit and persist config.
static void orionSetLimit(bool isUpper);

// Physical clamping bounds — independent of DMX mapping direction.
static inline int32_t orionPhysMin() {
    return orionConfig.downPosition < orionConfig.upPosition
        ? orionConfig.downPosition : orionConfig.upPosition;
}
static inline int32_t orionPhysMax() {
    return orionConfig.downPosition > orionConfig.upPosition
        ? orionConfig.downPosition : orionConfig.upPosition;
}

// Re-apply soft limits to the driver (after a /setlimit or homing direction change).
static void orionApplySoftLimits() {
    if (g_driver) g_driver->setSoftLimits(orionPhysMin(), orionPhysMax());
}

// ── Adaptive SG profile sweep state machine ──────────────────────────────────

namespace {
    enum class SgSweepPhase : uint8_t { IDLE, PRE_POSITION, RUN_BIN, COMPLETE, ABORTED };
    SgSweepPhase g_sweep_phase   = SgSweepPhase::IDLE;
    int8_t       g_sweep_dir     = 1;
    uint32_t     g_sweep_base    = 0;
    uint32_t     g_sweep_step    = 0;
    uint8_t      g_sweep_bin     = 0;
    uint32_t     g_sweep_t0_ms   = 0;
    uint32_t     g_sweep_samples_sum = 0;
    uint32_t     g_sweep_samples_sumsq = 0;
    uint16_t     g_sweep_samples_n   = 0;
    uint16_t     g_sweep_last_sg = 0;
    bool         g_sweep_capped  = false;  // true if step had to be reduced for travel fit
    // Per-bin: 200 ms settle + 500 ms sample. Tight enough that an 8-bin sweep
    // fits inside a 70-80 cm travel range at typical winch speeds (~5-15 cm/s),
    // long enough to gather ~5 SG samples per bin (REG_READ_INTERVAL_MS=100).
    constexpr uint32_t BIN_SETTLE_MS = 200;
    constexpr uint32_t BIN_SAMPLE_MS = 500;
}

bool orionStartSgSweep(int8_t direction, uint32_t base_speed, uint32_t speed_step) {
    if (!g_driver ||
        g_sweep_phase == SgSweepPhase::RUN_BIN ||
        g_sweep_phase == SgSweepPhase::PRE_POSITION) return false;
    MotorStatus s = g_driver->getStatus();
    if (s.state != MotorState::IDLE || !s.homed) return false;
    if (base_speed < 1000 || speed_step < 100) return false;

    // Travel-range fit: each bin runs (BIN_SETTLE_MS + BIN_SAMPLE_MS) at its
    // configured speed. Total distance ≈ bin_time × (N·base + (N·(N-1)/2)·step).
    // If that exceeds 80% of available soft-limit range, cap the step so the
    // top bin fits — the resulting sweep covers the lower speeds, capped on
    // the high end. A flag is exposed via /sgsweep so the UI can warn that
    // SG above the cap won't have a calibrated baseline.
    const uint32_t bin_time_ms = BIN_SETTLE_MS + BIN_SAMPLE_MS;
    const uint32_t travel_steps = (uint32_t)(orionPhysMax() - orionPhysMin());
    const uint64_t tri          = ORION_SGP_BINS * (ORION_SGP_BINS - 1) / 2;
    auto requiredSteps = [&](uint32_t base, uint32_t step) {
        uint64_t sum_speeds = (uint64_t)ORION_SGP_BINS * base + tri * step;
        return (uint32_t)((sum_speeds * bin_time_ms) / 1000);
    };
    uint32_t budget = (uint32_t)((uint64_t)travel_steps * 80 / 100);
    g_sweep_capped = false;
    if (travel_steps > 0 && requiredSteps(base_speed, speed_step) > budget) {
        int64_t lhs = (int64_t)budget * 1000 / bin_time_ms
                    - (int64_t)ORION_SGP_BINS * base_speed;
        int64_t scaled = (tri > 0) ? lhs / (int64_t)tri : 0;
        if (scaled < 100) {
            ESP_LOGW(TAG, "SG sweep infeasible: travel %u steps too short for base %u",
                     (unsigned)travel_steps, (unsigned)base_speed);
            return false;
        }
        speed_step = (uint32_t)scaled;
        g_sweep_capped = true;
        ESP_LOGW(TAG, "SG sweep step capped: %u (top bin %u step/s) — travel %u steps",
                 (unsigned)speed_step,
                 (unsigned)(base_speed + (ORION_SGP_BINS - 1) * speed_step),
                 (unsigned)travel_steps);
    }

    g_sweep_dir   = (direction < 0) ? -1 : +1;
    g_sweep_base  = base_speed;
    g_sweep_step  = speed_step;
    g_sweep_bin   = 0;

    // Bypass soft limits during the whole sweep — the motion may legitimately
    // run all the way to the opposite end stop, and a mid-sweep clamp would
    // freeze the motor at half the bins without ever sampling the rest.
    if (g_tmc) g_tmc->setJogIgnoreLimits(true);

    // Move to the opposite travel limit so the sweep has full travel to run.
    // UP sweep starts at the DOWN limit, and vice versa. Use a faster
    // pre-position speed (maxSpeed) so this phase isn't the bottleneck.
    int32_t start_pos = (g_sweep_dir > 0) ? orionPhysMin() : orionPhysMax();
    float pre_spd = (float)(orionConfig.maxSpeed > base_speed
                            ? orionConfig.maxSpeed : base_speed);
    g_driver->setSpeed(pre_spd);
    g_driver->moveTo(start_pos);
    g_sweep_phase = SgSweepPhase::PRE_POSITION;
    ESP_LOGI(TAG, "SG profile sweep pre-position to %d for dir=%d (spd=%u)",
             start_pos, (int)g_sweep_dir, (unsigned)pre_spd);
    return true;
}

OrionSgSweepProgress orionGetSgSweepProgress() {
    OrionSgSweepProgress p{};
    p.phase       = (uint8_t)g_sweep_phase;
    p.current_bin = g_sweep_bin;
    p.total_bins  = ORION_SGP_BINS;
    p.direction   = g_sweep_dir;
    p.last_sg     = g_sweep_last_sg;
    p.base_speed  = g_sweep_base;
    p.speed_step  = g_sweep_step;
    p.capped      = g_sweep_capped;
    return p;
}

void orionAbortSgSweep() {
    if (g_sweep_phase != SgSweepPhase::RUN_BIN &&
        g_sweep_phase != SgSweepPhase::PRE_POSITION) return;
    if (g_driver) g_driver->stop();
    if (g_tmc)    g_tmc->setJogIgnoreLimits(false);
    g_sweep_phase = SgSweepPhase::ABORTED;
    ESP_LOGW(TAG, "SG profile sweep aborted by user");
}

// Speed-aware stall detection against the calibrated SG profile. Called from
// motorTask each tick whenever the motor is moving under DMX/jog control.
// Trip rule: 5 consecutive samples where SG_RESULT < (bin.mean - 3·stddev).
static void updateProfileStallCheck() {
    if (!g_driver || !orionConfig.sgProfile.valid) return;
    // Suppress the check throughout the whole sweep operation: PRE_POSITION
    // moves the motor across the travel range (often through bins where the
    // profile says one direction is loaded, but we're driving the other), and
    // RUN_BIN is the calibration itself.
    if (g_sweep_phase == SgSweepPhase::PRE_POSITION ||
        g_sweep_phase == SgSweepPhase::RUN_BIN) return;

    MotorStatus s = g_driver->getStatus();
    if (s.state != MotorState::MOVING && s.state != MotorState::JOGGING) return;

    // MotorStatus::speed is already in step/s (not millihertz).
    float spd_abs = (s.speed < 0) ? -s.speed : s.speed;
    uint32_t spd_step_s = (uint32_t)spd_abs;
    constexpr uint32_t MIN_VALID_SPEED = 3000;
    if (spd_step_s < MIN_VALID_SPEED) return;  // SG invalid below ~60 RPM

    const SgProfile& prof = orionConfig.sgProfile;
    if (spd_step_s < prof.base_speed || prof.speed_step == 0) return;

    uint32_t bin = (spd_step_s - prof.base_speed) / prof.speed_step;
    if (bin >= ORION_SGP_BINS) bin = ORION_SGP_BINS - 1;

    bool moving_up = (s.speed > 0);
    const SgProfilePoint& pt = moving_up ? prof.up[bin] : prof.down[bin];
    if (pt.mean == 0) return;  // bin not calibrated for this direction yet

    // Direction-change debounce: a console sinusoid causes the motor to
    // reverse continuously. At each zero-crossing the PWM autoscale + SG
    // need time to settle in the new regime — SG_RESULT temporarily reads
    // low and would spuriously trip the stall check. Ignore samples for a
    // short window after every direction change.
    static bool    last_up    = true;
    static uint32_t turn_ms   = 0;
    if (moving_up != last_up) {
        last_up = moving_up;
        turn_ms = millis();
    }
    constexpr uint32_t DIR_DEBOUNCE_MS = 400;
    if (millis() - turn_ms < DIR_DEBOUNCE_MS) {
        return;
    }

    // Trip threshold: 60% of the calibrated mean, with a floor at
    // (mean - 5·stddev) for bins with unusually wide spread. This is loose
    // enough to ignore normal SG fluctuation (±10-20 around the mean) but
    // still catches a real stall, where SG_RESULT drops near 0.
    uint16_t trip_pct  = (uint16_t)((uint32_t)pt.mean * 6 / 10);
    uint16_t margin5   = (uint16_t)(5 * pt.stddev);
    uint16_t trip_sig  = (pt.mean > margin5) ? (pt.mean - margin5) : 0;
    uint16_t trip      = trip_pct < trip_sig ? trip_pct : trip_sig;

    static uint8_t low_count = 0;
    if (s.sg_result > 0 && s.sg_result < trip) {
        if (++low_count >= 5) {
            g_driver->triggerStall();
            ESP_LOGW(TAG, "profile stall: sg=%u trip=%u (bin=%u dir=%s mean=%u stddev=%u)",
                     (unsigned)s.sg_result, (unsigned)trip, (unsigned)bin,
                     moving_up ? "up" : "down", (unsigned)pt.mean, (unsigned)pt.stddev);
            low_count = 0;
        }
    } else {
        low_count = 0;
    }
}

// Drive the sweep state machine — called from motorTask each tick.
static void updateSgSweep() {
    if (!g_driver) return;

    // Pre-position phase: wait for motor to reach the opposite-limit start
    // position, then transition into bin sampling.
    if (g_sweep_phase == SgSweepPhase::PRE_POSITION) {
        MotorStatus ps = g_driver->getStatus();
        if (ps.state == MotorState::IDLE) {
            g_sweep_samples_n     = 0;
            g_sweep_samples_sum   = 0;
            g_sweep_samples_sumsq = 0;
            g_sweep_t0_ms         = millis();
            g_sweep_phase         = SgSweepPhase::RUN_BIN;
            g_driver->jog(g_sweep_dir, (float)g_sweep_base);
            ESP_LOGI(TAG, "SG profile sweep starting bin 0 at speed %u",
                     (unsigned)g_sweep_base);
        }
        return;
    }
    if (g_sweep_phase != SgSweepPhase::RUN_BIN) return;
    const uint32_t elapsed = millis() - g_sweep_t0_ms;
    MotorStatus s = g_driver->getStatus();
    g_sweep_last_sg = s.sg_result;

    if (elapsed < BIN_SETTLE_MS) return;  // wait for motor to reach speed + autoscale

    // Sample window — collect SG_RESULT into running sums.
    if (elapsed < BIN_SETTLE_MS + BIN_SAMPLE_MS) {
        // Every 100 ms (matches REG_READ_INTERVAL_MS) a fresh SG arrives. Use
        // a simple coalescing: only add if value changed since last add.
        static uint16_t last_added = 0xFFFF;
        if (s.sg_result != last_added) {
            g_sweep_samples_sum   += s.sg_result;
            g_sweep_samples_sumsq += (uint32_t)s.sg_result * s.sg_result;
            g_sweep_samples_n     += 1;
            last_added = s.sg_result;
        }
        return;
    }

    // Bin done — compute mean + stddev, save to profile, advance.
    SgProfilePoint pt{};
    if (g_sweep_samples_n > 0) {
        pt.mean = (uint16_t)(g_sweep_samples_sum / g_sweep_samples_n);
        // variance = E[x²] - E[x]²
        uint32_t sq_mean = g_sweep_samples_sumsq / g_sweep_samples_n;
        uint32_t mean_sq = (uint32_t)pt.mean * pt.mean;
        uint32_t var = (sq_mean > mean_sq) ? (sq_mean - mean_sq) : 0;
        // Approximate sqrt for stddev — int sqrt via Newton iteration.
        uint32_t r = var > 0 ? var : 1, prev = 0;
        for (int i = 0; i < 8 && r != prev; i++) { prev = r; r = (r + var / r) / 2; }
        pt.stddev = (uint16_t)r;
    }
    if (g_sweep_dir > 0) orionConfig.sgProfile.up[g_sweep_bin]   = pt;
    else                 orionConfig.sgProfile.down[g_sweep_bin] = pt;

    ESP_LOGI(TAG, "SG sweep bin %u dir=%d: mean=%u stddev=%u (n=%u)",
             g_sweep_bin, (int)g_sweep_dir, pt.mean, pt.stddev, g_sweep_samples_n);

    g_sweep_bin += 1;
    if (g_sweep_bin >= ORION_SGP_BINS) {
        // Sweep complete — stop motor, restore soft limits, persist profile.
        g_driver->stop();
        if (g_tmc) g_tmc->setJogIgnoreLimits(false);
        orionConfig.sgProfile.valid      = true;
        orionConfig.sgProfile.base_speed = g_sweep_base;
        orionConfig.sgProfile.speed_step = g_sweep_step;

        // Auto-derive the operational SGTHRS from the captured profile.
        // Take the worst-case "free-running floor" across all bins:
        //   floor[i] = mean[i] − 3·stddev[i]    (≈ 99.7 % of free runs above)
        //   operSgthrs = min over (bin, direction) of floor[i]
        // Anything below this floor at the corresponding speed is statistically
        // a real stall, not noise. Clamp to [1, 255]; only update if we have at
        // least one valid bin to avoid clobbering a previously good value.
        int32_t worst_floor = INT32_MAX;
        for (int i = 0; i < ORION_SGP_BINS; i++) {
            const SgProfilePoint* pts[2] = {&orionConfig.sgProfile.up[i],
                                             &orionConfig.sgProfile.down[i]};
            for (const SgProfilePoint* p : pts) {
                if (!p->mean) continue;
                int32_t floor = (int32_t)p->mean - 3 * (int32_t)p->stddev;
                if (floor < worst_floor) worst_floor = floor;
            }
        }
        if (worst_floor != INT32_MAX) {
            if (worst_floor < 1)   worst_floor = 1;
            if (worst_floor > 255) worst_floor = 255;
            orionSetOperSgthrs((uint8_t)worst_floor);
            ESP_LOGI(TAG, "sweep complete; operSgthrs auto-derived = %d", worst_floor);
        }

        saveConfig();
        g_sweep_phase = SgSweepPhase::COMPLETE;
        ESP_LOGI(TAG, "SG profile sweep complete and saved");
        return;
    }

    // Next bin: re-issue jog at the new speed. setSpeed() alone wouldn't
    // restart the motor if it had been stopped by a soft limit or fault,
    // and the previous jog() call's runForward/Backward direction may have
    // been cleared. jog() keeps the state in JOGGING which is what the
    // sweep-pause check expects.
    uint32_t spd = g_sweep_base + (uint32_t)g_sweep_bin * g_sweep_step;
    g_driver->jog(g_sweep_dir, (float)spd);
    g_sweep_samples_sum   = 0;
    g_sweep_samples_sumsq = 0;
    g_sweep_samples_n     = 0;
    g_sweep_t0_ms         = millis();
}

// ── Motor update task — pinned to core 1, high priority ─────────────────────

static void motorTask(void* arg) {
    (void)arg;
    const TickType_t delay = 1;   // 1 tick = ~1 ms with default config
    static bool       was_homing   = false;
    static MotorState prev_state   = MotorState::IDLE;
    static uint8_t    rehome_count = 0;   // consecutive auto-rehome attempts

    for (;;) {
        if (g_driver) {
            g_driver->update();
            updateSgSweep();
            updateProfileStallCheck();

            MotorStatus s = g_driver->getStatus();

            // ── Auto-rehome on stall ─────────────────────────────────────────
            // Conditions: feature enabled, stall-only fault (no HW faults),
            // fault happened from MOVING (DMX-driven, not operator jog),
            // and we haven't exhausted the consecutive retry budget.
            if (s.state == MotorState::FAULT &&
                prev_state == MotorState::MOVING &&
                orionConfig.autoRehomeOnStall &&
                (s.fault_flags & (uint8_t)MotorFault::STALL) &&
               !(s.fault_flags & ((uint8_t)MotorFault::OVERCURRENT |
                                  (uint8_t)MotorFault::OVERTEMP     |
                                  (uint8_t)MotorFault::DRIVER_ERROR))) {

                if (rehome_count < ORION_MAX_AUTO_REHOME) {
                    rehome_count++;
                    ESP_LOGW(TAG, "stall during DMX move — auto-rehoming (attempt %u/%u)",
                             rehome_count, (uint8_t)ORION_MAX_AUTO_REHOME);
                    if (g_driver->clearFault()) {
                        g_driver->startHoming();
                    } else {
                        ESP_LOGE(TAG, "auto-rehome: clearFault refused — staying in FAULT");
                    }
                } else {
                    ESP_LOGE(TAG, "auto-rehome limit reached (%u/%u) — hard FAULT, manual intervention required",
                             rehome_count, (uint8_t)ORION_MAX_AUTO_REHOME);
                }
            }

            // ── Homing completion ────────────────────────────────────────────
            // Auto-set the homing-side soft limit to 0 on rising edge of
            // successful homing. Also resets the auto-rehome counter so a
            // fresh stall sequence gets the full budget again.
            bool homing_now = (s.state == MotorState::HOMING);
            if (was_homing && !homing_now && s.homed) {
                if (orionConfig.homingDirection < 0) orionConfig.downPosition = 0;
                else                                 orionConfig.upPosition   = 0;
                orionApplySoftLimitsExternal();
                saveConfig();
                rehome_count = 0;
                ESP_LOGI(TAG, "homing complete — auto-set %s limit to 0, rehome counter reset",
                         orionConfig.homingDirection < 0 ? "DOWN" : "UP");
            }
            was_homing = homing_now;
            prev_state = s.state;
        }
        vTaskDelay(delay);
    }
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void initFixture() {
    ESP_LOGI(TAG, "Orion fixture init — personality=%u positionStart=%u controlStart=%u universe=%u",
             orionConfig.personality, orionConfig.positionStart,
             orionConfig.controlStart, dmxConfig.startUniverse);

    registerDmxUniverse(dmxConfig.startUniverse);

#ifdef ORION_HAS_LED
    // LED strip outputs — init BEFORE the motor so they work even if the motor
    // driver fails to init (e.g. TMC2209 UART issue). Pixel protocols only.
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        ledActive[i] = false;
        const led_output_cfg_t& cfg = orionConfig.ledOutputs[i];
        if (cfg.pixel_count == 0) continue;                                  // disabled
        if (cfg.protocol == LED_PWM || cfg.protocol == LED_RELAY) continue;  // pixel outputs only
        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);
        esp_err_t err = led_output_init(&ledStrips[i], HW_LED_OUTPUT_PINS[i], cfg.pixel_count,
                                        (rmt_channel_t)i, 1, ch_pp);
        if (err == ESP_OK) {
            ledActive[i] = true;
            uint8_t n = led_universe_count(&cfg);
            for (uint8_t u = 0; u < n; u++) registerDmxUniverse(cfg.universe_start + u);
            ESP_LOGI(TAG, "LED ch%d gpio%d n=%d proto=%d univ=%d ch=%d", i,
                     HW_LED_OUTPUT_PINS[i], cfg.pixel_count, cfg.protocol,
                     cfg.universe_start, cfg.dmx_start);
        } else {
            ESP_LOGE(TAG, "LED ch%d init failed err=%d", i, err);
        }
    }
#endif

    // Construct the TMC2209 backend directly — board file dictates the backend.
    // Future fixtures with runtime-selectable backends will use MotorDriverFactory.
    g_tmc = new Tmc2209LocalDriver(Serial2,
                                   HW_MOTOR_TMC_ADDRESS,
                                   HW_PIN_MOTOR_STEP, HW_PIN_MOTOR_DIR,  HW_PIN_MOTOR_EN,
                                   HW_PIN_MOTOR_RX,   HW_PIN_MOTOR_TX,   HW_PIN_MOTOR_DIAG,
                                   HW_MOTOR_R_SENSE, HW_MOTOR_UART_BAUD);
    g_driver = g_tmc;

    if (!g_driver->begin()) {
        ESP_LOGE(TAG, "driver begin() failed — check TMC2209 UART wiring");
        delete g_tmc;
        g_tmc = nullptr; g_driver = nullptr;
        return;
    }

    // Apply config to driver — currents from NVS config (factory defaults if never saved)
    g_driver->setRunCurrent(orionConfig.runCurrentMa);
    g_driver->setHoldCurrent(orionConfig.holdCurrentMa);
    g_driver->setStallGuardThreshold(orionConfig.operSgthrs);
    orionApplySoftLimits();
    g_driver->setSpeed((float)orionConfig.maxSpeed);
    g_driver->setAccel((float)orionConfig.maxAccel);

    // Homing config — only direction is user-set; the rest are ORION_HOMING_* constants
    HomingConfig hcfg;
    hcfg.direction     = orionConfig.homingDirection;
    hcfg.speed         = ORION_HOMING_SPEED;
    // Use the calibrated operating threshold for the homing trip too — the
    // ORION_HOMING_SGTHRS compile-time default is only a fallback if the user
    // never ran SGCal. Anything below ~10 means "uncalibrated, use default".
    hcfg.sgthrs        = (orionConfig.operSgthrs >= 10) ? orionConfig.operSgthrs
                                                       : ORION_HOMING_SGTHRS;
    hcfg.op_sgthrs     = orionConfig.operSgthrs;
    hcfg.current_ma    = ORION_HOMING_CURRENT_MA;
    hcfg.backoff_steps = ORION_HOMING_BACKOFF;
    g_tmc->setHomingConfig(hcfg);

    // Watchdog — armed on the first valid DMX frame (see handleDMX), NOT at boot.
    // "No signal yet" must not fault the motor — only "signal lost after it was present".
    if (ORION_DMX_WATCHDOG_MS > 0)
        g_dmx_wd.setTimeout(ORION_DMX_WATCHDOG_MS);

    // Start dedicated motor task on core 1
    xTaskCreatePinnedToCore(motorTask, "orion_motor", 4096, nullptr, 5, &g_motor_task, 1);

    ESP_LOGI(TAG, "Orion ready — motor task pinned to core 1");
}

// ── DMX handling ─────────────────────────────────────────────────────────────

static void applyEnable(uint8_t enable_byte) {
    g_last_dmx_enable = enable_byte;
    bool dmx_enabled  = (enable_byte > 0);

    // Transition enabled → disabled: decelerated stop. Does NOT fault the
    // driver — the operator's intent is to suspend the move (e.g. cue swap
    // on the console) and resume cleanly when Enable returns to 1. A real
    // safety stop is the E-stop button, not the Enable channel.
    if (g_dmx_was_enabled && !dmx_enabled) {
        if (g_driver) g_driver->stop();
        g_dmx_last_target = INT32_MIN;  // force re-issue of moveTo on re-arm
        ESP_LOGI(TAG, "DMX enable=0 — motor stopped (no fault)");
    }
    // Transition disabled → enabled: release any manual override so the
    // console takes control back. Lets the operator hand the rig back to
    // DMX with a quick Enable cycle (0→1) on the console instead of
    // clicking Release to DMX in the web UI.
    else if (!g_dmx_was_enabled && dmx_enabled) {
        if (orionManualOverride()) {
            orionReleaseToDmx();
            ESP_LOGI(TAG, "DMX enable=1 — manual override released by console cycle");
        } else {
            ESP_LOGI(TAG, "DMX enable=1 — motor re-armed");
        }
    }

    g_dmx_was_enabled = dmx_enabled;
}

static void orionSetLimit(bool isUpper) {
    if (!g_driver) return;
    int32_t pos = g_driver->getStatus().position;
    if (isUpper) {
        orionConfig.upPosition = pos;
        ESP_LOGI(TAG, "DMX set UP limit: upPosition = %d steps", pos);
    } else {
        orionConfig.downPosition = pos;
        ESP_LOGI(TAG, "DMX set DOWN limit: downPosition = %d steps", pos);
    }
    orionApplySoftLimits();
    saveConfig();
}

static void applyFunction(uint8_t function_byte, uint8_t enable_byte) {
    OrionFunction current = decodeFunction(function_byte);

    // Range transition resets the hold timer + fired latch
    if (current != g_function_holding) {
        g_function_holding    = current;
        g_function_hold_start = millis();
        g_function_fired      = false;
    }

    if (g_function_fired || current == OrionFunction::IDLE) return;

    bool ready = false;
    switch (current) {
        case OrionFunction::CLEAR_FAULT:
            // Edge-triggered: fire as soon as we entered the range
            ready = true;
            break;
        case OrionFunction::HOMING:
        case OrionFunction::SET_DOWN_LIMIT:
        case OrionFunction::SET_UP_LIMIT:
            // Safety: motor must be disarmed AND function held continuously for 5 s
            if (enable_byte == 0 && (millis() - g_function_hold_start) >= 5000) ready = true;
            break;
        default:
            break;
    }

    if (!ready || !g_driver) return;

    switch (current) {
        case OrionFunction::HOMING:
            ESP_LOGI(TAG, "DMX function: trigger homing");
            g_driver->startHoming();
            break;
        case OrionFunction::CLEAR_FAULT:
            ESP_LOGI(TAG, "DMX function: clear fault");
            g_driver->clearFault();
            break;
        case OrionFunction::SET_DOWN_LIMIT:
            orionSetLimit(false);
            break;
        case OrionFunction::SET_UP_LIMIT:
            orionSetLimit(true);
            break;
        default:
            break;
    }
    g_function_fired = true;
}

static void doWatchdogAction() {
    if (!g_driver) return;

    if (orionConfig.dmxWatchdogAction == (uint8_t)OrionWatchdogAction::RETURN_HOME) {
        MotorStatus s = g_driver->getStatus();
        if (s.homed) {
            // Return at the user's configured maxSpeed — the homing speed was
            // a safety default but ends up uncomfortably slow on a fly-out cue
            // recovery. The DMX-loss event is meant to be quick.
            g_driver->setSpeed((float)orionConfig.maxSpeed);
            g_driver->moveTo(0);
            ESP_LOGW(TAG, "DMX watchdog expired — returning to home at %u step/s",
                     (unsigned)orionConfig.maxSpeed);
            return;
        }
        ESP_LOGW(TAG, "DMX watchdog expired — not homed, falling back to e-stop");
    }
    g_driver->estop();
    ESP_LOGW(TAG, "DMX watchdog expired — e-stop");
}

// DMX 0 → downPosition, DMX 65535 → upPosition. Travel may be negative if
// upPosition < downPosition (motor wiring inverted) — the math still works.
static int32_t dmx16ToPosition(uint16_t dmx16) {
    if (orionConfig.dmxInvertPosition) dmx16 = 65535 - dmx16;
    int64_t travel = (int64_t)orionConfig.upPosition - orionConfig.downPosition;
    int64_t step   = (int64_t)orionConfig.downPosition + (travel * dmx16 / 65535);
    return (int32_t)step;
}

#ifdef ORION_HAS_LED
// Render the LED strip outputs from the universe pool — Elyon-style multi-universe
// math with per-channel universe resolution (handles pixels straddling a 512-ch
// boundary). Runs every frame, independent of motor state / driver presence. No
// mutex: the ArtNet/sACN receive task may write concurrently — worst case a single
// frame's tear, invisible on LEDs.
static void renderLedOutputs() {
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++) {
        if (!ledActive[i]) continue;
        const led_output_cfg_t& cfg = orionConfig.ledOutputs[i];
        uint8_t ch_pp = led_ch_per_pixel(cfg.protocol);

        // ── Highlight wipe (identification): a white band sweeps the strip ───────
        if (ledHlStart[i]) {
            uint32_t el = millis() - ledHlStart[i];
            if (el < ORION_LED_HL_MS) {
                uint16_t n = cfg.pixel_count;
                uint16_t head = (uint16_t)((uint32_t)el * n / ORION_LED_HL_MS);
                uint16_t band = n / 12; if (band < 1) band = 1;
                uint8_t on[4]  = {255, 255, 255, 255};
                uint8_t off[4] = {0, 0, 0, 0};
                for (uint16_t px = 0; px < n; px++) {
                    bool lit = (px <= head) && (px + band >= head);
                    led_output_write_raw(&ledStrips[i], px, lit ? on : off);
                }
                continue;   // skip DMX render for this output this frame
            }
            ledHlStart[i] = 0;  // wipe finished → resume DMX
        }

        const uint8_t* cached_ubuf = nullptr;
        uint16_t       cached_univ = 0xFFFF;

        for (uint16_t px = 0; px < cfg.pixel_count; px++) {
            uint32_t slot = px / cfg.grouping;
            uint8_t  logical[4] = {0, 0, 0, 0};
            bool     skip = false;
            for (uint8_t c = 0; c < ch_pp; c++) {
                uint32_t ch_flat   = (uint32_t)(cfg.dmx_start - 1) + slot * ch_pp + c;
                uint16_t ch_univ   = cfg.universe_start + (uint16_t)(ch_flat / 512);
                uint16_t ch_offset = (uint16_t)(ch_flat % 512);
                if (ch_univ != cached_univ) {
                    cached_ubuf = getUniverseData(ch_univ);
                    cached_univ = ch_univ;
                }
                if (!cached_ubuf) { skip = true; break; }
                logical[c] = (uint16_t)cached_ubuf[ch_offset + 1] * cfg.brightness / 255;
            }
            if (skip) continue;
            uint8_t wire[4];
            for (uint8_t c = 0; c < ch_pp; c++) wire[c] = logical[cfg.color_order[c] & 3];
            uint16_t out_idx = cfg.invert ? (cfg.pixel_count - 1 - px) : px;
            led_output_write_raw(&ledStrips[i], out_idx, wire);
        }
    }
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++)
        if (ledActive[i]) led_output_flush_async(&ledStrips[i]);
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++)
        if (ledActive[i]) led_output_wait_done(&ledStrips[i]);
}

// Start a white-wipe highlight on one LED output (identification).
void orionHighlightLed(int idx) {
    if (idx < 0 || idx >= HW_LED_OUTPUT_COUNT) return;
    if (!ledActive[idx]) return;
    uint32_t t = millis();
    ledHlStart[idx] = t ? t : 1;
}
#endif

void handleDMX() {
    if (!handleDMXenable) return;

#ifdef ORION_HAS_LED
    renderLedOutputs();   // LED outputs render every frame, independent of motor state
#endif

    if (!g_driver) return;   // motor logic below requires the driver

    // Per-universe freshness check FIRST, so a re-arming console (new frame
    // after a watchdog expiry) immediately resets the latched action flag
    // and re-kicks the timer — otherwise the early-return in the next block
    // would freeze us in the post-action state forever.
    uint32_t last_frame = getUniverseLastSeen(dmxConfig.startUniverse);
    static uint32_t prev_seen = 0;
    bool fresh_frame = (last_frame != 0) && (last_frame != prev_seen);
    prev_seen = last_frame;
    if (fresh_frame) {
        if (ORION_DMX_WATCHDOG_MS > 0 && !g_dmx_wd.isEnabled()) g_dmx_wd.enable();
        g_dmx_wd.kick();
        g_wd_action_fired = false;
    }

    // Watchdog: if expired, fire configured action once and bail until a
    // fresh frame above re-kicks it.
    if (g_dmx_wd.isEnabled() && g_dmx_wd.isExpired()) {
        if (!g_wd_action_fired) {
            doWatchdogAction();
            g_wd_action_fired = true;
        }
        return;
    }

    xSemaphoreTake(dmxBufferMutex, portMAX_DELAY);
    const uint8_t* buf = getUniverseData(dmxConfig.startUniverse);
    if (!buf) {
        xSemaphoreGive(dmxBufferMutex);
        return;
    }

    OrionPersonality p = (OrionPersonality)orionConfig.personality;

    // Position block: 1 ch (BASIC) or 2 ch (BASIC_HD / STANDARD)
    uint16_t pb = orionConfig.positionStart;
    uint8_t  pb_size = (p == OrionPersonality::BASIC) ? 1 : 2;
    if (pb < 1 || pb + pb_size - 1 > 512) {
        xSemaphoreGive(dmxBufferMutex);
        return;
    }

    // Control block: 3 ch (BASIC, BASIC_HD) or 4 ch (STANDARD)
    uint16_t cb = orionConfig.controlStart;
    uint8_t  cb_size = (p == OrionPersonality::STANDARD) ? 4 : 3;
    if (cb < 1 || cb + cb_size - 1 > 512) {
        xSemaphoreGive(dmxBufferMutex);
        return;
    }

    // (Watchdog arm + kick + flag reset already handled above so the
    // expired-action branch can short-circuit cleanly.)

    uint8_t pos_msb    = buf[pb + 0];
    uint8_t pos_lsb    = (pb_size == 2) ? buf[pb + 1] : 0;

    uint8_t enable_b   = buf[cb + 0];
    uint8_t speed_b    = buf[cb + 1];
    uint8_t accel_b    = 0;
    uint8_t function_b = 0;
    if (p == OrionPersonality::STANDARD) {
        accel_b    = buf[cb + 2];
        function_b = buf[cb + 3];
    } else {
        function_b = buf[cb + 2];
    }

    xSemaphoreGive(dmxBufferMutex);

    // Apply enable first (may e-stop on falling edge, clearFault on rising)
    applyEnable(enable_b);

    // Function byte: edge-triggered for homing/clearfault; 5 s hold + Enable=0
    // gate for SET_DOWN_LIMIT / SET_UP_LIMIT — must run regardless of enable state
    applyFunction(function_b, enable_b);

    if (enable_b == 0) return;

    // Manual override (operator jogged from UI) suppresses DMX position commands
    // until /release-dmx is called. Enable/Function bytes above still apply.
    if (g_manual_override) return;

    MotorStatus ms = g_driver->getStatus();

    // Ignore DMX position commands until the motor is homed: before homing the
    // step counter has no physical reference, so a position move could drive the
    // winch into a hard stop. Enable + Function bytes above still apply, so homing
    // can be triggered via DMX. Warn once until homing completes.
    if (!ms.homed) {
        static bool unhomedWarned = false;
        if (!unhomedWarned) {
            ESP_LOGW(TAG, "DMX position ignored — motor not homed");
            unhomedWarned = true;
        }
        return;
    }

    // Skip motion commands while homing, faulted, or driver disabled
    if (ms.state == MotorState::HOMING || ms.state == MotorState::JOGGING ||
        ms.state == MotorState::FAULT  || ms.state == MotorState::DRIVER_OFF) return;

    // Speed override (0 = use configured max)
    if (speed_b > 0) {
        g_driver->setSpeed((float)orionConfig.maxSpeed * speed_b / 255.0f);
    } else {
        g_driver->setSpeed((float)orionConfig.maxSpeed);
    }

    // Acceleration override (STANDARD only). 0 = configured max.
    if (p == OrionPersonality::STANDARD && accel_b > 0) {
        g_driver->setAccel((float)orionConfig.maxAccel * accel_b / 255.0f);
    }

    // Position target — width depends on personality. DMX 0 = downPosition,
    // DMX max = upPosition (swapped when dmxInvertPosition is set).
    int32_t target;
    if (p == OrionPersonality::BASIC) {
        uint8_t v = orionConfig.dmxInvertPosition ? (uint8_t)(255 - pos_msb) : pos_msb;
        int64_t travel = (int64_t)orionConfig.upPosition - orionConfig.downPosition;
        target = orionConfig.downPosition + (int32_t)(travel * v / 255);
    } else {
        uint16_t dmx16 = ((uint16_t)pos_msb << 8) | pos_lsb;
        target = dmx16ToPosition(dmx16);
    }
    // Dead zone: avoid re-issuing moveTo for small target changes between DMX
    // frames. FastAccelStepper recomputes the ramp on every call, and at 40+
    // fps that drives the motor to vibrate when the console sends a smoothly
    // varying signal (e.g. a small-amplitude sinusoid). 5 mm threshold —
    // imperceptible at the rope tip but kills the jitter. Reset to sentinel
    // on Enable=0 so a re-arm forces the motor to chase the current target.
    const int32_t dead = (int32_t)(orionStepsPerCm() * 0.5f);  // ≈ 5 mm
    if (g_dmx_last_target == INT32_MIN || abs(target - g_dmx_last_target) > dead) {
        g_driver->moveTo(target);
        g_dmx_last_target = target;
    }
}

void startDMX() {
    handleDMXenable = true;
}

void stopDMX() {
    handleDMXenable = false;
    if (g_driver) g_driver->stop();
    g_dmx_wd.disable();
#ifdef ORION_HAS_LED
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++)
        if (ledActive[i]) { led_output_clear(&ledStrips[i]); led_output_flush_async(&ledStrips[i]); }
    for (int i = 0; i < HW_LED_OUTPUT_COUNT; i++)
        if (ledActive[i]) led_output_wait_done(&ledStrips[i]);
#endif
}

void fixtureHighlight() {
    // Visual identify: a small back-and-forth at 5% of the configured travel
    // range. Picks a direction away from whichever soft limit is closer, so
    // there's always room. Enters manual override for the duration so a live
    // DMX stream can't overwrite the wiggle with its own position commands.
    if (!g_driver) return;
    MotorStatus s = g_driver->getStatus();
    if (s.state == MotorState::FAULT) return;

    int32_t travel = orionPhysMax() - orionPhysMin();
    if (travel <= 0) return;
    int32_t delta = travel * 5 / 100;
    if (delta < 100) delta = 100;
    // Direction: away from the closer limit.
    int32_t dist_to_min = s.position - orionPhysMin();
    int32_t dist_to_max = orionPhysMax() - s.position;
    int32_t signed_delta = (dist_to_min < dist_to_max) ? delta : -delta;

    bool was_override = g_manual_override;
    g_manual_override = true;            // suppress DMX position commands

    int32_t home_pos = s.position;
    g_driver->setSpeed((float)orionConfig.maxSpeed);
    g_driver->moveBy(signed_delta);
    vTaskDelay(pdMS_TO_TICKS(800));
    g_driver->moveTo(home_pos);
    vTaskDelay(pdMS_TO_TICKS(800));

    g_manual_override = was_override;    // restore (don't trap user in override)
}

// ── Helpers used by webserver.cpp ───────────────────────────────────────────

IMotorDriver* orionGetDriver() { return g_driver; }

// External entry point — same as the static orionApplySoftLimits() but callable
// from webserver.cpp after a config change.
void orionApplySoftLimitsExternal() { orionApplySoftLimits(); }

void orionSetJogIgnoreLimits(bool b) {
    if (g_tmc) g_tmc->setJogIgnoreLimits(b);
}

// "Override" implies BOTH limits AND stall detection are suspended: the
// operator is redefining limits with a possibly-uncalibrated SG profile,
// so the static operSgthrs would false-trip on transient bumps.
void orionSetJogIgnoreStall(bool b) {
    if (g_tmc) g_tmc->setJogIgnoreStall(b);
}

// Status accessors for webserver / UI
uint8_t orionDmxLastEnable() { return g_last_dmx_enable; }
bool    orionManualOverride() { return g_manual_override; }

// Called by /jog endpoint after a successful jog start.
void    orionEnterManualOverride() { g_manual_override = true; }

// Called by /release-dmx. Halts any ongoing jog and resumes DMX control.
void    orionReleaseToDmx() {
    if (g_driver) g_driver->stop();
    g_manual_override = false;
    ESP_LOGI(TAG, "manual override released — DMX motion resumes");
}

void orionApplyMotorCurrents() {
    if (!g_driver) return;
    g_driver->setRunCurrent(orionConfig.runCurrentMa);
    g_driver->setHoldCurrent(orionConfig.holdCurrentMa);
    ESP_LOGI(TAG, "motor currents updated: run=%u mA hold=%u mA",
             orionConfig.runCurrentMa, orionConfig.holdCurrentMa);
}

// Re-push the homing config into the TMC2209 backend after the UI changes
// homingDirection. All other homing params are ORION_HOMING_* compile-time constants.
void orionApplyHomingConfig() {
    if (!g_tmc) return;
    HomingConfig hcfg;
    hcfg.direction     = orionConfig.homingDirection;
    hcfg.speed         = ORION_HOMING_SPEED;
    // Use the calibrated operating threshold for the homing trip too — the
    // ORION_HOMING_SGTHRS compile-time default is only a fallback if the user
    // never ran SGCal. Anything below ~10 means "uncalibrated, use default".
    hcfg.sgthrs        = (orionConfig.operSgthrs >= 10) ? orionConfig.operSgthrs
                                                       : ORION_HOMING_SGTHRS;
    hcfg.op_sgthrs     = orionConfig.operSgthrs;
    hcfg.current_ma    = ORION_HOMING_CURRENT_MA;
    hcfg.backoff_steps = ORION_HOMING_BACKOFF;
    g_tmc->setHomingConfig(hcfg);
}

// Set + persist the operating StallGuard threshold (from the calibration wizard).
void orionSetOperSgthrs(uint8_t threshold) {
    if (threshold < 1) threshold = 1;
    orionConfig.operSgthrs = threshold;
    if (g_driver) g_driver->setStallGuardThreshold(threshold);
    orionApplyHomingConfig();   // op_sgthrs now follows operSgthrs
    saveConfig();
    ESP_LOGI(TAG, "operating SGTHRS set to %u (calibration)", threshold);
}

#endif // RAVLIGHT_FIXTURE_ORION
