#pragma once
#include <stdint.h>

// Guided StallGuard threshold calibrator.
//
// Usage:
//   1. Create instance, call begin(driver).
//   2. Move the motor freely (no load) — call update() each tick.
//   3. When state() == DONE, read suggestedThreshold() and apply it.
//   4. Optionally run again under load to verify no false stalls.
//
// The calibrator samples SG_RESULT over a configurable window and
// suggests SGTHRS = (min_sg / 2), leaving headroom for real stalls.

class IMotorDriver;

class StallGuardCalibrator {
public:
    enum class State : uint8_t { IDLE, RUNNING, DONE, FAILED };

    // sample_count: number of SG_RESULT readings to collect before deciding
    explicit StallGuardCalibrator(uint16_t sample_count = 200);

    // Attach to a running driver and start sampling.
    // The caller is responsible for keeping the motor moving during calibration.
    void begin(IMotorDriver* driver);

    // Call every update tick (same rate as driver->update()).
    void update();

    State    state()              const { return _state; }
    uint8_t  suggestedThreshold() const { return _suggested; }

    // Reset to IDLE so the same instance can be reused.
    void reset();

private:
    IMotorDriver* _driver       = nullptr;
    State         _state        = State::IDLE;
    uint16_t      _sample_count = 200;
    uint16_t      _samples_done = 0;
    uint16_t      _sg_min       = 0xFFFF;
    uint8_t       _suggested    = 0;
};
