"""Stamps the current git short revision into the build as YAT_BUILD_REV.

FW_VERSION in main.cpp appends this to its base version, so a dev flash
identifies its exact commit in STATUS / /api/status / the boot banner
("0.4.0-dev+34a4b68", with "+dirty" when the working tree has edits).
Without it every build of a development month reports the same string and
no one can tell which firmware a device is actually running.
"""
import subprocess

Import("env")


def _rev():
    try:
        rev = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=env["PROJECT_DIR"], text=True).strip()
        dirty = subprocess.run(
            ["git", "diff", "--quiet", "HEAD"],
            cwd=env["PROJECT_DIR"]).returncode != 0
        return rev + ("-dirty" if dirty else "")
    except Exception:
        return "unknown"


env.Append(CPPDEFINES=[("YAT_BUILD_REV", env.StringifyMacro("+" + _rev()))])
