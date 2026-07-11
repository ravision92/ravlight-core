#!/usr/bin/env python3
"""Generate ESP Web Tools manifests for the ravlight.com installer.

Scans release/latest/*.bin (merged 4 MB flash images produced by
scripts/rename_fw.py) and emits one manifest JSON per board into the
ravlight-site repo (expected as a sibling checkout: ../ravlight-site).
Manifests point at the raw.githubusercontent.com URL of each binary —
GitHub serves raw files with `Access-Control-Allow-Origin: *` so the
browser flasher can fetch them cross-origin without copying the
binaries into the site.

Run after each release (or add to the release checklist), then commit
and push the regenerated manifests in ravlight-site:
    python scripts/gen_manifests.py
"""

import json
import re
from pathlib import Path

ROOT      = Path(__file__).resolve().parent.parent
LATEST    = ROOT / "release" / "latest"
OUT_DIR   = ROOT.parent / "ravlight-site" / "manifests"
VERSION_H = ROOT / "include" / "version.h"

RAW_BASE = "https://raw.githubusercontent.com/ravision92/ravlight-core/master/release/latest"

# Friendly display names, keyed by the merged-bin basename (custom_fw_name).
FRIENDLY = {
    "veyron_xdmx2":            ("Veyron",  "XDMX rev2.2 (WT32-ETH01)"),
    "elyon_quinled_octa":      ("Elyon",   "QuinLED Dig-Octa"),
    "elyon_gledopto_elite4d":  ("Elyon",   "Gledopto Elite 4D (GL-C-618WL)"),
    "elyon_gledopto_elite2d":  ("Elyon",   "Gledopto Elite 2D (GL-C-616WL)"),
    "elyon_quinled_penta_plus":("Elyon",   "QuinLED Penta+"),
    "elyon_quinled_penta_deca":("Elyon",   "QuinLED Penta Deca"),
    "elyon_generic_esp32_test":("Elyon",   "Generic ESP32 DevKit (test)"),
    "orion_led_lifter_v5":     ("Orion",   "LED Lifter v5"),
    "axon_xdmx_v1_4":          ("Axon",    "XDMX v1.4"),
}


def firmware_version() -> str:
    m = re.search(r'FW_VERSION\s+"([^"]+)"', VERSION_H.read_text())
    return m.group(1) if m else "unknown"


def main() -> None:
    version = firmware_version()
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    bins = sorted(LATEST.glob("*.bin"))
    if not bins:
        raise SystemExit(f"no merged binaries found in {LATEST}")

    index = []
    for b in bins:
        base = b.stem
        fixture, board = FRIENDLY.get(base, (base, base))
        manifest = {
            "name": f"RavLight {fixture} — {board}",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [
                {
                    "chipFamily": "ESP32",
                    "parts": [
                        {"path": f"{RAW_BASE}/{b.name}", "offset": 0}
                    ],
                }
            ],
        }
        out = OUT_DIR / f"{base}.json"
        out.write_text(json.dumps(manifest, indent=2) + "\n")
        index.append({"id": base, "fixture": fixture, "board": board})
        print(f"  {out}")

    # Small index consumed by install.html to build the picker dynamically —
    # a new board only needs a FRIENDLY entry + re-run, no HTML edits.
    idx_out = OUT_DIR / "index.json"
    idx_out.write_text(json.dumps({"version": version, "boards": index}, indent=2) + "\n")
    print(f"  {idx_out}")
    print(f"{len(index)} manifests generated (firmware v{version})")


if __name__ == "__main__":
    main()
