#ifndef SETTINGS_H
#define SETTINGS_H

// Fixture identity — PROJECT_NAME and FW_VERSION are fixture-specific
#ifdef RAVLIGHT_FIXTURE_VEYRON
  #define PROJECT_NAME "Veyron"
  #define FW_VERSION   "FW 2.7.0 HW 2.2"
#endif

// Board pins (HW_PIN_*) and capabilities (RAVLIGHT_HAS_*) are defined in the
// board file force-included via -include "boards/<name>.h" in platformio.ini.

#endif
