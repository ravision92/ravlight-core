"""Pre-build: work around an upstream esp_dmx bug (someweisguy/esp_dmx,
rdm/responder/dmx_setup.c) where rdm_rhd_get_dmx_personality_description()
declares its response struct without zero-initializing it:

    rdm_dmx_personality_description_t pd;
    ...
    memcpy(pd.description, desc, strnlen(desc, RDM_ASCII_SIZE_MAX));
    const size_t pdl = sizeof(pd);
    return rdm_write_ack(dmx_num, header, definition->get.response.format, &pd, pdl);

The memcpy only fills the personality name's actual length, but pdl covers
the whole struct — so the unwritten tail of pd.description is uninitialized
stack garbage, sent on the wire as part of RDM_PID_DMX_PERSONALITY_DESCRIPTION.
RDM consoles (Onyx, DMX Workshop) render that garbage as placeholder glyphs
after the real name (e.g. "Default???!???[]").

Patches the vendored copy in .pio/libdeps/<env>/esp_dmx after PlatformIO's
Library Dependency Finder has fetched it, adding a `= {0}` zero-initializer.
Idempotent — skips envs that don't depend on esp_dmx, and skips files
already patched (checked via a marker comment) so re-running is a no-op.
"""
import re
from pathlib import Path

Import("env")  # noqa: F821 — PlatformIO injects this

MARKER = "// RavLight: zero-init patch (see scripts/patch_esp_dmx.py)"
TARGET_REL = Path("esp_dmx") / "src" / "rdm" / "responder" / "dmx_setup.c"
OLD = "rdm_dmx_personality_description_t pd;"
NEW = f"rdm_dmx_personality_description_t pd = {{0}};  {MARKER}"


def patch_file(path: Path) -> None:
    text = path.read_text()
    if MARKER in text:
        return  # already patched
    if OLD not in text:
        print(f"[patch_esp_dmx] WARNING: expected line not found in {path} "
              "— upstream file may have changed, skipping")
        return
    text = text.replace(OLD, NEW, 1)
    path.write_text(text)
    print(f"[patch_esp_dmx] zero-initialized `pd` in {path}")


def main():
    libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR"))  # noqa: F821
    pioenv = env["PIOENV"]  # noqa: F821
    target = libdeps_dir / pioenv / TARGET_REL
    if not target.is_file():
        return  # this env doesn't depend on esp_dmx
    patch_file(target)


main()
