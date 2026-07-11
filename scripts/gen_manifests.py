#!/usr/bin/env python3
"""Generate ESP Web Tools manifests for the ravlight.com installer.

Scans release/latest/*.bin (merged 4 MB flash images produced by
scripts/rename_fw.py) and, into the sibling ravlight-site checkout:
  1. copies each merged image into ravlight-site/firmware/
  2. emits one ESP Web Tools manifest per board into ravlight-site/manifests/

The binaries are served SAME-ORIGIN from ravlight.com/firmware/. This is
deliberate: GitHub *release-assets* (release-assets.githubusercontent.com)
do NOT send Access-Control-Allow-Origin, so a browser fetch from the
installer would be blocked by CORS. Serving them from the Pages site
itself sidesteps CORS entirely (same origin) and GitHub Pages also sets
`Access-Control-Allow-Origin: *` anyway.

Run after each release (or add to the release checklist), then commit
and push the regenerated firmware + manifests in ravlight-site:
    python scripts/gen_manifests.py
"""

import json
import re
import shutil
from pathlib import Path

ROOT      = Path(__file__).resolve().parent.parent
LATEST    = ROOT / "release" / "latest"
SITE      = ROOT.parent / "ravlight-site"
OUT_DIR   = SITE / "manifests"
FW_DIR    = SITE / "firmware"
VERSION_H = ROOT / "include" / "version.h"

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
    FW_DIR.mkdir(parents=True, exist_ok=True)

    bins = sorted(LATEST.glob("*.bin"))
    if not bins:
        raise SystemExit(f"no merged binaries found in {LATEST}")

    index = []
    for b in bins:
        base = b.stem
        fixture, board = FRIENDLY.get(base, (base, base))
        # Copy the merged image into the site so it is served same-origin.
        shutil.copy2(b, FW_DIR / b.name)
        manifest = {
            "name": f"RavLight {fixture} — {board}",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [
                {
                    "chipFamily": "ESP32",
                    # Same-origin relative path (installer lives at site root,
                    # manifests/ is one level down → ../firmware/).
                    "parts": [
                        {"path": f"../firmware/{b.name}", "offset": 0}
                    ],
                }
            ],
        }
        out = OUT_DIR / f"{base}.json"
        out.write_text(json.dumps(manifest, indent=2) + "\n")
        index.append({"id": base, "fixture": fixture, "board": board})
        print(f"  {out.name}  +  firmware/{b.name}")

    # Small index consumed by install.html to build the picker dynamically —
    # a new board only needs a FRIENDLY entry + re-run, no HTML edits.
    idx_out = OUT_DIR / "index.json"
    idx_out.write_text(json.dumps({"version": version, "boards": index}, indent=2) + "\n")
    print(f"  {idx_out}")
    print(f"{len(index)} manifests generated (firmware v{version})")


if __name__ == "__main__":
    main()
