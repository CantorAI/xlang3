# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import subprocess
from pathlib import Path


DEFAULT_MODULES = [
    "codecs",
    "dis",
    "getpass",
    "http",
    "http.client",
    "io",
    "json",
    "locale",
    "inspect",
    "marshal",
    "opcode",
    "os",
    "pathlib",
    "pickle",
    "platform",
    "pkgutil",
    "re",
    "signal",
    "site",
    "stat",
    "subprocess",
    "struct",
    "sys",
    "sysconfig",
    "threading",
    "time",
    "tokenize",
    "urllib.parse",
    "winreg",
    "xmlrpc.client",
    "zipimport",
    "importlib.resources",
    "operator",
    "itertools",
    "collections",
    "queue",
]


def main() -> int:
    parser = argparse.ArgumentParser(description="Probe individual XLang3 imports with short timeouts.")
    parser.add_argument("--xlang3", default=str(Path("build") / "Release" / "xlang3.exe"))
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("modules", nargs="*")
    args = parser.parse_args()

    modules = args.modules or DEFAULT_MODULES
    root = Path(__file__).resolve().parents[2]
    exe = (root / args.xlang3).resolve() if not Path(args.xlang3).is_absolute() else Path(args.xlang3)

    failed = 0
    for module_name in modules:
        code = f'import {module_name}; print("OK {module_name}")'
        try:
            result = subprocess.run(
                [str(exe), "-c", code],
                cwd=str(root),
                capture_output=True,
                text=True,
                timeout=args.timeout,
            )
        except subprocess.TimeoutExpired:
            print(f"TIMEOUT {module_name}", flush=True)
            failed += 1
            continue

        output = (result.stdout + result.stderr).strip().splitlines()
        tail = output[-1] if output else ""
        status = "OK" if result.returncode == 0 else "FAIL"
        print(f"{status} {module_name}: {tail[:180]}", flush=True)
        if result.returncode != 0:
            failed += 1

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
