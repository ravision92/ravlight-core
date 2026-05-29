#ifdef RAVLIGHT_HAS_MOTOR
#include "core/motor/utils/StallGuardCalibrator.h"
#include "core/motor/IMotorDriver.h"
#include <esp_log.h>

static const char* TAG = "SGCalibrator";

StallGuardCalibrator::StallGuardCalibrator(uint16_t sample_count)
    : _sample_count(sample_count) {}

void StallGuardCalibrator::begin(IMotorDriver* driver) {
    if (!driver) {
        ESP_LOGE(TAG, "begin: null driver");
        _state = State::FAILED;
        return;
    }
    _driver      = driver;
    _samples_done = 0;
    _sg_min      = 0xFFFF;
    _suggested   = 0;
    _state       = State::RUNNING;
    ESP_LOGI(TAG, "calibration started, collecting %u samples", _sample_count);
}

void StallGuardCalibrator::update() {
    if (_state != State::RUNNING) return;

    uint16_t sg = _driver->getStallGuardResult();

    // SG_RESULT == 0 during stall — ignore readings where motor is already stalled
    if (sg > 0 && sg < _sg_min)
        _sg_min = sg;

    ++_samples_done;

    if (_samples_done >= _sample_count) {
        if (_sg_min == 0xFFFF) {
            // Never got a valid reading
            ESP_LOGE(TAG, "calibration failed — no valid SG_RESULT readings");
            _state = State::FAILED;
            return;
        }
        // Suggest half the minimum free-running value as the stall threshold
        _suggested = (uint8_t)(_sg_min / 2);
        _state     = State::DONE;
        ESP_LOGI(TAG, "calibration done: sg_min=%u → suggested SGTHRS=%u",
                 _sg_min, _suggested);
    }
}

void StallGuardCalibrator::reset() {
    _driver       = nullptr;
    _state        = State::IDLE;
    _samples_done = 0;
    _sg_min       = 0xFFFF;
    _suggested    = 0;
}

#endif // RAVLIGHT_HAS_MOTOR
