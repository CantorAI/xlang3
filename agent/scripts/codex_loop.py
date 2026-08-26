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
import shlex
import shutil
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


def default_python(config: dict) -> str:
    configured = config.get("repo", {}).get("python_exe", "").strip()
    if configured:
        return configured
    return os.environ.get("PYTHON", "python")


def default_codex_command(config: dict) -> str:
    return config.get("codex", {}).get("command", "")


def default_commit_message_command(config: dict) -> str:
    return config.get("codex", {}).get("commit_message_command", "")


def resolve_codex_executable(config: dict | None = None) -> str:
    if config:
        codex_config = config.get("codex", {})
        configured_exe = codex_config.get("exe", "").strip()
        if configured_exe:
            path = Path(configured_exe)
            if not path.is_absolute():
                path = ROOT / path
            if not path.exists():
                raise SystemExit(f"Configured Codex executable does not exist: {path}")
            return str(path)

        configured_bin = codex_config.get("bin_dir", "").strip()
        if configured_bin:
            path = Path(configured_bin)
            if not path.is_absolute():
                path = ROOT / path
            candidate = newest_codex_executable(path)
            if candidate:
                return str(candidate)
            raise SystemExit(f"Configured Codex bin directory has no codex.exe: {path}")

    explicit = os.environ.get("XLANG3_CODEX_EXE", "").strip()
    if explicit:
        return explicit

    local_bin = Path.home() / "AppData" / "Local" / "OpenAI" / "Codex" / "bin"
    candidate = newest_codex_executable(local_bin)
    if candidate:
        return str(candidate)

    found = shutil.which("codex")
    if found:
        return found

    raise SystemExit(
        "Codex CLI was not found. Install/open Codex Desktop, or set XLANG3_CODEX_EXE."
    )


def newest_codex_executable(folder: Path) -> Path | None:
    if not folder.exists():
        return None
    candidates = sorted(
        folder.glob("*\\codex.exe"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    return candidates[0] if candidates else None


def expand_command_template(config: dict, command: str, output: str = "") -> str:
    if "{codex}" in command:
        command = command.replace("{codex}", resolve_codex_executable(config))
    if "{root}" in command:
        command = command.replace("{root}", str(ROOT))
    if "{output}" in command:
        command = command.replace("{output}", output)
    return command


def default_section(config: dict, goal: str) -> str:
    return goal_config(config, goal).get("default_section", "")


def default_limit(config: dict, goal: str) -> int:
    return int(goal_config(config, goal).get("default_limit", 8))


def default_iterations(config: dict, goal: str) -> int:
    # 0 means keep running batches until the selected audit scope is complete.
    return int(goal_config(config, goal).get("default_iterations", 1))


def default_commit_message(config: dict, goal: str) -> str:
    return goal_config(config, goal).get(
        "default_commit_message",
        "Advance Python 3.14 compatibility",
    )


def validate_codex_command(config: dict, command: str, output: str = "") -> str:
    stripped = command.strip()
    placeholders = {"...", "<codex-command>", "TODO", "todo"}
    if stripped in placeholders:
        raise SystemExit(
            f"Invalid Codex backend command: {stripped!r}. "
            "Pass a real command or use --dry-run/--status."
        )
    return expand_command_template(config, stripped, output)


def preflight_codex_command(command: str) -> None:
    if not command:
        return

    try:
        parts = shlex.split(command, posix=False)
    except ValueError as exc:
        raise SystemExit(f"Invalid Codex backend command syntax: {exc}") from exc
    if not parts:
        return

    tool = parts[0].strip('"')
    lowered = tool.lower()
    if lowered != "codex" and not lowered.endswith("\\codex.exe"):
        return

    try:
        result = subprocess.run(
            [tool, "--help"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        raise SystemExit(
            "Codex backend command is configured, but the Codex CLI did not launch. "
            f"Command: {tool} --help\n{exc}"
        ) from exc
    if result.returncode != 0:
        message = (result.stderr or result.stdout).strip()
        raise SystemExit(
            "Codex backend command is configured, but the Codex CLI did not launch. "
            f"Command: {tool} --help\n{message}"
        )


def run(command: list[str] | str, *, shell: bool = False) -> None:
    print()
    print("==", command if isinstance(command, str) else " ".join(command))
    result = subprocess.run(command, cwd=ROOT, shell=shell)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def run_with_input(command: str, stdin_text: str) -> None:
    print()
    print("==", command)
    result = subprocess.run(command, cwd=ROOT, shell=True, text=True, input=stdin_text)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def capture(command: list[str]) -> str:
    return subprocess.check_output(command, cwd=ROOT, text=True, stderr=subprocess.STDOUT)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def section_lines(lines: list[str], section: str) -> list[tuple[int, str]]:
    if not section:
        for index, line in enumerate(lines):
            match = HEADING_RE.match(line)
            if match and len(match.group(1)) == 2:
                return list(enumerate(lines[index:], start=index + 1))
        return list(enumerate(lines, start=1))

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
    in_fence = False
    for _, line in section_lines(lines, section):
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


def print_loop_status(config: dict, goal: str, section: str, limit: int) -> None:
    current_state = read_loop_state(config, goal)
    checked, partial, missing = audit_counts(config, goal, section)
    items = unfinished_items(config, goal, section, limit)

    print(f"Repo: {ROOT}")
    print(f"Goal: {goal}")
    print(f"Section: {section or 'whole audit'}")
    print(f"Audit counts: checked={checked}, partial={partial}, missing={missing}")

    if current_state.get("active"):
        print(f"Saved loop phase: {current_state.get('phase', '')}")
        print(f"Saved prompt: {current_state.get('prompt_path', '')}")
    else:
        print("Saved loop phase: none")

    stageable_changes = changed_paths()
    print(f"Stageable changed files: {len(stageable_changes)}")
    for path in stageable_changes[:20]:
        print(f"  {path}")
    if len(stageable_changes) > 20:
        print(f"  ... {len(stageable_changes) - 20} more")

    print("Next unfinished rows:")
    for line, mark, text in items:
        print(f"[{mark}] line {line}: {text}")


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
        print()
        print("== Codex backend")
        print(f"Prompt file: {prompt_path}")
        run_with_input(command, prompt)
        print("Codex backend finished.")
        return

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


def validate(cmake: str, xlang3: str, python_exe: str, skip_build: bool, skip_tests: bool) -> None:
    if not skip_build:
        print()
        print("Status: building Release xlang3")
        run([cmake, "--build", "build", "--config", "Release", "--target", "xlang3", "--", "/m"])
    if not skip_tests:
        print()
        print("Status: running compatibility fixtures")
        run([
            python_exe,
            "agent\\scripts\\run_fixtures.py",
            "--xlang3",
            xlang3,
        ])
        print()
        print("Status: checking patch whitespace")
        run(["git", "diff", "--check"])


def should_resume_as_codex_done(phase: str, saved_state: dict, stageable_changes: list[str]) -> bool:
    if phase not in {"prompt_written", "codex_running"}:
        return False
    baseline = sorted(saved_state.get("baseline_stageable_paths", []))
    current = sorted(stageable_changes)
    return current != baseline


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


def staged_diff_context(max_diff_chars: int = 12000) -> str:
    names = capture(["git", "diff", "--cached", "--name-only"]).strip()
    stat = capture(["git", "diff", "--cached", "--stat"]).strip()
    diff = capture(["git", "diff", "--cached", "--", *names.splitlines()]) if names else ""
    if len(diff) > max_diff_chars:
        diff = diff[:max_diff_chars] + "\n... diff truncated ...\n"
    return f"""Staged files:
{names}

Stat:
{stat}

Diff:
{diff}
"""


def sanitize_commit_message(text: str) -> str:
    for line in text.splitlines():
        candidate = line.strip().strip('"').strip("'")
        if not candidate:
            continue
        prefixes = ("commit message:", "subject:", "- ")
        lowered = candidate.lower()
        for prefix in prefixes:
            if lowered.startswith(prefix):
                candidate = candidate[len(prefix):].strip()
                lowered = candidate.lower()
        if candidate:
            return candidate[:120]
    return ""


def generate_commit_message(config: dict, goal: str, fallback: str) -> str:
    template = default_commit_message_command(config)
    if not template:
        return fallback

    output_path = runs_dir(config, goal) / "commit_message.txt"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    command = validate_codex_command(config, template, str(output_path))
    prompt = f"""Generate exactly one Git commit subject line for this staged XLang3 change.

Rules:
- Output only the subject line.
- Use imperative mood.
- Keep it under 72 characters if possible.
- Do not include markdown, bullets, quotes, or explanation.

{staged_diff_context()}
"""

    try:
        if output_path.exists():
            output_path.unlink()
        run_with_input(command, prompt)
        if output_path.exists():
            generated = sanitize_commit_message(output_path.read_text(encoding="utf-8"))
            if generated:
                print(f"Generated commit message: {generated}")
                return generated
    except (OSError, subprocess.SubprocessError, SystemExit) as exc:
        print(f"Commit message generation failed; using fallback. {exc}")
    return fallback


def commit_and_push(config: dict, goal: str, message: str) -> bool:
    paths = changed_paths()
    if not paths:
        print()
        print("No stageable compatibility changes found.")
        run(["git", "status", "--short"])
        return False

    print()
    print("Status: staging whitelisted files")
    for path in paths:
        print(f"  stage {path}")
        run(["git", "add", "--", path])

    if not message:
        raise SystemExit("--commit-message is required unless --no-commit is set")

    message = generate_commit_message(config, goal, message)

    print()
    print("Status: committing")
    run(["git", "commit", "-m", message])
    print()
    print("Status: pushing")
    run(["git", "push"])
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the XLang3 Python 3.14 Codex compatibility loop.")
    parser.add_argument("--goal", default="", help="Goal name from agent/config.toml.")
    parser.add_argument("--section", default=None, help="Optional audit section name.")
    parser.add_argument("--limit", type=int, default=0, help="Number of unfinished audit rows to include in the Codex prompt.")
    parser.add_argument(
        "--iterations",
        type=int,
        default=-1,
        help="Number of Codex work/validate/commit cycles. 0 means continuous until done.",
    )
    parser.add_argument("--codex-command", default="", help="Backend command. Use {prompt_file} or {prompt}; otherwise prompt file is appended.")
    parser.add_argument("--status", action="store_true", help="Show current goal/audit/resume status and exit.")
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
    section = args.section if args.section is not None else default_section(config, goal)
    limit = args.limit if args.limit > 0 else default_limit(config, goal)
    commit_message = args.commit_message or default_commit_message(config, goal)
    iterations = args.iterations if args.iterations >= 0 else default_iterations(config, goal)
    continuous = iterations == 0
    xlang3 = args.xlang3 or default_xlang3(config)
    python_exe = default_python(config)
    codex_command = validate_codex_command(config, args.codex_command or default_codex_command(config))
    cmake = resolve_cmake(args.cmake) if not args.dry_run and not args.skip_build else ""

    if args.reset_loop_state:
        clear_loop_state(config, goal)
        print(f"Cleared loop state for goal: {goal}")

    if args.status:
        print_loop_status(config, goal, section, limit)
        return 0

    saved_state = read_loop_state(config, goal)
    resume_phase = saved_state.get("phase", "") if saved_state.get("active") else ""
    if (
        not args.dry_run
        and not codex_command
        and resume_phase not in {"codex_done", "validated"}
    ):
        raise SystemExit(
            "No Codex backend command configured. Pass --codex-command, "
            "set [codex].command in agent/config.toml, or use --dry-run/--status."
        )
    if not args.dry_run and resume_phase not in {"codex_done", "validated"}:
        preflight_codex_command(codex_command)

    iteration = 1
    while continuous or iteration <= iterations:
        saved_state = read_loop_state(config, goal)
        resume_phase = saved_state.get("phase", "") if saved_state.get("active") else ""
        active_section = saved_state.get("section", section) if resume_phase else section
        stageable_changes = changed_paths()
        current_prompt_path = saved_state.get("prompt_path", "")
        current_baseline_paths = saved_state.get("baseline_stageable_paths", stageable_changes)

        print()
        print("=" * 72)
        iteration_total = "continuous" if continuous else str(iterations)
        print(f"XLang3 Python 3.14 compatibility loop iteration {iteration}/{iteration_total}")
        print(f"Repo: {ROOT}")
        print(f"Goal: {goal}")
        print(f"Section: {active_section or 'whole audit'}")
        if resume_phase:
            print(f"Resume phase: {resume_phase}")
            print(f"Stageable changed files: {len(stageable_changes)}")
        print("=" * 72)

        if should_resume_as_codex_done(resume_phase, saved_state, stageable_changes):
            print("Saved phase was interrupted, but stageable changes exist; resuming at validation.")
            resume_phase = "codex_done"
        elif resume_phase in {"prompt_written", "codex_running"}:
            prompt_path = Path(saved_state.get("prompt_path", ""))
            if not prompt_path.exists():
                print("Saved prompt is missing; starting a fresh iteration.")
                clear_loop_state(config, goal)
                saved_state = {}
                resume_phase = ""
            else:
                prompt = prompt_path.read_text(encoding="utf-8")
                print("Resuming saved Codex prompt.")
                write_loop_state(config, goal, {
                    "active": True,
                    "phase": "codex_running",
                    "goal": goal,
                    "section": active_section,
                    "prompt_path": str(prompt_path),
                    "iteration": iteration,
                    "baseline_stageable_paths": saved_state.get("baseline_stageable_paths", []),
                })
                try:
                    invoke_codex(codex_command, prompt_path, prompt)
                except SystemExit:
                    write_loop_state(config, goal, {
                        "active": True,
                        "phase": "prompt_written",
                        "goal": goal,
                        "section": active_section,
                        "prompt_path": str(prompt_path),
                        "iteration": iteration,
                        "baseline_stageable_paths": saved_state.get("baseline_stageable_paths", []),
                    })
                    raise
                write_loop_state(config, goal, {
                    "active": True,
                    "phase": "codex_done",
                    "goal": goal,
                    "section": active_section,
                    "prompt_path": str(prompt_path),
                    "iteration": iteration,
                    "baseline_stageable_paths": saved_state.get("baseline_stageable_paths", []),
                })
                resume_phase = "codex_done"
                current_prompt_path = str(prompt_path)
                current_baseline_paths = saved_state.get("baseline_stageable_paths", [])
        elif resume_phase and resume_phase not in {"codex_done", "validated"}:
            print("Saved state is not recognized; starting a fresh iteration.")
            clear_loop_state(config, goal)
            saved_state = {}
            resume_phase = ""

        if not resume_phase:
            checked, partial, missing = audit_counts(config, goal, active_section)
            items = unfinished_items(config, goal, active_section, limit)
            print()
            print(f"Audit counts: checked={checked}, partial={partial}, missing={missing}")
            if not items:
                print("No unfinished rows remain in this audit scope.")
                clear_loop_state(config, goal)
                break
            print("Next unfinished rows:")
            for line, mark, text in items:
                print(f"[{mark}] line {line}: {text}")

            prompt = compose_prompt(config, goal, active_section, items)
            prompt_path = write_prompt(config, goal, prompt, iteration)
            current_prompt_path = str(prompt_path)
            current_baseline_paths = stageable_changes
            print()
            print(f"Wrote prompt: {prompt_path}")

            write_loop_state(config, goal, {
                "active": True,
                "phase": "prompt_written",
                "goal": goal,
                "section": active_section,
                "prompt_path": str(prompt_path),
                "iteration": iteration,
                "baseline_stageable_paths": stageable_changes,
            })

            if args.dry_run:
                print()
                print("Dry run: compact prompt follows.")
                print(prompt)
                clear_loop_state(config, goal)
                break

            write_loop_state(config, goal, {
                "active": True,
                "phase": "codex_running",
                "goal": goal,
                "section": active_section,
                "prompt_path": str(prompt_path),
                "iteration": iteration,
                "baseline_stageable_paths": stageable_changes,
            })
            try:
                invoke_codex(codex_command, prompt_path, prompt)
            except SystemExit:
                write_loop_state(config, goal, {
                    "active": True,
                    "phase": "prompt_written",
                    "goal": goal,
                    "section": active_section,
                    "prompt_path": str(prompt_path),
                    "iteration": iteration,
                    "baseline_stageable_paths": stageable_changes,
                })
                raise
            write_loop_state(config, goal, {
                "active": True,
                "phase": "codex_done",
                "goal": goal,
                "section": active_section,
                "prompt_path": str(prompt_path),
                "iteration": iteration,
                "baseline_stageable_paths": stageable_changes,
            })

        if resume_phase == "codex_done" or not resume_phase:
            print()
            print("Status: validating Codex changes")
            validate(cmake, xlang3, python_exe, args.skip_build, args.skip_tests)
            write_loop_state(config, goal, {
                "active": True,
                "phase": "validated",
                "goal": goal,
                "section": active_section,
                "prompt_path": current_prompt_path,
                "iteration": iteration,
                "baseline_stageable_paths": current_baseline_paths,
            })

        if args.no_commit:
            print()
            print("No commit requested. Current status:")
            run(["git", "status", "--short"])
            clear_loop_state(config, goal)
            break
        else:
            committed = commit_and_push(config, goal, commit_message)
            clear_loop_state(config, goal)
            if continuous and not committed:
                print("Stopping continuous loop because this batch produced no stageable changes.")
                break

        iteration += 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
