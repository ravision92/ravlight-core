Import("env")
import os, shutil, re

def get_fw_version(project_dir):
    version_h = os.path.join(project_dir, "include", "version.h")
    try:
        with open(version_h) as f:
            m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', f.read())
            return m.group(1) if m else "0.0.0"
    except:
        return "0.0.0"

def rename_bin(source, target, env):
    proj_dir  = env.subst("$PROJECT_DIR")
    src       = str(target[0])
    version   = get_fw_version(proj_dir)
    is_fs     = "littlefs" in os.path.basename(src)

    try:
        base = env.GetProjectOption("custom_fw_name")
    except:
        base = env.subst("$PIOENV")

    suffix = f"_fs_v{version}.bin" if is_fs else f"_fw_v{version}.bin"
    dst = os.path.join(proj_dir, base + suffix)
    shutil.copy2(src, dst)
    print(f"\n  >> {os.path.basename(dst)}\n")

env.AddPostAction("$BUILD_DIR/firmware.bin",  rename_bin)
env.AddPostAction("$BUILD_DIR/littlefs.bin",  rename_bin)
