#pragma once
#include <stdint.h>

// Motor Abstraction Layer — core interface.
// Fixture logic calls only these methods; the backend (TMC2209 local/remote,
// StepDir external) is selected at runtime from config JSON via MotorDriverFactory.

// Bitmask — combine with | to report multiple simultaneous faults.
enum class MotorFault : uint8_t {
    NONE          = 0,
    STALL         = 1 << 0,
    OVERCURRENT   = 1 << 1,
    OVERTEMP      = 1 << 2,
    DRIVER_ERROR  = 1 << 3,
    COMM_LOST     = 1 << 4,   // remote backend only
    NOT_HOMED     = 1 << 5,
};

enum class MotorState : uint8_t {
    IDLE,
    HOMING,
    MOVING,
    JOGGING,      // constant-speed manual move (calibration); bypasses soft limits
    FAULT,
    DRIVER_OFF,   // driver de-energized (EN pin HIGH)
};

struct MotorStatus {
    MotorState  state;
    int32_t     position;       // current step count
    float       speed;          // current steps/s
    uint8_t     fault_flags;    // bitmask of MotorFault values
    uint16_t    sg_result;      // StallGuard result (0 if unavailable)
    uint8_t     driver_temp;    // degrees C (0 if unavailable)
    bool        homed;
};

class IMotorDriver {
public:
    virtual ~IMotorDriver() = default;

    // Lifecycle
    virtual bool  begin() = 0;
    virtual void  end()   = 0;

    // Motion
    virtual void  moveTo(int32_t position)       = 0;
    virtual void  moveBy(int32_t delta)          = 0;
    virtual void  setSpeed(float steps_per_sec)  = 0;
    virtual void  setAccel(float steps_per_sec2) = 0;
    virtual void  stop()                         = 0;  // controlled deceleration
    virtual void  estop()                        = 0;  // immediate stop + disable

    // Constant-speed manual move — for calibration / homing setup.
    // direction: +1 or -1. speed_sps: steps per second (positive magnitude).
    // Bypasses soft limits — caller is responsible for not crashing the mechanism.
    // Call stop() to end the jog.
    virtual void  jog(int8_t direction, float speed_sps) = 0;

    // Soft position limits — moveTo()/moveBy() clamp the target into [min, max].
    // Pass min == max == 0 (default) to disable clamping.
    virtual void  setSoftLimits(int32_t min_pos, int32_t max_pos) = 0;
    virtual void  disableSoftLimits()                              = 0;

    // Fault recovery — attempts to clear FAULT state and re-enable the driver.
    // Returns true if all faults cleared; false if any sticky condition persists
    // (e.g. overtemperature still active). Caller decides whether to retry.
    virtual bool  clearFault() = 0;

    // Homing
    virtual void  startHoming()      = 0;
    virtual void  setHomePosition()  = 0;  // forces current position = 0

    // Current control
    virtual void  setRunCurrent(uint16_t ma)  = 0;
    virtual void  setHoldCurrent(uint16_t ma) = 0;

    // StallGuard — no-op on backends that do not support it
    virtual void     setStallGuardThreshold(uint8_t threshold) = 0;
    virtual uint16_t getStallGuardResult()                     = 0;

    // StallGuard calibration run — motor turns at homing speed in `direction`
    // (+1/-1) with the stall fault suppressed, so the operator can observe
    // SG_RESULT free vs loaded. No-op on backends without StallGuard.
    virtual void startStallGuardCal(int8_t direction) { (void)direction; }
    virtual void stopStallGuardCal()  {}

    // Status
    virtual MotorStatus getStatus() = 0;
    virtual bool        isBusy()    = 0;

    // Feature query — avoids #ifdef in fixture logic.
    // Known keys: "stallguard", "coolstep", "uart_config", "comm_latency"
    virtual bool hasFeature(const char* feature) = 0;

    // Update loop — must be called from a dedicated task on every iteration.
    // Handles ISR flags, UART polling, AccelStepper tick.
    virtual void update() = 0;
};
