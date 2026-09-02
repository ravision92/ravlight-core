#pragma once
#define FW_VERSION  "2.24.0"

// custom_fw_name of this build (e.g. "elyon_quinled_octa"), injected as a -D
// flag by scripts/embed_assets.py. It is the OTA feed key and the only string
// that identifies which binary belongs on this board — BOARD_NAME is a display
// label ("XDMX v2.2") and cannot be mapped back to it. Guard in case it is
// ever missing.
#ifndef RAVLIGHT_FW_BASE
#define RAVLIGHT_FW_BASE "unknown"
#endif
