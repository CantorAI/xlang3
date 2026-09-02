# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import os
import sys


root = "xlang3_async_pkg_probe"
pkg = root + "/pkg"
os.makedirs(pkg, exist_ok=True)

with open(pkg + "/__init__.py", "w", encoding="utf-8") as f:
    f.write("from .base_events import *\n")

with open(pkg + "/base_events.py", "w", encoding="utf-8") as f:
    f.write(
        "import collections\n"
        "import collections.abc\n"
        "import concurrent.futures\n"
        "import errno\n"
        "import heapq\n"
        "import itertools\n"
        "import os\n"
        "import socket\n"
        "import stat\n"
        "import subprocess\n"
        "import threading\n"
        "import time\n"
        "import traceback\n"
        "import sys\n"
        "import warnings\n"
        "import weakref\n"
        "try:\n"
        "    import ssl\n"
        "except ImportError:\n"
        "    ssl = None\n"
        "from . import constants\n"
        "__all__ = ('constants',)\n"
    )

with open(pkg + "/constants.py", "w", encoding="utf-8") as f:
    f.write(
        "import enum\n"
        "class Mode(enum.Enum):\n"
        "    A = enum.auto()\n"
        "    B = enum.auto()\n"
    )

sys.path.insert(0, root)
try:
    import pkg

    print("package-relative-enum", pkg.constants.Mode.B.value)
finally:
    if root in sys.path:
        sys.path.remove(root)
