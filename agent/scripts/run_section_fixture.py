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

from win_no_popup import configure_no_popup_error_mode


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "agent" / "config.toml"


SECTION_FIXTURES = {
    "syntax": "module_and_statement_syntax.py",
    "Module And Statement Syntax": "module_and_statement_syntax.py",
    "Function And Class Syntax": "function_and_class_syntax.py",
    "Expression Syntax": "expression_syntax.py",
    "runtime_core": "core_value_and_object_model.py",
    "Core Value And Object Model": "core_value_and_object_model.py",
    "Functions And Calls": "functions_and_calls.py",
    "Exceptions": "exceptions.py",
    "builtin_types": "containers.py",
    "Containers": "containers.py",
    "Strings And Unicode": "strings_and_unicode.py",
    "native_dependencies": "imports_and_modules.py",
    "Imports And Modules": "imports_and_modules.py",
    "builtin_functions": "builtins.py",
    "Builtins": "builtins.py",
    "standard_modules": "standard_modules.py",
    "Standard Modules Foundation": "standard_modules.py",
    "system_stdlib": "system_stdlib.py",
    "filesystem_io": "builtins.py",
    "async_threads": "standard_modules.py",
    "debugger": "standard_modules.py",
}


def load_config() -> dict:
    return tomllib.loads(CONFIG_PATH.read_text(encoding="utf-8"))


def default_xlang3(config: dict) -> Path:
    return ROOT / config.get("repo", {}).get("release_exe", "build/Release/xlang3.exe")


def normalize(text: str) -> str:
    return text.replace("\r\n", "\n").rstrip()


def completed_output(result: subprocess.CompletedProcess[str]) -> str:
    return normalize((result.stdout or "") + (result.stderr or ""))


def main() -> int:
    configure_no_popup_error_mode()

    parser = argparse.ArgumentParser(description="Run one XLang3 compatibility section fixture.")
    parser.add_argument("--section", required=True, help="Audit section name.")
    parser.add_argument("--xlang3", default="", help="Optional xlang3 executable path.")
    parser.add_argument(
        "--case-timeout",
        type=float,
        default=60.0,
        help="Maximum seconds the section fixture may run before it is treated as failed.",
    )
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
    try:
        result = subprocess.run(
            [str(xlang3), str(fixture)],
            cwd=ROOT,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=args.case_timeout,
        )
    except subprocess.TimeoutExpired as exc:
        output = normalize((exc.stdout or "") + (exc.stderr or ""))
        detail = f"\nPartial output:\n{output}" if output else ""
        raise SystemExit(f"{fixture} timed out after {args.case_timeout:g} seconds.{detail}") from exc
    if result.returncode != 0:
        output = completed_output(result)
        detail = f"\nOutput:\n{output}" if output else "\nOutput: <empty>"
        raise SystemExit(f"{fixture_name} failed with exit code {result.returncode}{detail}")
    print(completed_output(result))
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
