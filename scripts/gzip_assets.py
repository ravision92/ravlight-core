"""Pre-build: gzip text assets in data/ so the firmware can serve them with
Content-Encoding: gzip. Each <file> gets a sibling <file>.gz, regenerated only
when the source is newer. Stale .gz files (source removed) are deleted.

The firmware route handlers look for <path>.gz first and fall back to <path>.
"""
import gzip
import os
from pathlib import Path

Import("env")  # noqa: F821 — PlatformIO injects this

ROOT       = Path(env["PROJECT_DIR"])  # noqa: F821
DATA_DIR   = ROOT / "data"
EXTENSIONS = {".html", ".css", ".js"}
# Files we deliberately exclude — legacy backups or files only kept as reference.
SKIP_NAMES = {"index.html.legacy", "style.css.bak"}


def should_gzip(path: Path) -> bool:
    if path.name in SKIP_NAMES:
        return False
    if path.suffix not in EXTENSIONS:
        return False
    if path.name.endswith(".gz"):
        return False
    return True


def gzip_one(src: Path) -> tuple[int, int]:
    dst = src.with_suffix(src.suffix + ".gz")
    if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
        return (src.stat().st_size, dst.stat().st_size)
    raw = src.read_bytes()
    # mtime=0 keeps the .gz byte-stable across rebuilds so LittleFS image hashes
    # stay deterministic when nothing changed.
    with gzip.GzipFile(filename="", mode="wb", fileobj=open(dst, "wb"),
                       compresslevel=9, mtime=0) as gz:
        gz.write(raw)
    return (len(raw), dst.stat().st_size)


def sweep_stale() -> int:
    removed = 0
    for gz in DATA_DIR.rglob("*.gz"):
        src = gz.with_suffix("")
        if not src.exists():
            gz.unlink()
            removed += 1
    return removed


def main():
    if not DATA_DIR.is_dir():
        return
    total_raw, total_gz, count = 0, 0, 0
    for path in DATA_DIR.rglob("*"):
        if not path.is_file() or not should_gzip(path):
            continue
        raw, gzs = gzip_one(path)
        total_raw += raw
        total_gz  += gzs
        count     += 1
    removed = sweep_stale()
    if count:
        ratio = (1 - total_gz / total_raw) * 100 if total_raw else 0
        print(f"[gzip_assets] {count} files: {total_raw} -> {total_gz} bytes "
              f"({ratio:.1f}% saved){', cleaned %d stale .gz' % removed if removed else ''}")


main()
