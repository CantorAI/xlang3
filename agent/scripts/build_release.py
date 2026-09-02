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

import subprocess
import sys
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "agent" / "config.toml"


def load_config() -> dict:
    return tomllib.loads(CONFIG_PATH.read_text(encoding="utf-8"))


def cmake_exe(config: dict) -> str:
    configured = config.get("repo", {}).get("cmake_exe", "").strip()
    return configured or "cmake"


def main() -> int:
    config = load_config()
    boundary_check = subprocess.run(
        [sys.executable, "agent/scripts/check_module_boundaries.py"],
        cwd=ROOT,
    )
    if boundary_check.returncode != 0:
        return boundary_check.returncode
    command = [
        cmake_exe(config),
        "--build",
        "build",
        "--config",
        "Release",
        "--target",
        "xlang3",
        "--",
        "/m:1",
    ]
    return subprocess.run(command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
