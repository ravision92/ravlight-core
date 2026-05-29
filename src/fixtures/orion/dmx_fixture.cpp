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

// ── Motor update task — pinned to core 1, high priority ─────────────────────

static void motorTask(void* arg) {
    (void)arg;
    const TickType_t delay = 1;   // 1 tick = ~1 ms with default config
    for (;;) {
        if (g_driver) g_driver->update();
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
                                   0.11f, HW_MOTOR_UART_BAUD);
    g_driver = g_tmc;

    if (!g_driver->begin()) {
        ESP_LOGE(TAG, "driver begin() failed — check TMC2209 UART wiring");
        delete g_tmc;
        g_tmc = nullptr; g_driver = nullptr;
        return;
    }

    // Apply config to driver — currents and StallGuard are compile-time constants
    g_driver->setRunCurrent(ORION_RUN_CURRENT_MA);
    g_driver->setHoldCurrent(ORION_HOLD_CURRENT_MA);
    g_driver->setStallGuardThreshold(orionConfig.operSgthrs);
    orionApplySoftLimits();
    g_driver->setSpeed((float)orionConfig.maxSpeed);
    g_driver->setAccel((float)orionConfig.maxAccel);

    // Homing config — only direction is user-set; the rest are ORION_HOMING_* constants
    HomingConfig hcfg;
    hcfg.direction     = orionConfig.homingDirection;
    hcfg.speed         = ORION_HOMING_SPEED;
    hcfg.sgthrs        = ORION_HOMING_SGTHRS;
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

    // Transition enabled → disabled: e-stop the motor
    if (g_dmx_was_enabled && !dmx_enabled) {
        if (g_driver) g_driver->estop();
        ESP_LOGW(TAG, "DMX enable=0 — e-stop");
    }
    // Transition disabled → enabled: clear fault if we estopped, otherwise no-op
    else if (!g_dmx_was_enabled && dmx_enabled) {
        if (g_driver) g_driver->clearFault();
        ESP_LOGI(TAG, "DMX enable=1 — fault cleared, motor re-armed");
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
        case OrionFunction::HOMING:
        case OrionFunction::CLEAR_FAULT:
            // Edge-triggered: fire as soon as we entered the range
            ready = true;
            break;
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
            g_driver->setSpeed((float)ORION_HOMING_SPEED);
            g_driver->moveTo(0);
            ESP_LOGW(TAG, "DMX watchdog expired — returning to home (slow speed)");
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

    // Watchdog: if expired, fire configured action once and bail until kick()
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

    // The first valid DMX frame arms the watchdog; subsequent frames just kick it.
    if (ORION_DMX_WATCHDOG_MS > 0 && !g_dmx_wd.isEnabled()) g_dmx_wd.enable();
    g_dmx_wd.kick();
    g_wd_action_fired = false;

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
    // DMX max = upPosition. Travel may be negative if wiring inverted.
    int32_t target;
    if (p == OrionPersonality::BASIC) {
        int64_t travel = (int64_t)orionConfig.upPosition - orionConfig.downPosition;
        target = orionConfig.downPosition + (int32_t)(travel * pos_msb / 255);
    } else {
        uint16_t dmx16 = ((uint16_t)pos_msb << 8) | pos_lsb;
        target = dmx16ToPosition(dmx16);
    }
    g_driver->moveTo(target);
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
    // Visual identify: small jog back and forth
    if (!g_driver) return;
    MotorStatus s = g_driver->getStatus();
    if (s.state == MotorState::FAULT) return;

    int32_t home_pos = s.position;
    g_driver->moveBy(200);
    vTaskDelay(pdMS_TO_TICKS(300));
    g_driver->moveTo(home_pos);
}

// ── Helpers used by webserver.cpp ───────────────────────────────────────────

IMotorDriver* orionGetDriver() { return g_driver; }

// External entry point — same as the static orionApplySoftLimits() but callable
// from webserver.cpp after a config change.
void orionApplySoftLimitsExternal() { orionApplySoftLimits(); }

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

// Re-push the homing config into the TMC2209 backend after the UI changes
// homingDirection. All other homing params are ORION_HOMING_* compile-time constants.
void orionApplyHomingConfig() {
    if (!g_tmc) return;
    HomingConfig hcfg;
    hcfg.direction     = orionConfig.homingDirection;
    hcfg.speed         = ORION_HOMING_SPEED;
    hcfg.sgthrs        = ORION_HOMING_SGTHRS;
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
