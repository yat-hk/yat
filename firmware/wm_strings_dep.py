"""Makes the build notice when YAT's captive-portal strings header changes.

WiFiManager.h pulls the strings in as `#include WM_STRINGS_FILE` — a macro, not
a literal path. PlatformIO's SCons scanner is regex-based and cannot expand it,
so include/yat_wm_strings.h never enters the dependency graph. Editing the
portal's copy then leaves a stale WiFiManager.cpp.o behind and `pio run` reports
SUCCESS while the device keeps serving the previous page — a quiet way to ship
the wrong portal, and it cost one debugging round already.

The fix is a content signature folded into the compiler flags: a changed header
changes -DYAT_WM_STRINGS_SIG, which PlatformIO does track, so everything that
sees the define gets rebuilt. The macro is never referenced in code; its only
job is to be different. (env.AddBuildMiddleware with a *WiFiManager.cpp pattern
looks like the tidier answer but does not fire for library sources — verified,
not assumed.)
"""

import hashlib
import os

Import("env")  # noqa: F821 — injected by PlatformIO

_HEADER = os.path.join(env.subst("$PROJECT_DIR"), "include", "yat_wm_strings.h")  # noqa: F821

with open(_HEADER, "rb") as fh:
    _SIG = hashlib.md5(fh.read()).hexdigest()[:12]

env.Append(CPPDEFINES=[("YAT_WM_STRINGS_SIG", "s" + _SIG)])  # noqa: F821
