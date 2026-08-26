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
import datetime as _dt
import json
import os
import re
import subprocess
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


def runs_dir(config: dict, goal: str) -> Path:
    return ROOT / goal_config(config, goal)["runs_dir"]


def state_path(config: dict, goal: str) -> Path:
    return runs_dir(config, goal) / "loop_state.json"


def default_xlang3(config: dict) -> str:
    return str(ROOT / config.get("repo", {}).get("release_exe", "build/Release/xlang3.exe"))


def run(command: list[str] | str, *, shell: bool = False) -> None:
    print()
    print("==", command if isinstance(command, str) else " ".join(command))
    result = subprocess.run(command, cwd=ROOT, shell=shell)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def section_lines(lines: list[str], section: str) -> list[tuple[int, str]]:
    numbered = list(enumerate(lines, start=1))
    if not section:
        return numbered

    start_index = -1
    start_level = 0
    for index, line in enumerate(lines):
        match = HEADING_RE.match(line)
        if match and match.group(2).strip() == section:
            start_index = index
            start_level = len(match.group(1))
            break

    if start_index < 0:
        raise SystemExit(f"section not found in audit: {section}")

    end_index = len(lines)
    for index in range(start_index + 1, len(lines)):
        match = HEADING_RE.match(lines[index])
        if match and len(match.group(1)) <= start_level:
            end_index = index
            break

    return list(enumerate(lines[start_index:end_index], start=start_index + 1))


def unfinished_items(config: dict, goal: str, section: str, limit: int) -> list[tuple[int, str, str]]:
    lines = read_text(audit_path(config, goal)).splitlines()
    selected = section_lines(lines, section)
    items: list[tuple[int, str, str]] = []
    for index, (line_number, line) in enumerate(selected):
        match = CHECK_RE.match(line)
        if not match:
            continue
        mark = match.group(1)
        if mark != "x":
            text_parts = [match.group(2).strip()]
            next_index = index + 1
            while next_index < len(selected):
                _, continuation = selected[next_index]
                if CHECK_RE.match(continuation) or HEADING_RE.match(continuation):
                    break
                if continuation.startswith("  ") and continuation.strip():
                    text_parts.append(continuation.strip())
                next_index += 1
            items.append((line_number, mark, " ".join(text_parts)))
            if len(items) >= limit:
                break
    return items[:limit]


def audit_counts(config: dict, goal: str, section: str) -> tuple[int, int, int]:
    lines = read_text(audit_path(config, goal)).splitlines()
    checked = partial = missing = 0
    for _, line in section_lines(lines, section):
        match = CHECK_RE.match(line)
        if not match:
            continue
        mark = match.group(1)
        if mark == "x":
            checked += 1
        elif mark == "~":
            partial += 1
        else:
            missing += 1
    return checked, partial, missing


def compose_prompt(config: dict, goal: str, section: str, items: list[tuple[int, str, str]]) -> str:
    next_items = "\n".join(
        f"- line {line}: [{mark}] {text}" for line, mark, text in items
    )
    if not next_items:
        next_items = "- No unfinished items found in the selected scope."

    checked, partial, missing = audit_counts(config, goal, section)
    return f"""# XLang3 Python 3.14 Compatibility Batch

This is a compact extracted context. Do not reread the full control markdown
unless something is ambiguous.

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
- No benchmark-specific code.
- No stubs, placeholder facades, or fake compatibility.
- Every compatibility change needs fixture coverage under tests/fixtures.
- Update doc/python314-compat-audit.md truthfully.
- Update agent/python314_compat/state.md if the checkpoint changes.
- The loop will build, test, commit, and push after your work.

Selected audit section:
{section or "whole audit"}

Agent goal:
{goal}

Audit counts:
- checked: {checked}
- partial: {partial}
- missing: {missing}

Next unfinished audit rows:
{next_items}

Implement one coherent compatibility batch now.
"""


def write_prompt(config: dict, goal: str, prompt: str, iteration: int) -> Path:
    output_dir = runs_dir(config, goal)
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = output_dir / f"{stamp}-iteration-{iteration:02d}.prompt.md"
    path.write_text(prompt, encoding="utf-8")
    return path


def read_loop_state(config: dict, goal: str) -> dict:
    path = state_path(config, goal)
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(f"invalid loop state file: {path}: {exc}") from exc


def write_loop_state(config: dict, goal: str, state: dict) -> None:
    path = state_path(config, goal)
    path.parent.mkdir(parents=True, exist_ok=True)
    state["updated_at"] = _dt.datetime.now().isoformat(timespec="seconds")
    path.write_text(json.dumps(state, indent=2, sort_keys=True), encoding="utf-8")


def clear_loop_state(config: dict, goal: str) -> None:
    path = state_path(config, goal)
    if path.exists():
        path.unlink()


def invoke_codex(command_template: str, prompt_path: Path, prompt: str) -> None:
    if not command_template:
        raise SystemExit(
            "No Codex backend command configured. Pass --codex-command or use --dry-run."
        )

    command = command_template
    if "{prompt_file}" in command:
        command = command.replace("{prompt_file}", str(prompt_path))
    elif "{prompt}" in command:
        command = command.replace("{prompt}", prompt.replace('"', '\\"'))
    else:
        command = f'{command} "{prompt_path}"'

    print()
    print("== Codex backend")
    print(f"Prompt file: {prompt_path}")
    run(command, shell=True)
    print("Codex backend finished.")


def resolve_cmake(explicit: str) -> str:
    if explicit:
        return explicit

    for folder in os.environ.get("PATH", "").split(os.pathsep):
        candidate = Path(folder) / "cmake.exe"
        if candidate.exists():
            return str(candidate)

    candidates = [
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)

    raise SystemExit("cmake.exe was not found. Pass --cmake.")


def validate(cmake: str, xlang3: str, skip_build: bool, skip_tests: bool) -> None:
    if not skip_build:
        print()
        print("Status: building Release xlang3")
        run([cmake, "--build", "build", "--config", "Release", "--target", "xlang3", "--", "/m"])
    if not skip_tests:
        print()
        print("Status: running compatibility fixtures")
        run([
            os.environ.get("PYTHON", "python"),
            "agent\\scripts\\run_fixtures.py",
            "--xlang3",
            xlang3,
        ])
        print()
        print("Status: checking patch whitespace")
        run(["git", "diff", "--check"])


def stageable(path: str) -> bool:
    normalized = path.replace("\\", "/")
    blocked_prefixes = (
        ".tmp_",
        "build/",
        ".vs/",
        "tests/fixtures/.vs/",
    )
    if normalized.startswith(blocked_prefixes):
        return False
    if re.match(r"^xlang3_.*(\.tmp|\.txt|\.py)$", normalized):
        return False
    if re.match(r"^xlang3_.*/", normalized):
        return False

    allowed_prefixes = (
        "agent/",
        "src/",
        "include/",
        "tests/",
        "doc/",
        "modules/",
        "tools/",
        "cmake/",
        "third_party/",
    )
    return normalized == "CMakeLists.txt" or normalized.startswith(allowed_prefixes)


def changed_paths() -> list[str]:
    output = subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True)
    paths: list[str] = []
    for line in output.splitlines():
        if len(line) < 4:
            continue
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        if stageable(path):
            paths.append(path)
    return paths


def commit_and_push(message: str) -> None:
    paths = changed_paths()
    if not paths:
        print()
        print("No stageable compatibility changes found.")
        run(["git", "status", "--short"])
        return

    print()
    print("Status: staging whitelisted files")
    for path in paths:
        print(f"  stage {path}")
        run(["git", "add", "--", path])

    if not message:
        raise SystemExit("--commit-message is required unless --no-commit is set")

    print()
    print("Status: committing")
    run(["git", "commit", "-m", message])
    print()
    print("Status: pushing")
    run(["git", "push"])


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the XLang3 Python 3.14 Codex compatibility loop.")
    parser.add_argument("--goal", default="", help="Goal name from agent/config.toml.")
    parser.add_argument("--section", default="", help="Optional audit section name.")
    parser.add_argument("--limit", type=int, default=8, help="Number of unfinished audit rows to include in the Codex prompt.")
    parser.add_argument("--iterations", type=int, default=1, help="Number of Codex work/validate/commit cycles.")
    parser.add_argument("--codex-command", default="", help="Backend command. Use {prompt_file} or {prompt}; otherwise prompt file is appended.")
    parser.add_argument("--dry-run", action="store_true", help="Write and print prompts without invoking Codex, building, testing, or committing.")
    parser.add_argument("--no-commit", action="store_true", help="Validate but do not commit.")
    parser.add_argument("--commit-message", default="", help="Commit message for validated changes.")
    parser.add_argument("--cmake", default="", help="Path to cmake.exe.")
    parser.add_argument("--xlang3", default="", help="Path to built xlang3.exe.")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-tests", action="store_true")
    parser.add_argument("--reset-loop-state", action="store_true", help="Discard saved resume state for this goal.")
    args = parser.parse_args()

    os.chdir(ROOT)
    config = load_config()
    goal = args.goal or config.get("default_goal", "")
    xlang3 = args.xlang3 or default_xlang3(config)
    cmake = resolve_cmake(args.cmake) if not args.dry_run and not args.skip_build else ""

    if args.reset_loop_state:
        clear_loop_state(config, goal)
        print(f"Cleared loop state for goal: {goal}")

    for iteration in range(1, args.iterations + 1):
        saved_state = read_loop_state(config, goal)
        resume_phase = saved_state.get("phase", "") if saved_state.get("active") else ""
        active_section = saved_state.get("section", args.section) if resume_phase else args.section

        print()
        print("=" * 72)
        print(f"XLang3 Python 3.14 compatibility loop iteration {iteration}/{args.iterations}")
        print(f"Repo: {ROOT}")
        print(f"Goal: {goal}")
        print(f"Section: {active_section or 'whole audit'}")
        if resume_phase:
            print(f"Resume phase: {resume_phase}")
        print("=" * 72)

        if resume_phase and resume_phase not in {"codex_done", "validated"}:
            print("Saved state is from an incomplete prompt/backend phase; starting a fresh iteration.")
            clear_loop_state(config, goal)
            saved_state = {}
            resume_phase = ""

        if not resume_phase:
            checked, partial, missing = audit_counts(config, goal, active_section, )
            items = unfinished_items(config, goal, active_section, args.limit)
            print()
            print(f"Audit counts: checked={checked}, partial={partial}, missing={missing}")
            print("Next unfinished rows:")
            for line, mark, text in items:
                print(f"[{mark}] line {line}: {text}")

            prompt = compose_prompt(config, goal, active_section, items)
            prompt_path = write_prompt(config, goal, prompt, iteration)
            print()
            print(f"Wrote prompt: {prompt_path}")

            write_loop_state(config, goal, {
                "active": True,
                "phase": "prompt_written",
                "goal": goal,
                "section": active_section,
                "prompt_path": str(prompt_path),
                "iteration": iteration,
            })

            if args.dry_run:
                print()
                print("Dry run: compact prompt follows.")
                print(prompt)
                clear_loop_state(config, goal)
                continue

            invoke_codex(args.codex_command, prompt_path, prompt)
            write_loop_state(config, goal, {
                "active": True,
                "phase": "codex_done",
                "goal": goal,
                "section": active_section,
                "prompt_path": str(prompt_path),
                "iteration": iteration,
            })

        if resume_phase == "codex_done" or not resume_phase:
            print()
            print("Status: validating Codex changes")
            validate(cmake, xlang3, args.skip_build, args.skip_tests)
            write_loop_state(config, goal, {
                "active": True,
                "phase": "validated",
                "goal": goal,
                "section": active_section,
                "prompt_path": saved_state.get("prompt_path", ""),
                "iteration": iteration,
            })

        if args.no_commit:
            print()
            print("No commit requested. Current status:")
            run(["git", "status", "--short"])
        else:
            commit_and_push(args.commit_message)
            clear_loop_state(config, goal)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
