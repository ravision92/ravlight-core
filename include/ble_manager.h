#pragma once
#ifdef RAVLIGHT_MODULE_BLE

#define BLE_ACTIVE_WINDOW_MS 600000UL   // 10 minutes

void initBLE();
void deinitBLE();
void updateBleAdvertising();
void updateBLE();       // call from loop() — handles deferred shutdown
bool isBleActive();

#endif // RAVLIGHT_MODULE_BLE
