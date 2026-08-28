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

import argparse
import subprocess
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "agent" / "config.toml"


SECTION_FIXTURES = {
    "Module And Statement Syntax": "module_and_statement_syntax.py",
    "Function And Class Syntax": "function_and_class_syntax.py",
    "Expression Syntax": "expression_syntax.py",
    "Core Value And Object Model": "core_value_and_object_model.py",
    "Functions And Calls": "functions_and_calls.py",
    "Exceptions": "exceptions.py",
    "Containers": "containers.py",
    "Strings And Unicode": "strings_and_unicode.py",
    "Imports And Modules": "imports_and_modules.py",
    "Builtins": "builtins.py",
    "Standard Modules Foundation": "standard_modules.py",
}


def load_config() -> dict:
    return tomllib.loads(CONFIG_PATH.read_text(encoding="utf-8"))


def default_xlang3(config: dict) -> Path:
    return ROOT / config.get("repo", {}).get("release_exe", "build/Release/xlang3.exe")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run one XLang3 compatibility section fixture.")
    parser.add_argument("--section", required=True, help="Audit section name.")
    parser.add_argument("--xlang3", default="", help="Optional xlang3 executable path.")
    args = parser.parse_args()

    config = load_config()
    fixture_name = SECTION_FIXTURES.get(args.section)
    if not fixture_name:
        known = ", ".join(sorted(SECTION_FIXTURES))
        raise SystemExit(f"no section fixture configured for {args.section!r}. Known: {known}")

    xlang3 = Path(args.xlang3) if args.xlang3 else default_xlang3(config)
    if not xlang3.is_absolute():
        xlang3 = ROOT / xlang3
    fixture = ROOT / "tests" / "fixtures" / "compat_sections" / fixture_name
    result = subprocess.run([str(xlang3), str(fixture)], cwd=ROOT)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
