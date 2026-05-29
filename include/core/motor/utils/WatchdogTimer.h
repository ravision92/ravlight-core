#pragma once
#include <stdint.h>

// Simple monotonic-time watchdog.
//
// Usage:
//   WatchdogTimer wd(5000);          // 5 s timeout
//   wd.enable();
//   ...
//   wd.kick();                       // every time a valid DMX packet arrives
//   if (wd.isExpired()) estop();     // call from update task
//
// The watchdog is monotonic — kick() resets the timer; isExpired() reports
// whether more than `timeout_ms` has elapsed since the last kick. Polling-based
// (no callbacks), so the caller decides when to react to expiry.

class WatchdogTimer {
public:
    explicit WatchdogTimer(uint32_t timeout_ms = 5000);

    void     setTimeout(uint32_t timeout_ms);
    uint32_t getTimeout() const { return _timeout_ms; }

    void     enable();    // arms the watchdog; starts counting from now
    void     disable();   // stops counting; isExpired() returns false until re-enabled
    bool     isEnabled() const { return _enabled; }

    void     kick();      // resets the timer to "now"
    bool     isExpired() const;

    // Time since last kick, in ms. Returns 0 if disabled.
    uint32_t elapsedMs() const;

private:
    uint32_t _timeout_ms;
    uint32_t _last_kick_ms = 0;
    bool     _enabled      = false;
};
