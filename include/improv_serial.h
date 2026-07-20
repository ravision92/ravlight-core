#pragma once

// Improv-Serial responder (https://www.improv-wifi.com/serial/).
//
// Lets the ravlight.com browser installer (ESP Web Tools) provision WiFi over
// the same USB serial link used for flashing: right after the flash, the
// browser prompts for SSID/password, sends them over serial, and this responder
// connects and reports the device URL back — no AP hopping.
//
// It is the front-door for the UART when compiled in: it consumes Improv frames
// and forwards every other byte to the text serial console (serialConsoleFeedChar),
// so the human console still works. Poll from loop() in place of
// checkSerialConsole().
//
// Enabled per board via RAVLIGHT_MODULE_IMPROV; a no-op when the module is off
// or serial is disabled for the board.
void checkImprovSerial();
