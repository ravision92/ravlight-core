#include "core/motor/utils/WatchdogTimer.h"
#include <Arduino.h>

WatchdogTimer::WatchdogTimer(uint32_t timeout_ms)
    : _timeout_ms(timeout_ms) {}

void WatchdogTimer::setTimeout(uint32_t timeout_ms) {
    _timeout_ms = timeout_ms;
}

void WatchdogTimer::enable() {
    _enabled      = true;
    _last_kick_ms = millis();
}

void WatchdogTimer::disable() {
    _enabled = false;
}

void WatchdogTimer::kick() {
    _last_kick_ms = millis();
}

bool WatchdogTimer::isExpired() const {
    if (!_enabled) return false;
    return (millis() - _last_kick_ms) >= _timeout_ms;
}

uint32_t WatchdogTimer::elapsedMs() const {
    if (!_enabled) return 0;
    return millis() - _last_kick_ms;
}
