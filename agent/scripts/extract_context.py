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
import re
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "agent" / "config.toml"
CHECK_RE = re.compile(r"^\s*-\s+\[(x|~| )\]\s+(.+)$")
HEADING_RE = re.compile(r"^(#+)\s+(.+)$")


def load_config() -> dict:
    return tomllib.loads(CONFIG_PATH.read_text(encoding="utf-8"))


def goal_config(config: dict, goal: str) -> dict:
    goals = config.get("goals", {})
    if goal not in goals:
        known = ", ".join(sorted(goals))
        raise SystemExit(f"unknown goal: {goal}. Known goals: {known}")
    return goals[goal]


def audit_path(config: dict, goal: str) -> Path:
    return ROOT / goal_config(config, goal)["audit"]


def read_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8").splitlines()


def section_lines(lines: list[str], section: str) -> list[tuple[int, str]]:
    if not section:
        for index, line in enumerate(lines):
            match = HEADING_RE.match(line)
            if match and len(match.group(1)) == 2:
                return list(enumerate(lines[index:], start=index + 1))
        return list(enumerate(lines, start=1))

    start = -1
    level = 0
    for index, line in enumerate(lines):
        match = HEADING_RE.match(line)
        if match and match.group(2).strip() == section:
            start = index
            level = len(match.group(1))
            break
    if start < 0:
        raise SystemExit(f"section not found: {section}")

    end = len(lines)
    for index in range(start + 1, len(lines)):
        match = HEADING_RE.match(lines[index])
        if match and len(match.group(1)) <= level:
            end = index
            break

    return list(enumerate(lines[start:end], start=start + 1))


def extract(config: dict, goal: str, section: str, limit: int) -> str:
    checked = partial = missing = 0
    unfinished: list[tuple[int, str, str]] = []
    selected = section_lines(read_lines(audit_path(config, goal)), section)
    in_fence = False

    for index, (line_number, line) in enumerate(selected):
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        match = CHECK_RE.match(line)
        if not match:
            continue
        mark = match.group(1)
        if mark == "x":
            checked += 1
        elif mark == "~":
            partial += 1
            text_parts = [match.group(2).strip()]
            next_index = index + 1
            while next_index < len(selected):
                _, continuation = selected[next_index]
                if CHECK_RE.match(continuation) or HEADING_RE.match(continuation):
                    break
                if continuation.startswith("  ") and continuation.strip():
                    text_parts.append(continuation.strip())
                next_index += 1
            unfinished.append((line_number, mark, " ".join(text_parts)))
        else:
            missing += 1
            text_parts = [match.group(2).strip()]
            next_index = index + 1
            while next_index < len(selected):
                _, continuation = selected[next_index]
                if CHECK_RE.match(continuation) or HEADING_RE.match(continuation):
                    break
                if continuation.startswith("  ") and continuation.strip():
                    text_parts.append(continuation.strip())
                next_index += 1
            unfinished.append((line_number, mark, " ".join(text_parts)))

    rows = "\n".join(
        f"- line {line}: [{mark}] {text}"
        for line, mark, text in unfinished[:limit]
    )
    if not rows:
        rows = "- No unfinished rows found in this scope."

    return f"""# XLang3 Python 3.14 Compact Agent Context

Goal:
Make XLang3 runtime compatible with Python 3.14 so CPython standard-library
.py files can run naturally on XLang3.

Runtime doctrine:
- Fix runtime primitives first: object model, call binding, descriptors, import
  system, code/frame/traceback, exceptions, VFS/open/_io, and required native
  dependency modules.
- Do not grow native C++ clones of pure Python stdlib modules as the main
  strategy.
- Native C++ is correct for CPython native/core dependency modules and
  performance-critical product modules.
- No debugpy-only shortcuts.
- No stubs, placeholder facades, or fake compatibility.
- Do not change fixtures blindly. For each output mismatch, confirm whether the
  runtime behavior is Python-compatible or the runtime needs fixing.
- Every compatibility change needs fixture coverage under tests/fixtures.
- Before claiming a feature is implemented, map it to a concrete fixture
  assertion. If no assertion exists, add one to the combined section fixture or
  add a focused fixture plus expected output.
- Include a compact feature-to-fixture coverage map in the batch summary.
- Update doc/python314-compat-audit.md truthfully.
- Use the fixed scripts below for validation and section checks; do not invent
  build/test commands during the batch.
- On Windows PowerShell, use `rg -F` for literal searches, especially for audit
  checkboxes, brackets, backticks, quotes, C++ punctuation, and Python syntax.
  Use regex mode only when the pattern is intentionally a regex.

Selected audit section:
{section or "whole audit"}

Agent goal:
{goal}

Audit counts:
- checked: {checked}
- partial: {partial}
- missing: {missing}

Next unfinished audit rows:
{rows}

Validation after Codex work:
- Run C:/Python/Python314/python.exe agent/scripts/build_release.py.
- Run C:/Python/Python314/python.exe agent/scripts/run_fixtures.py --xlang3 D:/CantorAI/xlang3/build/Release/xlang3.exe.
- Run C:/Python/Python314/python.exe agent/scripts/run_section_fixture.py --section "{section or "Standard Modules Foundation"}" when a quick selected-section check is useful.
- Run git diff --check.
- Commit and push only after validation passes.
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract compact context for the Python 3.14 compatibility loop.")
    parser.add_argument("--goal", default="", help="Goal name from agent/config.toml.")
    parser.add_argument("--section", default="")
    parser.add_argument("--limit", type=int, default=8)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    config = load_config()
    goal = args.goal or config.get("default_goal", "")
    text = extract(config, goal, args.section, args.limit)
    if args.out:
        path = Path(args.out)
        if not path.is_absolute():
            path = ROOT / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print(path)
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
