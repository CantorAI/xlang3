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
from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


# Public CPython stdlib modules that are implemented as Python source in
# Python 3.14 and must not be replaced by native C++ facades in XLang3.
FORBIDDEN_PUBLIC_CPP_MODULES = {
    "abc",
    "argparse",
    "asyncio",
    "code",
    "collections",
    "contextlib",
    "copy",
    "copyreg",
    "ctypes",
    "ctypes.wintypes",
    "dataclasses",
    "dis",
    "enum",
    "fnmatch",
    "glob",
    "http",
    "http.client",
    "http.server",
    "inspect",
    "json",
    "linecache",
    "locale",
    "logging",
    "ntpath",
    "operator",
    "os",
    "pathlib",
    "pickle",
    "pkgutil",
    "posixpath",
    "queue",
    "re",
    "runpy",
    "shutil",
    "signal",
    "socket",
    "string",
    "subprocess",
    "sysconfig",
    "threading",
    "tokenize",
    "traceback",
    "types",
    "typing",
    "urllib",
    "urllib.parse",
    "warnings",
    "weakref",
    "zipfile",
}


# Public modules here are native/builtin/frozen in CPython or XLang3 product
# modules. Private underscore modules are dependency primitives and are allowed.
ALLOWED_PUBLIC_CPP_MODULES = {
    "atexit",
    "binascii",
    "builtins",
    "errno",
    "importlib._bootstrap",
    "importlib._bootstrap_external",
    "itertools",
    "marshal",
    "math",
    "msvcrt",
    "nt",
    "posix",
    "pyexpat",
    "pyexpat.errors",
    "pyexpat.model",
    "select",
    "sys",
    "sys._jit",
    "sys.monitoring",
    "task",
    "time",
    "unicodedata",
    "winreg",
    "zipimport",
    "zlib",
}


REGISTER_RE = re.compile(r'runtime\.register_module\("([^"]+)"')
BUILDER_RE = re.compile(r'NativeModuleBuilder\s+\w+\(runtime,\s*"([^"]+)"')
CPP_SOURCE_RE = re.compile(r"src/runtime/modules/[^\s)]+\.cpp")


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def find_registered_modules() -> list[tuple[Path, int, str, str]]:
    entries: list[tuple[Path, int, str, str]] = []
    roots = [ROOT / "src" / "runtime" / "modules", ROOT / "src" / "builtins"]
    for root in roots:
        for path in root.rglob("*.cpp"):
            for lineno, line in enumerate(read_text(path).splitlines(), 1):
                for pattern in (REGISTER_RE, BUILDER_RE):
                    match = pattern.search(line)
                    if match:
                        entries.append((path, lineno, match.group(1), line.strip()))
    return entries


def check_forbidden_public_modules(errors: list[str]) -> None:
    for path, lineno, name, line in find_registered_modules():
        if name.startswith("_"):
            continue
        if name in ALLOWED_PUBLIC_CPP_MODULES:
            continue
        if name in FORBIDDEN_PUBLIC_CPP_MODULES:
            errors.append(
                f"{path.relative_to(ROOT)}:{lineno}: forbidden public CPython "
                f"stdlib C++ module '{name}': {line}"
            )
        else:
            errors.append(
                f"{path.relative_to(ROOT)}:{lineno}: unclassified public C++ "
                f"module '{name}'. Add a policy decision before registering it."
            )


def check_unused_module_sources(errors: list[str]) -> None:
    cmake_text = read_text(ROOT / "CMakeLists.txt")
    cmake_sources = {match.group(0).replace("/", "\\") for match in CPP_SOURCE_RE.finditer(cmake_text)}
    for path in sorted((ROOT / "src" / "runtime" / "modules").rglob("*.cpp")):
        rel = str(path.relative_to(ROOT))
        if rel.replace("\\", "/") == "src/runtime/modules/system/source_encoding.cpp":
            continue
        if rel not in cmake_sources:
            errors.append(f"{rel}: module source exists but is not listed in CMakeLists.txt")


def main() -> int:
    errors: list[str] = []
    check_forbidden_public_modules(errors)
    check_unused_module_sources(errors)
    if errors:
        print("Module boundary check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    print("Module boundary check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
