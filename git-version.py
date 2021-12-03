import subprocess
import time

Import("env")

def Command(args):
    ret = subprocess.run(args, stdout=subprocess.PIPE, text=True)
    return ret.stdout.strip()

def CreateVersion():
    version = Command(["git", "rev-parse", "--short", "HEAD"])
    clean = Command(["git", "status", "-uno", "--porcelain"])
    if clean.strip() != "":
        version += "!"
    return version

env.Append(BUILD_FLAGS=[
    '-D GIT_VERSION=\\\"%s\\\"' % CreateVersion()
    ])
