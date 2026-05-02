Import("env")
import os, shutil, re, subprocess, sys

def get_fw_version(project_dir):
    version_h = os.path.join(project_dir, "include", "version.h")
    try:
        with open(version_h) as f:
            m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', f.read())
            return m.group(1) if m else "0.0.0"
    except:
        return "0.0.0"

def parse_partitions(project_dir):
    """Return {name: offset_int} from partitions.csv."""
    csv_path = os.path.join(project_dir, "partitions.csv")
    offsets = {}
    try:
        with open(csv_path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                parts = [p.strip() for p in line.split(',')]
                if len(parts) >= 4:
                    name, _, _, offset = parts[:4]
                    try:
                        offsets[name] = int(offset, 0)
                    except ValueError:
                        pass
    except Exception as e:
        print(f"  >> partitions.csv parse error: {e}")
    return offsets

def try_merge(env, build_dir, fw_path, fs_path, out_path, project_dir):
    # Locate esptool.py bundled with the platform
    esptool = None
    try:
        esptool = env.subst("$ESPTOOLPY")
    except Exception:
        pass
    if not esptool or not os.path.exists(str(esptool)):
        pkg_dir = env.subst("$PROJECT_PACKAGES_DIR")
        esptool = os.path.join(pkg_dir, "tool-esptoolpy", "esptool.py")
    if not os.path.exists(str(esptool)):
        print("  >> merge skipped: esptool.py not found")
        return

    # Build-time artefacts produced by PlatformIO
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")

    # boot_app0 shipped with the Arduino-ESP32 framework
    pkg_dir = env.subst("$PROJECT_PACKAGES_DIR")
    boot_app0 = os.path.join(pkg_dir, "framework-arduinoespressif32",
                              "tools", "partitions", "boot_app0.bin")

    for label, path in [("bootloader", bootloader),
                         ("partitions", partitions),
                         ("boot_app0",  boot_app0)]:
        if not os.path.exists(path):
            print(f"  >> merge skipped: {label} not found ({path})")
            return

    # Read partition offsets directly from CSV (single source of truth)
    part = parse_partitions(project_dir)
    app_offset  = part.get("app0",   0x10000)
    fs_offset   = part.get("spiffs", 0x2F0000)

    cmd = [
        sys.executable, str(esptool),
        "--chip", "esp32",
        "merge_bin",
        "--fill-flash-size", "4MB",
        "-o", out_path,
        "0x1000",                    bootloader,
        "0x8000",                    partitions,
        hex(0xe000),                 boot_app0,
        hex(app_offset),             fw_path,
        hex(fs_offset),              fs_path,
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  >> merge failed:\n{result.stderr.strip()}")
    else:
        print(f"  >> {os.path.basename(out_path)}")

def rename_bin(source, target, env):
    proj_dir  = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")
    src       = str(target[0])
    version   = get_fw_version(proj_dir)
    is_fs     = "littlefs" in os.path.basename(src)

    try:
        base = env.GetProjectOption("custom_fw_name")
    except:
        base = env.subst("$PIOENV")

    release_dir = os.path.join(proj_dir, "release")
    os.makedirs(release_dir, exist_ok=True)

    suffix = f"_fs_v{version}.bin" if is_fs else f"_fw_v{version}.bin"
    dst = os.path.join(release_dir, base + suffix)
    shutil.copy2(src, dst)
    print(f"\n  >> release/{os.path.basename(dst)}")

    # Attempt merged binary once both individual files are present
    fw_path = os.path.join(release_dir, base + f"_fw_v{version}.bin")
    fs_path = os.path.join(release_dir, base + f"_fs_v{version}.bin")
    if os.path.exists(fw_path) and os.path.exists(fs_path):
        merged = os.path.join(release_dir, base + f"_v{version}.bin")
        try_merge(env, build_dir, fw_path, fs_path, merged, proj_dir)
    print()

env.AddPostAction("$BUILD_DIR/firmware.bin",  rename_bin)
env.AddPostAction("$BUILD_DIR/littlefs.bin",  rename_bin)
