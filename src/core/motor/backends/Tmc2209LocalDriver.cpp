#ifdef RAVLIGHT_HAS_MOTOR
#include "core/motor/backends/Tmc2209LocalDriver.h"
#include <Arduino.h>
#include <string.h>
#include <esp_log.h>

static const char* TAG = "TMC2209Local";

// Periodic register read interval (ms)
static constexpr uint32_t REG_READ_INTERVAL_MS = 100;

// Short blank window — covers just the acceleration ramp before SG sampling
// kicks in. The settle-then-watch state machine below handles PWM autoscale.
static constexpr uint32_t HOMING_STALL_BLANK_MS  = 300;

// Consecutive SG samples above trip needed to declare "settled" (free-run
// reached, autoscale converged). At 100 ms sample rate, 10 = 1000 ms of stable
// high SG. Covers PWM autoscale re-tune after a prior stall event so a
// second consecutive homing doesn't false-trip on the transient.
static constexpr uint8_t  HOMING_SG_SETTLE_HIGH  = 10;

// Maximum time we wait for SG to first cross above the trip threshold. If the
// motor is jammed against an end stop from the start, SG never rises, so this
// is the upper bound on how long the motor pushes before we declare stall.
static constexpr uint32_t HOMING_SETTLE_TIMEOUT_MS = 2500;

// Consecutive SG samples below trip required to declare a stall once settled.
// At 100 ms sample rate this is 500 ms of sustained low SG — filters out
// natural SG oscillation (±10-15 points around free-run regime).
static constexpr uint8_t  HOMING_SG_DEBOUNCE     = 5;

// EN pin polarity: TMC2209 EN is active LOW
static constexpr uint8_t EN_ENABLE  = LOW;
static constexpr uint8_t EN_DISABLE = HIGH;

// ─── Constructor / Destructor ────────────────────────────────────────────────

Tmc2209LocalDriver::Tmc2209LocalDriver(HardwareSerial& serial,
                                       uint8_t  tmc_address,
                                       uint8_t  step_pin,
                                       uint8_t  dir_pin,
                                       uint8_t  en_pin,
                                       uint8_t  rx_pin,
                                       uint8_t  tx_pin,
                                       uint8_t  diag_pin,
                                       float    r_sense,
                                       uint32_t uart_baud)
    : _serial(serial),
      _tmc_addr(tmc_address),
      _step_pin(step_pin), _dir_pin(dir_pin), _en_pin(en_pin),
      _rx_pin(rx_pin), _tx_pin(tx_pin), _diag_pin(diag_pin),
      _r_sense(r_sense), _uart_baud(uart_baud)
{}

Tmc2209LocalDriver::~Tmc2209LocalDriver() {
    end();
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

bool Tmc2209LocalDriver::begin() {
    // Configure output pins — keep driver disabled until fully initialised
    pinMode(_en_pin,   OUTPUT); digitalWrite(_en_pin,   EN_DISABLE);
    pinMode(_step_pin, OUTPUT); digitalWrite(_step_pin, LOW);
    pinMode(_dir_pin,  OUTPUT); digitalWrite(_dir_pin,  LOW);
    pinMode(_diag_pin, INPUT);

    // UART for TMCStepper — half-duplex PDN_UART
    _serial.begin(_uart_baud, SERIAL_8N1, _rx_pin, _tx_pin);
    delay(50);

    // TMC2209 via TMCStepper
    _tmc = new TMC2209Stepper(&_serial, _r_sense, _tmc_addr);
    _tmc->begin();
    _tmc->toff(4);                      // chopper off-time: enables driver output stage
    _tmc->blank_time(36);               // longer TBL → smoother chopper, less hiss
    _tmc->intpol(true);                 // 256-microstep interpolation: smoother current
    // Set both IRUN and IHOLD explicitly. The single-arg rms_current() leaves
    // IHOLD at the library default (~50% of IRUN), which kept the motor at
    // ~400 mA at idle and caused audible chopper hum even with pwm_freq=3.
    {
        float hold_mult = (_run_ma > 0) ? (float)_hold_ma / _run_ma : 0.0f;
        _tmc->rms_current(_run_ma, hold_mult);
    }
    _tmc->microsteps(16);               // 1/16 microstepping
    // SpreadCycle from boot — silent on this motor in motion AND at idle.
    // The StealthChop hum is motor-specific and we never reach a silent
    // chopper state with it on this hardware.
    _tmc->en_spreadCycle(true);
    // SGTHRS = 0 means DIAG never fires from stall comparison — set higher
    // only when entering homing/SGCal. Prevents spurious stall faults on jog.
    _tmc->SGTHRS(0);
    // PWM chopper frequency: 0=~23 kHz, 1=~35 kHz (datasheet default),
    // 2=~47 kHz, 3=~58 kHz. Higher freq is "more ultrasonic" in theory but
    // can hit motor acoustic resonance modes. 1 is the manufacturer-recommended
    // default — try others if this resonates on the specific motor.
    _tmc->pwm_freq(2);
    // Keep coils energised at IHOLD on standstill so the winch has holding
    // torque from boot — freewheel was tried (FREEWHEEL=1) but produced an
    // audible low-pitched hum on this hardware and made the rope freely
    // releasable at idle.
    _tmc->freewheel(0);
    // Short TPOWERDOWN so current drops to IHOLD ~310 ms after motion ends —
    // default ~3 s leaves the motor at full IRUN heating up after each move.
    _tmc->TPOWERDOWN(2);
    _tmc->TCOOLTHRS(0xFFFFF);           // enable CoolStep / StallGuard at all speeds
    // CoolStep: monitor SG_RESULT and reduce drive current when load is light.
    // semin=5 → ramp current up when SG_RESULT drops below 32*(semin+1) = 192.
    // semax=2 → ramp current down when SG_RESULT > 32*(semin+semax+1) = 256.
    // sedn=01 → current decrement rate (1 step per 32 SG reads).
    // seimin=1 → minimum CoolStep-reduced current = IRUN/2 (i.e. ~250 mA).
    // Keeps the motor cool during long DMX sinusoid sessions where the load
    // is typically much lower than the configured peak run current.
    _tmc->semin(5);
    _tmc->semax(2);
    _tmc->sedn(0b01);
    _tmc->seimin(1);

    // Verify UART communication
    if (_tmc->test_connection() != 0) {
        ESP_LOGE(TAG, "TMC2209 UART not responding (addr=%d)", _tmc_addr);
        return false;
    }
    ESP_LOGI(TAG, "TMC2209 addr=%d ok, version=0x%02X", _tmc_addr, _tmc->version());

    // FastAccelStepper — step pulses generated by ESP32 hardware (MCPWM/RMT).
    _engine.init();
    _stepper = _engine.stepperConnectToPin(_step_pin);
    if (!_stepper) {
        ESP_LOGE(TAG, "FastAccelStepper: no step-gen hardware free for GPIO%d", _step_pin);
        return false;
    }
    _stepper->setDirectionPin(_dir_pin);
    _stepper->setSpeedInHz(2000);     // overridden by setSpeed()
    _stepper->setAcceleration(1000);  // overridden by setAccel()

    // DIAG interrupt — RISING edge when TMC2209 detects stall
    attachInterruptArg(digitalPinToInterrupt(_diag_pin), _diagIsrCb, this, RISING);

    // Enable driver
    digitalWrite(_en_pin, EN_ENABLE);
    _state       = MotorState::IDLE;
    _fault_flags = 0;
    _homed       = false;

    return true;
}

void Tmc2209LocalDriver::end() {
    if (_diag_pin < 255)
        detachInterrupt(digitalPinToInterrupt(_diag_pin));

    if (_stepper) _stepper->forceStop();
    digitalWrite(_en_pin, EN_DISABLE);
    _state = MotorState::DRIVER_OFF;

    // FastAccelStepper instances are owned by the engine — not deleted here.
    _stepper = nullptr;
    delete _tmc; _tmc = nullptr;
}

// ─── Motion ──────────────────────────────────────────────────────────────────

void Tmc2209LocalDriver::moveTo(int32_t position) {
    if (_state == MotorState::FAULT || _state == MotorState::DRIVER_OFF) return;

    // Apply soft limits — clamp target into [min, max]
    if (_soft_limits_enabled) {
        if (position < _soft_min) {
            ESP_LOGW(TAG, "moveTo %d clamped to soft_min %d", position, _soft_min);
            position = _soft_min;
        } else if (position > _soft_max) {
            ESP_LOGW(TAG, "moveTo %d clamped to soft_max %d", position, _soft_max);
            position = _soft_max;
        }
    }

    _stepper->moveTo(position);
    if (position != _stepper->getCurrentPosition())
        _state = MotorState::MOVING;
}

void Tmc2209LocalDriver::moveBy(int32_t delta) {
    moveTo(_stepper->getCurrentPosition() + delta);
}

void Tmc2209LocalDriver::setSpeed(float steps_per_sec) {
    uint32_t hz = (steps_per_sec > 1.0f) ? (uint32_t)steps_per_sec : 1;
    _stepper->setSpeedInHz(hz);
}

void Tmc2209LocalDriver::setAccel(float steps_per_sec2) {
    uint32_t a = (steps_per_sec2 > 1.0f) ? (uint32_t)steps_per_sec2 : 1;
    _stepper->setAcceleration(a);
}

void Tmc2209LocalDriver::stop() {
    // Decelerated stop — applies to both ramped moves and constant-velocity jog.
    _stepper->stopMove();
    if (_state == MotorState::JOGGING)
        _state = MotorState::IDLE;
}

void Tmc2209LocalDriver::jog(int8_t direction, float speed_sps) {
    if (_state == MotorState::FAULT || _state == MotorState::DRIVER_OFF) {
        ESP_LOGW(TAG, "jog rejected: driver not ready (state=%d)", (int)_state);
        return;
    }
    if (direction == 0 || speed_sps <= 0.0f) {
        stop();
        return;
    }
    uint32_t hz = (speed_sps > 1.0f) ? (uint32_t)speed_sps : 1;
    _stepper->setSpeedInHz(hz);
    if (direction > 0) _stepper->runForward();
    else               _stepper->runBackward();
    _state = MotorState::JOGGING;
}

void Tmc2209LocalDriver::triggerStall() {
    if (!_stepper) return;
    _stepper->forceStop();
    _fault_flags |= (uint8_t)MotorFault::STALL;
    _state = MotorState::FAULT;
    ESP_LOGW(TAG, "stall triggered by fixture (profile-based)");
}

void Tmc2209LocalDriver::estop() {
    // Immediate stop (no deceleration ramp) — then cut driver enable
    _stepper->forceStop();
    digitalWrite(_en_pin, EN_DISABLE);
    _state       = MotorState::FAULT;
    _fault_flags |= (uint8_t)MotorFault::DRIVER_ERROR;
    ESP_LOGW(TAG, "E-STOP triggered");
}

// ─── Soft limits + fault recovery ────────────────────────────────────────────

void Tmc2209LocalDriver::setSoftLimits(int32_t min_pos, int32_t max_pos) {
    if (min_pos >= max_pos) {
        ESP_LOGW(TAG, "setSoftLimits ignored: min (%d) >= max (%d)", min_pos, max_pos);
        return;
    }
    _soft_min            = min_pos;
    _soft_max            = max_pos;
    _soft_limits_enabled = true;
    ESP_LOGI(TAG, "soft limits enabled: [%d, %d]", _soft_min, _soft_max);
}

void Tmc2209LocalDriver::disableSoftLimits() {
    _soft_limits_enabled = false;
    ESP_LOGI(TAG, "soft limits disabled");
}

bool Tmc2209LocalDriver::clearFault() {
    if (_state != MotorState::FAULT && _state != MotorState::DRIVER_OFF) {
        // Nothing to clear — but still reset flags in case they accumulated
        _fault_flags = 0;
        return true;
    }
    if (!_tmc) {
        ESP_LOGE(TAG, "clearFault: driver not initialised");
        return false;
    }

    // Re-read driver registers to check whether sticky faults still active
    _readRegisters();

    // Sticky conditions: overtemperature must clear, driver must respond on UART
    if (_fault_flags & (uint8_t)MotorFault::OVERTEMP) {
        ESP_LOGW(TAG, "clearFault: overtemperature still active — refusing to clear");
        return false;
    }
    if (_tmc->test_connection() != 0) {
        ESP_LOGW(TAG, "clearFault: UART not responding — refusing to clear");
        _fault_flags |= (uint8_t)MotorFault::DRIVER_ERROR;
        return false;
    }

    // All sticky conditions cleared — reset flags, re-enable, return to IDLE
    _fault_flags = 0;
    _stall_isr_flag = false;
    digitalWrite(_en_pin, EN_ENABLE);
    _state = MotorState::IDLE;
    ESP_LOGI(TAG, "fault cleared, driver re-enabled");
    return true;
}

// ─── Homing ──────────────────────────────────────────────────────────────────

void Tmc2209LocalDriver::startHoming() {
    if (_state == MotorState::FAULT || _state == MotorState::DRIVER_OFF) {
        ESP_LOGW(TAG, "startHoming: driver not ready (state=%d)", (int)_state);
        return;
    }
    if (!_tmc || !_stepper) return;

    // Apply reduced current directly — do NOT update _run_ma so restore in _updateHoming works
    float hold_mult = (_hcfg.current_ma > 0)
                      ? (float)_hold_ma / _hcfg.current_ma : 0.0f;
    _tmc->rms_current(_hcfg.current_ma, hold_mult);
    // Already in SpreadCycle from boot — set SGTHRS for the homing trip.
    _tmc->SGTHRS(_hcfg.sgthrs);

    // Clear any stale stall flag before we start moving
    _stall_isr_flag = false;

    // Constant-speed travel toward the end stop.
    _stepper->setSpeedInHz(_hcfg.speed > 1 ? _hcfg.speed : 1);
    if (_hcfg.direction > 0) _stepper->runForward();
    else                     _stepper->runBackward();

    _homing_start_ms = millis();
    _homing_phase    = HomingPhase::MOVING;
    _state           = MotorState::HOMING;
    _homing_settled  = false;   // wait for SG to first cross above trip threshold
    _sg_high_count   = 0;
    _sg_low_count    = 0;
    ESP_LOGI(TAG, "homing started: dir=%d speed=%u sgthrs=%u current=%u mA",
             _hcfg.direction, _hcfg.speed, _hcfg.sgthrs, _hcfg.current_ma);
}

void Tmc2209LocalDriver::setHomePosition() {
    _stepper->forceStopAndNewPosition(0);
    _homed = true;
    ESP_LOGI(TAG, "home position set (step counter reset to 0)");
}

void Tmc2209LocalDriver::setHomingConfig(const HomingConfig& cfg) {
    _hcfg = cfg;
    ESP_LOGI(TAG, "homing config: dir=%d speed=%u sgthrs=%u current=%u backoff=%u",
             cfg.direction, cfg.speed, cfg.sgthrs, cfg.current_ma, cfg.backoff_steps);
}

// ─── Current control ─────────────────────────────────────────────────────────

void Tmc2209LocalDriver::setRunCurrent(uint16_t ma) {
    _run_ma = ma;
    if (_tmc) {
        float hold_mult = (_run_ma > 0) ? (float)_hold_ma / _run_ma : 0.0f;
        _tmc->rms_current(_run_ma, hold_mult);
    }
}

void Tmc2209LocalDriver::setHoldCurrent(uint16_t ma) {
    _hold_ma = ma;
    setRunCurrent(_run_ma);  // re-applies hold multiplier
}

// ─── StallGuard — Fase 4 stubs ───────────────────────────────────────────────

void Tmc2209LocalDriver::setStallGuardThreshold(uint8_t threshold) {
    if (_tmc) _tmc->SGTHRS(threshold);
}

uint16_t Tmc2209LocalDriver::getStallGuardResult() {
    return _sg_result;
}

// Run the motor at homing speed with the stall fault suppressed, so the operator
// can load the shaft and watch SG_RESULT (SGCAL log) without tripping a FAULT.
// We switch to SpreadCycle (StallGuard2) only for the cal run: SG4 in StealthChop
// can saturate near zero on motors already operating close to stall current,
// giving a useless 2-point swing. SG2 in SpreadCycle has a 0-510 range with
// 100-300 points of typical free/loaded difference. Restored in stopStallGuardCal.
void Tmc2209LocalDriver::startStallGuardCal(int8_t direction) {
    if (_state == MotorState::FAULT || _state == MotorState::DRIVER_OFF) {
        ESP_LOGW(TAG, "startStallGuardCal: driver not ready (state=%d)", (int)_state);
        return;
    }
    if (!_stepper || !_tmc) return;
    _sg_cal         = true;
    _stall_isr_flag = false;
    _stepper->setSpeedInHz(_hcfg.speed > 1 ? _hcfg.speed : 1);
    if (direction >= 0) _stepper->runForward();
    else                _stepper->runBackward();
    _state = MotorState::JOGGING;
    ESP_LOGI(TAG, "StallGuard calibration started (SpreadCycle, speed=%u dir=%d)",
             _hcfg.speed, (int)direction);
}

void Tmc2209LocalDriver::stopStallGuardCal() {
    _sg_cal = false;
    if (_stepper) _stepper->stopMove();
    // Restore the operational threshold so DMX moves resume with stall detect.
    if (_tmc)     _tmc->SGTHRS(_hcfg.op_sgthrs);
    if (_state == MotorState::JOGGING) _state = MotorState::IDLE;
    ESP_LOGI(TAG, "StallGuard calibration stopped");
}

// ─── Status ──────────────────────────────────────────────────────────────────

MotorStatus Tmc2209LocalDriver::getStatus() {
    MotorStatus s;
    s.state       = _state;
    s.position    = _stepper ? _stepper->getCurrentPosition() : 0;
    s.speed       = _stepper ? (_stepper->getCurrentSpeedInMilliHz() / 1000.0f) : 0.0f;
    s.fault_flags = _fault_flags;
    s.sg_result   = _sg_result;
    s.driver_temp = _driver_temp;
    s.homed       = _homed;
    return s;
}

bool Tmc2209LocalDriver::isBusy() {
    return _stepper && _stepper->isRunning();
}

bool Tmc2209LocalDriver::hasFeature(const char* feature) {
    if (strcmp(feature, "stallguard")  == 0) return true;
    if (strcmp(feature, "coolstep")    == 0) return true;
    if (strcmp(feature, "uart_config") == 0) return true;
    return false;
}

// ─── Update loop ─────────────────────────────────────────────────────────────

void Tmc2209LocalDriver::update() {
    if (!_stepper || !_tmc) return;

    // Step pulses are generated by FastAccelStepper in hardware — update() only
    // advances the homing state machine, detects stalls and polls TMC registers.
    if (_state == MotorState::HOMING) {
        _updateHoming();
    } else if (_state == MotorState::JOGGING) {
        // DIAG-based stall during jog. Only honored above the StallGuard4
        // valid velocity (~3000 step/s) — below that SG_RESULT is garbage and
        // DIAG pulses on transients (EMI, cable strain) without a real stall.
        // A jogging operator who's at low speed gets no stall fault; at high
        // speed (~15 cm/s on a typical winch = ~9500 step/s) a real cable
        // jam still trips FAULT and waits for Clear Fault.
        if (_stall_isr_flag) {
            _stall_isr_flag = false;
            int32_t spd_mhz = _stepper->getCurrentSpeedInMilliHz();
            constexpr int32_t STALL_MIN_SPD_MHZ = 3000000;  // 3000 step/s
            if (!_sg_cal && abs(spd_mhz) >= STALL_MIN_SPD_MHZ) {
                _stepper->forceStop();
                _fault_flags |= (uint8_t)MotorFault::STALL;
                _state = MotorState::FAULT;
                ESP_LOGW(TAG, "stall during jog (spd=%d mHz)", spd_mhz);
            }
        }
        // Soft-limit enforcement, direction-aware. Active only after homing
        // (before homing the position counter is meaningless). Blocks motion
        // that would push further past a limit already crossed; allows motion
        // back into the safe range. Stops cleanly on every re-jog past limit.
        if (_soft_limits_enabled && _homed && !_jog_ignore_limits) {
            int32_t pos = _stepper->getCurrentPosition();
            int32_t spd = _stepper->getCurrentSpeedInMilliHz();
            bool past_top = pos >= _soft_max && spd > 0;
            bool past_bot = pos <= _soft_min && spd < 0;
            if (past_top || past_bot) {
                _stepper->forceStop();
                _state = MotorState::IDLE;
                ESP_LOGI(TAG, "jog stopped at soft limit (pos=%d, range=[%d,%d], spd=%d)",
                         pos, _soft_min, _soft_max, spd);
            }
        }
    } else {
        // Transition to IDLE when a ramped move reaches its target
        if (_state == MotorState::MOVING &&
            _stepper->getCurrentPosition() == _stepper->targetPos())
            _state = MotorState::IDLE;

        // DIAG-based stall during DMX moves. Same velocity gate as JOG: SG
        // only valid above ~3000 step/s. Most DMX setups run far below that
        // so this effectively disables stall detection during normal
        // operation, but fast moves (e.g. high maxSpeed) still get coverage.
        if (_stall_isr_flag) {
            _stall_isr_flag = false;
            int32_t spd_mhz = _stepper->getCurrentSpeedInMilliHz();
            constexpr int32_t STALL_MIN_SPD_MHZ = 3000000;  // 3000 step/s
            if (abs(spd_mhz) >= STALL_MIN_SPD_MHZ) {
                _stepper->forceStop();
                _fault_flags |= (uint8_t)MotorFault::STALL;
                _state = MotorState::FAULT;
                ESP_LOGW(TAG, "stall detected via DIAG (spd=%d mHz)", spd_mhz);
            }
        }
    }

    // Periodic TMC register read (driver status + StallGuard result)
    uint32_t now = millis();
    if (now - _last_reg_ms >= REG_READ_INTERVAL_MS) {
        _last_reg_ms = now;
        _readRegisters();
        // StallGuard calibration aid — while the motor moves, stream SG_RESULT at
        // 10 Hz so the operator can read the free-running vs loaded band and pick a
        // homing SGTHRS. Jog free, then hold the shaft and watch SG drop toward 0.
        if (_state == MotorState::JOGGING || _state == MotorState::HOMING ||
            _state == MotorState::MOVING) {
            ESP_LOGI(TAG, "SGCAL sg=%u state=%d", (unsigned)_sg_result, (int)_state);
        }
    }
}

// ─── Homing state machine ────────────────────────────────────────────────────

void Tmc2209LocalDriver::_updateHoming() {
    switch (_homing_phase) {

    case HomingPhase::MOVING: {
        // Ignore everything during the brief acceleration ramp.
        if (millis() - _homing_start_ms < HOMING_STALL_BLANK_MS) {
            _stall_isr_flag = false;
            _sg_low_count   = _sg_high_count = 0;
            break;
        }
        const uint16_t trip_thr = (uint16_t)_hcfg.sgthrs * 2;
        const uint32_t elapsed  = millis() - _homing_start_ms;

        // Phase A — settle: wait for SG to first cross above trip_thr (free-run
        // reached). This adapts to the actual PWM autoscale settling time and
        // avoids false-tripping on the rising transient (SG climbs 0 → ~180).
        if (!_homing_settled) {
            // DIAG fires throughout the transient (TMC2209 hardware asserts it
            // whenever SG*2 ≤ SGTHRS, which includes SG=0 at startup). Drop the
            // flag every tick during settle so phase B doesn't inherit a stale
            // assertion.
            _stall_isr_flag = false;
            if (_sg_result > trip_thr) {
                if (++_sg_high_count >= HOMING_SG_SETTLE_HIGH) {
                    _homing_settled = true;
                    _sg_low_count   = 0;
                    ESP_LOGI(TAG, "homing settled (sg=%u thr=%u after %ums)",
                             (unsigned)_sg_result, (unsigned)trip_thr, elapsed);
                }
            } else {
                _sg_high_count = 0;
            }
            // Settle timeout: motor never reached free-run regime → already at
            // end stop. Same trip path as a normal stall.
            if (elapsed < HOMING_SETTLE_TIMEOUT_MS) break;
            ESP_LOGW(TAG, "homing settle timeout (sg=%u) — assuming start at end stop",
                     (unsigned)_sg_result);
        } else {
            // Phase B — watch: SG previously settled high, now look for a drop.
            // We deliberately ignore _stall_isr_flag here: TMC2209 DIAG fires on
            // any momentary SG dip below SGTHRS, including transient bumps
            // during normal free-run motion, which produced false trips. SG
            // polling with debounce (5 × 100 ms = 500 ms sustained low) is the
            // single source of truth — no hardware fallback.
            _stall_isr_flag = false;
            if (_sg_result > 0 && _sg_result < trip_thr) {
                if (++_sg_low_count < HOMING_SG_DEBOUNCE) break;
                // 5 consecutive low samples → real stall, fall through to trip.
            } else {
                _sg_low_count = 0;
                break;
            }
        }

        // End stop reached — stop and zero the position counter
        _stepper->forceStopAndNewPosition(0);

        // Restore operational current. SGTHRS goes to 0 (DIAG disarmed) for
        // post-homing operation — operSgthrs is only valid at the homing
        // velocity, and DMX moves typically run at a different speed where
        // the natural SG_RESULT band sits below the calibrated trip, causing
        // false stall faults. Hardware OT/short protections remain.
        setRunCurrent(_run_ma);
        _tmc->SGTHRS(0);

        // Retreat from the end stop (ramped move away from it)
        _stepper->setSpeedInHz(_hcfg.speed > 1 ? _hcfg.speed : 1);
        _stepper->moveTo((int32_t)_hcfg.backoff_steps * -_hcfg.direction);
        _homing_phase = HomingPhase::BACKOFF;
        ESP_LOGI(TAG, "stall at end stop (sg=%u thr=%u) — backing off %u steps",
                 (unsigned)_sg_result, (unsigned)trip_thr, _hcfg.backoff_steps);
        break;
    }

    case HomingPhase::BACKOFF:
        if (_stepper->getCurrentPosition() == _stepper->targetPos()) {
            // Back-off complete — declare home at current position. Clear any
            // residual DIAG assertion: while the operator was holding the rope
            // during the stall trip the TMC2209 kept asserting DIAG, and on
            // transition to IDLE the regular update() path would otherwise
            // immediately re-promote that flag to a STALL fault.
            _stepper->setCurrentPosition(0);
            _stall_isr_flag = false;
            _homed          = true;
            _homing_phase   = HomingPhase::IDLE;
            _state          = MotorState::IDLE;
            ESP_LOGI(TAG, "homing complete — home position set");
        }
        break;

    case HomingPhase::IDLE:
    default:
        break;
    }
}

// ─── Private helpers ─────────────────────────────────────────────────────────

void Tmc2209LocalDriver::_readRegisters() {
    _sg_result = _tmc->SG_RESULT();

    // DRV_STATUS UART reads are corrupted by coil EMI while the motor is
    // moving — individual otpw/ot/s2g* accessors can flip a fault flag on
    // even when the chip is cold and the wiring fine. The TMC2209 protects
    // itself in hardware (157 °C thermal shutdown, short-circuit
    // auto-disable) regardless of our software supervision, so we skip the
    // poll while running and add a 500 ms cool-off after motion ends.
    bool moving = _stepper && _stepper->isRunning();
    if (moving) {
        _last_motion_ms = millis();
        return;
    }
    if (millis() - _last_motion_ms < 500) {
        return;
    }

    // DRV_STATUS bit layout differs across TMC chips — use the library's
    // chip-specific accessors instead of raw bit math. The accessors include
    // CRC retry logic that a direct register read does not.
    bool otpw = _tmc->otpw();   // over-temperature pre-warning (~120 °C)
    bool ot   = _tmc->ot();     // over-temperature shutdown (~150 °C)
    bool s2ga = _tmc->s2ga();   // short to GND phase A
    bool s2gb = _tmc->s2gb();   // short to GND phase B

    if (ot || otpw) {
        _fault_flags |= (uint8_t)MotorFault::OVERTEMP;
        _driver_temp  = ot ? 150 : 120;
        if (ot) ESP_LOGE(TAG, "TMC2209 overtemperature shutdown!");
    } else {
        _fault_flags &= ~(uint8_t)MotorFault::OVERTEMP;
    }
    if (s2ga || s2gb) {
        _fault_flags |= (uint8_t)MotorFault::OVERCURRENT;
        ESP_LOGE(TAG, "TMC2209 short to GND detected");
    }
}

void IRAM_ATTR Tmc2209LocalDriver::_diagIsrCb(void* arg) {
    Tmc2209LocalDriver* drv = static_cast<Tmc2209LocalDriver*>(arg);
    drv->_stall_isr_flag = true;
}

#endif // RAVLIGHT_HAS_MOTOR
