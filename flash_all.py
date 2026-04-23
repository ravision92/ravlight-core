import sys
from SCons.Script import Import, AlwaysBuild

Import("env")

python = sys.executable  # Usa lo stesso interprete che esegue PlatformIO

def flash_all(target, source, env):
    env.Execute(f'"{python}" -m platformio run -t upload')
    env.Execute(f'"{python}" -m platformio run -t uploadfs')
    env.Execute(f'"{python}" -m platformio device monitor --echo')

flash_target = env.Alias("flash_all", None, flash_all)
AlwaysBuild(flash_target)

