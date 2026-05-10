from SCons.Script import AlwaysBuild
from SCons.Script import Builder

Import("env")

def build_allfs(source, target, env):
    env.Execute("pio run -t buildfs")

env.AddCustomTarget(
    name="buildfs_all",
    dependencies=None,
    actions=[build_allfs],
    title="Build Filesystem All",
    description="Build filesystem for all env