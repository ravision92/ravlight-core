#pragma once
#include "core/motor/IMotorDriver.h"
#include <HardwareSerial.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include <esp_timer.h>

// Homing configuration — passed via setHomingConfig() before calling startHoming().
struct HomingConfig {
    int8_t   direction       = -1;   // +1 toward positive axis, -1 toward negative
    uint32_t speed           = 500;  // steps/s during slow homing travel
    uint8_t  sgthrs          = 30;   // StallGuard threshold for homing (lower = less sensitive)
    uint8_t  op_sgthrs       = 50;   // operational SGTHRS restored after homing complete
    uint16_t current_ma      = 400;  // reduced current during homing move
    uint32_t backoff_steps   = 200;  // steps to retreat from end stop after stall
};

// TMC2209 driver mounted directly on the node board.
// Communication: UART half-duplex on PDN_UART pin via HardwareSerial.
// Step generation: AccelStepper for ramp scheduling, called from update().

class Tmc2209LocalDriver : public IMotorDriver {
public:
    // r_sense: sense resistor in Ohms — 0.11f for most breakout boards
    // uart_baud: PDN_UART baud rate — board-dependent (pass HW_MOTOR_UART_BAUD)
    Tmc2209LocalDriver(HardwareSerial& serial,
                       uint8_t  tmc_address,
                       uint8_t  step_pin,
                       uint8_t  dir_pin,
                       uint8_t  en_pin,
                       uint8_t  rx_pin,
                       uint8_t  tx_pin,
                       uint8_t  diag_pin,
                       float    r_sense   = 0.11f,
                       uint32_t uart_baud = 115200);
    ~Tmc2209LocalDriver() override;

    // Lifecycle
    bool begin() override;
    void end()   override;

    // Motion
    void moveTo(int32_t position)       override;
    void moveBy(int32_t delta)          override;
    void setSpeed(float steps_per_sec)  override;
    void setAccel(float steps_per_sec2) override;
    void stop()                         override;
    void hardStop()                     override;
    void estop()                        override;
    void driverOff()                    override;
    void driverOn()                     override;
    void triggerStall()                 override;
    void jog(int8_t direction, float speed_sps) override;

    // Soft limits + fault recovery
    void setSoftLimits(int32_t min_pos, int32_t max_pos) override;
    void disableSoftLimits()                              override;
    bool clearFault()                                     override;

    // Homing
    void startHoming()     override;
    void setHomePosition() override;
    void setHomingConfig(const HomingConfig& cfg);

    // Current control
    void setRunCurrent(uint16_t ma)  override;
    void setHoldCurrent(uint16_t ma) override;

    // StallGuard
    void     setStallGuardThreshold(uint8_t threshold) override;
    uint16_t getStallGuardResult()                     override;
    void     startStallGuardCal(int8_t direction)      override;
    void     stopStallGuardCal()                       override;

    // Status
    MotorStatus getStatus() override;
    bool        isBusy()    override;
    bool        hasFeature(const char* feature) override;

    // Called periodically from the motor task. Step pulses are generated in
    // hardware by FastAccelStepper — this only advances the homing state machine,
    // checks the DIAG stall flag and reads TMC registers every 100 ms.
    void update() override;

private:
    HardwareSerial& _serial;
    const uint8_t   _tmc_addr;
    const uint8_t   _step_pin, _dir_pin, _en_pin, _rx_pin, _tx_pin, _diag_pin;
    const float     _r_sense;
    const uint32_t  _uart_baud;

    TMC2209Stepper*        _tmc     = nullptr;
    FastAccelStepperEngine _engine  = FastAccelStepperEngine();
    FastAccelStepper*      _stepper = nullptr;

    MotorState _state       = MotorState::IDLE;
    bool       _homed       = false;
    uint8_t    _fault_flags = 0;
    uint16_t   _run_ma      = 800;
    uint16_t   _hold_ma     = 300;
    uint16_t   _sg_result   = 0;
    uint8_t    _driver_temp = 0;
    uint32_t   _last_reg_ms     = 0;
    uint32_t   _homing_start_ms = 0;   // millis() when the current homing move began
    uint32_t   _last_motion_ms  = 0;   // millis() of the last tick where motor was running
    uint8_t    _ot_debounce     = 0;   // consecutive DRV_STATUS reads showing OT/OTPW

    volatile bool _stall_isr_flag  = false;  // set by DIAG interrupt
    bool          _sg_cal          = false;  // StallGuard calibration run active
    bool          _homing_settled  = false;  // SG has reached free-run regime (autoscale done)
    bool          _spread_active   = false;  // current chopper mode: true=SpreadCycle, false=StealthChop
    uint8_t       _sg_low_count    = 0;      // debounce counter for SG-polling homing detect
    uint8_t       _sg_high_count   = 0;      // counts SG samples above trip during settle

    // Soft position limits — disabled when min == max
    int32_t _soft_min = 0;
    int32_t _soft_max = 0;
    bool    _soft_limits_enabled = false;
    bool    _was_in_soft_range   = false;  // tracks soft-limit range transitions for jog

public:
    // Suspend soft-limit enforcement for the next jog session. Used by the
    // UI's "override" toggle so the operator can set travel limits past the
    // currently saved range. Cleared on /jogstop in the orion fixture.
    void setJogIgnoreLimits(bool b) { _jog_ignore_limits = b; }
    // Suspend StallGuard stall trips during a jog session. The operator
    // re-defining travel limits is jogging slowly while the SG profile may
    // not yet be calibrated for this rig — the static operSgthrs would
    // false-trip on transient bumps. Cleared on /jogstop.
    void setJogIgnoreStall(bool b)  { _jog_ignore_stall  = b; }

    // Worm-gear / self-locking rigs keep physical position through a stall,
    // so clearFault doesn't need to force a re-home there.
    void setInvalidateHomeOnStall(bool b) override { _invalidate_home_on_stall = b; }

    // True during the post-stop grace window used to mask false stalls
    // from the natural SG_RESULT dip while the motor decelerates. Used by
    // the fixture-level profile check so it also skips during that window.
    bool inStopGrace() const override { return (int32_t)(_stop_grace_until_ms - millis()) > 0; }

    // ── SG diagnostic ring buffer ────────────────────────────────────────
    // One entry per register read (100 ms) — ~25 s of history. Lets the
    // web UI / curl dump exactly what StallGuard saw during a physical
    // block test without a serial cable attached.
    struct SgLogEntry {
        uint32_t t;      // millis() of the sample
        uint16_t sg;     // SG_RESULT
        int16_t  spd10;  // signed speed / 10 (steps/s ÷ 10 to fit int16)
        uint8_t  state;  // MotorState at sample time
        uint8_t  flags;  // bit0 DIAG pin, bit1 in_grace, bit2 ignore_stall
    };
    static constexpr uint16_t SGLOG_N = 256;
    // Copy newest-first into caller's buffer; returns entries written.
    uint16_t sgLogSnapshot(SgLogEntry* out, uint16_t max_entries) const;

private:
    bool    _jog_ignore_limits   = false;
    bool    _jog_ignore_stall    = false;
    bool    _invalidate_home_on_stall = true;

    // Mirror of the SGTHRS register — the TMC2209 asserts DIAG while
    // SG_RESULT < 2·SGTHRS. Tracked so the level-based stall fallback in
    // update() knows the active trip line without a UART read.
    uint8_t _sgthrs_active = 0;
    // Consecutive low-SG register reads for the level-based stall fallback
    // (separate from _sg_low_count, which belongs to the homing FSM).
    uint8_t _op_sg_low_count = 0;

    // SG diagnostic ring buffer (see SgLogEntry above).
    SgLogEntry _sglog[SGLOG_N] = {};
    uint16_t   _sglog_head = 0;   // next write slot
    uint16_t   _sglog_used = 0;   // entries filled (caps at SGLOG_N)

    // Post-stop grace timestamp. StallGuard4's SG_RESULT is speed-sensitive
    // — during a deceleration ramp the reading drops naturally as speed
    // decreases, which the DIAG pin can misinterpret as a genuine stall.
    // stop() sets this to millis() + STOP_GRACE_MS so subsequent DIAG
    // events during the ramp are ignored irrespective of _jog_ignore_stall.
    uint32_t _stop_grace_until_ms = 0;

    // Edge-triggered latch for anticipated soft-limit / home-side braking.
    // Set true when the anticipation math fires stopMove(); cleared once the
    // motor has fully stopped. Prevents the update() loop from re-firing
    // stopMove() (and its ESP_LOG line) at 1 ms cadence during the whole
    // deceleration ramp, which starves the loop task and trips the WDT.
    bool _limit_braking = false;

    // hardStop() temporarily boosts deceleration to near-instant. The
    // original accel is saved here and restored by update() once the
    // motor comes to rest. Using this indirection instead of the
    // library's forceStop() avoids the ignore_commands=true flag that
    // FastAccelStepper sets on forceStop, which permanently blocks
    // subsequent runForward/moveTo calls until a re-init.
    bool     _accel_override_active = false;
    uint32_t _saved_accel           = 0;

    // Homing state machine
    enum class HomingPhase : uint8_t { IDLE, MOVING, BACKOFF };
    HomingPhase _homing_phase = HomingPhase::IDLE;
    HomingConfig _hcfg;

    static void IRAM_ATTR _diagIsrCb(void* arg);
    void _readRegisters();
    void _updateHoming();

    // Near-instant stop via accel-boost + stopMove — used by hardStop,
    // triggerStall and the DIAG stall paths in update(). Avoids the
    // ignore_commands=true side effect of FastAccelStepper::forceStop().
    void _instantStop();
};
