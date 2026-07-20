#pragma once

// Minimal serial recovery console (WLED-style). Poll from loop(). Lets an
// operator recover a network-unreachable node over USB serial: reboot, WiFi
// provisioning, toggle ESP-NOW (to escape the WiFi-heap-starves-EMAC state),
// factory reset, and a status dump — without the web UI or a reflash.
//
// Compiled to a no-op when serial is disabled for the board (e.g. QuinLED
// Octa, where the UART pins drive LED outputs).
void checkSerialConsole();

// Feed one incoming serial byte into the console line buffer (dispatches on
// newline). Exposed so the Improv-Serial front-door can forward the bytes it
// doesn't consume to the text console — both share the one UART. No-op when
// serial is disabled.
void serialConsoleFeedChar(char c);
