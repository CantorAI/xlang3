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
import msvcrt
from pathlib import Path

from win_no_popup import configure_no_popup_error_mode


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "agent" / "config.toml"


CHECK_RE = re.compile(r"^\s*-\s+\[(x|~| )\]\s+(.+)$")
HEADING_RE = re.compile(r"^(#+)\s+(.+)$")
SESSION_ID_RE = re.compile(r"\bsession id:\s*([0-9a-fA-F-]{36})\b")
TASK_ALIASES = {
    "Module And Statement Syntax": "syntax",
    "Function And Class Syntax": "runtime_core",
    "Expression Syntax": "syntax",
    "Assignment Syntax": "syntax",
    "Core Value And Object Model": "runtime_core",
    "Functions And Calls": "runtime_core",
    "Exceptions": "runtime_core",
    "Containers": "builtin_types",
    "Strings And Unicode": "builtin_types",
    "Imports And Modules": "native_dependencies",
    "Builtins": "builtin_functions",
    "Standard Modules Foundation": "standard_modules",
    "Async, Tasks, And Threads": "async_threads",
    "Filesystem And IO": "filesystem_io",
    "Debugger Compatibility": "debugger",
}


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


def task_dir(config: dict, goal: str) -> Path:
    configured = goal_config(config, goal).get("tasks_dir", "")
    if configured:
        return ROOT / configured
    return ROOT / "agent" / goal / "tasks"


def task_slug(value: str) -> str:
    value = TASK_ALIASES.get(value, value)
    value = value.removesuffix(".md")
    value = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").lower()
    return value


def task_path(config: dict, goal: str, selector: str) -> Path | None:
    folder = task_dir(config, goal)
    if not selector or not folder.exists():
        return None

    direct = Path(selector)
    candidates = []
    if direct.suffix == ".md":
        candidates.append(folder / direct.name)
    else:
        candidates.append(folder / f"{selector}.md")
        candidates.append(folder / f"{task_slug(selector)}.md")

    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def task_file_has_unfinished(path: Path) -> bool:
    in_fence = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        match = CHECK_RE.match(line)
        if match and match.group(1) != "x":
            return True
    return False


def queued_task_paths(config: dict, goal: str) -> list[Path]:
    goal_info = goal_config(config, goal)
    queue_value = goal_info.get("queue", "")
    folder = task_dir(config, goal)
    paths: list[Path] = []
    seen: set[Path] = set()

    if queue_value:
        queue_file = ROOT / queue_value
        if queue_file.exists():
            for line in queue_file.read_text(encoding="utf-8").splitlines():
                match = re.search(r"`([^`]+\.md)`", line)
                if not match:
                    continue
                raw = match.group(1).replace("\\", "/")
                if not raw.startswith("tasks/"):
                    continue
                candidate = folder / Path(raw).name
                candidate = candidate.resolve()
                if candidate.exists() and candidate not in seen:
                    paths.append(candidate)
                    seen.add(candidate)

    if folder.exists():
        for candidate in sorted(folder.glob("*.md")):
            resolved = candidate.resolve()
            if resolved not in seen:
                paths.append(resolved)
                seen.add(resolved)

    return paths


def auto_task_selector(config: dict, goal: str) -> str:
    for path in queued_task_paths(config, goal):
        if task_file_has_unfinished(path):
            return path.stem
    return ""


def resolve_section(config: dict, goal: str, selector: str) -> str:
    if selector.strip().lower() in {"", "auto", "next"}:
        return auto_task_selector(config, goal)
    return selector


def runs_dir(config: dict, goal: str) -> Path:
    return ROOT / goal_config(config, goal)["runs_dir"]


def state_path(config: dict, goal: str) -> Path:
    return runs_dir(config, goal) / "loop_state.json"


def stop_request_path(config: dict, goal: str) -> Path:
    return runs_dir(config, goal) / "stop.requested"


def loop_lock_path(config: dict, goal: str) -> Path:
    return runs_dir(config, goal) / "loop.lock"


def session_id_path(config: dict, goal: str) -> Path:
    return runs_dir(config, goal) / "codex_session_id.txt"


def lessons_path(config: dict, goal: str) -> Path:
    configured = goal_config(config, goal).get("lessons", "")
    if configured:
        return ROOT / configured
    return ROOT / "agent" / goal / "lessons.md"


def default_xlang3(config: dict) -> str:
    return str(ROOT / config.get("repo", {}).get("release_exe", "build/Release/xlang3.exe"))


def default_python(config: dict) -> str:
    configured = config.get("repo", {}).get("python_exe", "").strip()
    if configured:
        return configured
    return os.environ.get("PYTHON", "python")


def default_cmake(config: dict) -> str:
    return config.get("repo", {}).get("cmake_exe", "").strip()


def command_quote(value: str) -> str:
    return '"' + value.replace('"', '\"') + '"'


def build_command_text(config: dict) -> str:
    return f"{command_quote(default_python(config))} agent\\scripts\\build_release.py"


def fixture_command_text(config: dict) -> str:
    timeout = int(config.get("repo", {}).get("fixture_case_timeout", 60))
    return (
        f"{command_quote(default_python(config))} agent\\scripts\\run_fixtures.py "
        f"--xlang3 {command_quote(default_xlang3(config))} "
        f"--case-timeout {timeout}"
    )


def section_fixture_command_text(config: dict, section: str) -> str:
    timeout = int(config.get("repo", {}).get("fixture_case_timeout", 60))
    return (
        f"{command_quote(default_python(config))} agent\\scripts\\run_section_fixture.py "
        f"--section {command_quote(section)} --case-timeout {timeout}"
    )


def default_codex_command(config: dict) -> str:
    return config.get("codex", {}).get("command", "")


def default_codex_resume_command(config: dict) -> str:
    return config.get("codex", {}).get("resume_command", "")


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


def expand_session_command_template(config: dict, command: str, session_id: str) -> str:
    return expand_command_template(config, command).replace("{session_id}", session_id)


def default_section(config: dict, goal: str) -> str:
    return goal_config(config, goal).get("default_section", "")


def default_limit(config: dict, goal: str) -> int:
    return int(goal_config(config, goal).get("default_limit", 8))


def default_iterations(config: dict, goal: str) -> int:
    # 0 means keep running batches until the selected audit scope is complete.
    return int(goal_config(config, goal).get("default_iterations", 1))


def default_no_progress_retry_limit(config: dict, goal: str) -> int:
    return int(goal_config(config, goal).get("no_progress_retry_limit", 3))


def default_validation_repair_limit(config: dict, goal: str) -> int:
    return int(goal_config(config, goal).get("validation_repair_limit", 2))


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


class CommandFailure(Exception):
    def __init__(self, command_text: str, returncode: int, output: str) -> None:
        super().__init__(f"command failed with exit code {returncode}: {command_text}")
        self.command_text = command_text
        self.returncode = returncode
        self.output = output


def command_display(command: list[str] | str) -> str:
    return command if isinstance(command, str) else " ".join(command)


def run_capture_tee(command: list[str] | str, *, shell: bool = False) -> str:
    print()
    print("==", command_display(command))
    try:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            shell=shell,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
        )
    except OSError as exc:
        raise CommandFailure(command_display(command), 1, str(exc)) from exc

    assert process.stdout is not None
    captured: list[str] = []
    for line in process.stdout:
        print(line, end="")
        captured.append(line)
    return_code = process.wait()
    output = "".join(captured)
    if return_code != 0:
        raise CommandFailure(command_display(command), return_code, output)
    return output


class LoopLock:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.file = None

    def __enter__(self) -> "LoopLock":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.file = self.path.open("a+", encoding="utf-8")
        try:
            msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as exc:
            self.file.seek(0)
            owner = self.file.read().strip()
            raise SystemExit(
                "Another XLang3 compatibility loop is already running. "
                f"Lock: {self.path}"
                + (f"\nOwner: {owner}" if owner else "")
            ) from exc

        self.file.seek(0)
        self.file.truncate()
        self.file.write(f"pid={os.getpid()}\nstarted={_dt.datetime.now().isoformat(timespec='seconds')}\n")
        self.file.flush()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if not self.file:
            return
        try:
            self.file.seek(0)
            try:
                msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
            except OSError:
                # The loop is already ending; stale lock-file cleanup must not
                # turn a successful batch into a failed run.
                pass
        finally:
            self.file.close()
            self.file = None


def run_with_input(command: str, stdin_text: str) -> None:
    print()
    print("==", command)
    result = subprocess.run(
        command,
        cwd=ROOT,
        shell=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        input=stdin_text,
    )
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def run_with_input_tee(command: str, stdin_text: str) -> str:
    print()
    print("==", command)
    process = subprocess.Popen(
        command,
        cwd=ROOT,
        shell=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
    )
    assert process.stdin is not None
    assert process.stdout is not None
    process.stdin.write(stdin_text)
    process.stdin.close()

    captured: list[str] = []
    for line in process.stdout:
        print(line, end="")
        captured.append(line)
    return_code = process.wait()
    output = "".join(captured)
    if return_code != 0:
        raise SystemExit(return_code)
    return output


def capture(command: list[str]) -> str:
    return subprocess.check_output(
        command,
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        stderr=subprocess.STDOUT,
    )


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def read_compact_lessons(config: dict, goal: str, max_lines: int = 8) -> str:
    path = lessons_path(config, goal)
    if not path.exists():
        return "- No lessons recorded yet."
    lessons: list[str] = []
    current: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("- "):
            if current:
                lessons.append(" ".join(current))
            current = [stripped]
        elif current and stripped and not stripped.startswith("#") and not stripped.startswith("<!--"):
            current.append(stripped)
    if current:
        lessons.append(" ".join(current))
    if not lessons:
        return "- No lessons recorded yet."
    return "\n".join(lessons[-max_lines:])


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


def control_source(
    config: dict,
    goal: str,
    selector: str,
) -> tuple[Path, list[tuple[int, str]], str, bool]:
    selector = resolve_section(config, goal, selector)
    task = task_path(config, goal, selector)
    if task:
        label = task.relative_to(ROOT).as_posix()
        return task, list(enumerate(read_text(task).splitlines(), start=1)), label, True

    path = audit_path(config, goal)
    label = f"{path.relative_to(ROOT).as_posix()}::{selector or 'whole audit'}"
    return path, section_lines(read_text(path).splitlines(), selector), label, False


def unfinished_items(config: dict, goal: str, section: str, limit: int) -> list[tuple[int, str, str]]:
    # Cursor extraction keeps prompts small: only unfinished rows from the
    # selected task file or legacy audit section are passed to Codex.
    _, selected, _, _ = control_source(config, goal, section)
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
    _, selected, _, _ = control_source(config, goal, section)
    checked = partial = missing = 0
    in_fence = False
    for _, line in selected:
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


def task_cursor(source_label: str, items: list[tuple[int, str, str]]) -> str:
    if not items:
        return f"file={source_label}; offset=done; line=done"
    line = items[0][0]
    return f"file={source_label}; offset={line - 1}; line={line}"


def compose_prompt(config: dict, goal: str, section: str, items: list[tuple[int, str, str]]) -> str:
    next_items = "\n".join(
        f"- line {line}: [{mark}] {text}" for line, mark, text in items
    )
    if not next_items:
        next_items = "- No unfinished items found in the selected scope."

    checked, partial, missing = audit_counts(config, goal, section)
    source_path, _, source_label, is_task = control_source(config, goal, section)
    build_command = build_command_text(config)
    fixture_command = fixture_command_text(config)
    section_fixture_command = section_fixture_command_text(config, section) if section else ""
    cursor = task_cursor(source_label, items)
    lessons = read_compact_lessons(config, goal)
    return f"""# XLang3 Python 3.14 Compatibility Batch

This is a compact cursor-extracted context. Use the task file and cursor below
as the control plane. Do not reread the legacy giant audit unless something is
ambiguous.

Goal:
Make XLang3 runtime compatible with Python 3.14 so CPython standard-library
.py files can run naturally on XLang3.

Runtime doctrine:
- XLang3 keeps its own runtime: `Value`, object/refcount model, XlangVM, native
  module/package ABI, VFS, and IR execution are not CPython internals.
- The compatibility target is Python 3.14 syntax and runtime behavior for pure
  Python `.py` files, especially CPython `Lib/*.py` modules.
- For system stdlib work, run the real CPython 3.14 library source first, then
  implement the missing XLang3 runtime primitive or native dependency it
  requires.
- Fix runtime primitives first: object model, call binding, descriptors, import
  system, code/frame/traceback, exceptions, VFS/open/_io, and required native
  dependency modules.
- Do not grow native C++ clones of pure Python stdlib modules as the main
  strategy.
- Native C++ is correct for CPython native/core dependency modules and
  performance-critical product modules.
- If a module is pure Python in CPython, do not add a public C++ module for it.
  Examples: `abc`, `collections`, `queue`, `json`, `pathlib`, `inspect`,
  `argparse`, `typing`, `subprocess`, and `zipfile`.
- Mixed files may keep private/native dependency exports such as `_abc`,
  `_collections`, `_queue`, `_io`, `_weakref`, `_opcode`, `_string`, `_pickle`,
  `_struct`, `nt`/`posix`, `time`, `marshal`, `zlib`, `zipimport`, and `sys`.
- No debugpy-only shortcuts.
- No benchmark-specific code.
- No stubs, placeholder facades, or fake compatibility.
- Any crash, hang, Windows popup, access violation, or negative exit code is a
  runtime regression. Stop feature work, isolate the smallest fixture repro,
  fix the runtime cause, and update lessons before continuing.
- If build or fixtures fail after your batch, the outer loop will feed the
  failure log back to you as a repair prompt in the same Codex session. Repair
  the failed batch before advancing to new audit rows.
- Do not hide regressions with broad locks, sleeps, retries, fake return values,
  or expected-output edits.
- Threading/runtime changes must state and preserve ownership and lock
  boundaries. Lock the minimum region only; do not serialize the whole VM as a
  shortcut.
- Every compatibility change needs fixture coverage under tests/fixtures.
- Before claiming a feature is implemented, map it to a concrete fixture
  assertion. If no assertion exists, add one to the combined section fixture or
  add a focused fixture plus expected output.
- Include a compact feature-to-fixture coverage map in the batch summary.
- Use the fixed local commands below. Do not rediscover cmake, msbuild, ninja,
  python.exe, or xlang3.exe paths unless one of these exact commands fails.
- Use the fixed agent scripts below for deterministic build/test behavior;
  do not generate new build or fixture command lines inside the batch.
- For repository searches on Windows PowerShell, prefer `rg -F` for literal
  text and quote patterns with single quotes. Do not use regex syntax for audit
  checkboxes, backticks, brackets, or C++ punctuation unless regex is required.
- Build only the Release xlang3 target during this loop.
- Update the active task file truthfully.
- Update doc/python314-compat-audit.md only when you intentionally keep the
  legacy audit snapshot aligned with the task.
- Update agent/python314_compat/state.md if the checkpoint changes.
- Update agent/python314_compat/lessons.md if this batch exposes a reusable
  mistake pattern, compatibility trap, or workflow lesson.
- The loop will build, test, commit, and push after your work.

Fixed local scripts:
- Build: {build_command}
- Full fixture validation: {fixture_command}
- Selected section quick run: {section_fixture_command or "not available for whole-audit mode"}

Task source:
{source_path.relative_to(ROOT).as_posix()}

Task mode:
{"task folder" if is_task else "legacy audit fallback"}

Task cursor:
{cursor}

Selected task:
{section or "whole audit"}

Agent goal:
{goal}

Lessons from previous iterations:
{lessons}

Task counts:
- checked: {checked}
- partial: {partial}
- missing: {missing}

Next unfinished task rows from the cursor:
{next_items}

Implement one coherent compatibility batch now. Do not scan unrelated audit
sections; update only the task rows affected by this batch.
"""


def tail_text(text: str, max_chars: int = 16000) -> str:
    if len(text) <= max_chars:
        return text
    return "... output truncated to the final validation context ...\n" + text[-max_chars:]


def compose_repair_prompt(
    config: dict,
    goal: str,
    section: str,
    previous_prompt_path: str,
    failure_log_path: Path,
    failure_output: str,
    repair_attempt: int,
    repair_limit: int,
) -> str:
    build_command = build_command_text(config)
    fixture_command = fixture_command_text(config)
    section_fixture_command = section_fixture_command_text(config, section) if section else ""
    lessons = read_compact_lessons(config, goal)
    paths = changed_paths()
    changed = "\n".join(f"- {path}" for path in paths) or "- No stageable files detected."

    return f"""# XLang3 Python 3.14 Compatibility Repair Batch

The previous compatibility iteration changed code, but validation failed. Do
not advance to new audit items yet. Repair this batch first, then run the fixed
local validation scripts.

Repair attempt:
{repair_attempt}/{repair_limit}

Goal:
Make the current in-progress batch build and pass fixtures without hiding or
weakening the compatibility checks.

Rules:
- Treat the validation failure as the primary input.
- Fix the runtime/test cause, not the symptom.
- Do not fake return values, add benchmark-specific paths, silence crashes, or
  edit expected output unless Python 3.14 proves the expected output is wrong.
- Add or adjust fixture coverage only when it proves the repaired behavior.
- Keep work scoped to the current failed batch and its direct regression.
- Use deterministic scripts; do not rediscover local tool paths.
- Update lessons if this failure reveals a reusable loop/runtime mistake.
- Commit is handled by the outer loop only after validation passes.

Fixed local scripts:
- Build: {build_command}
- Full fixture validation: {fixture_command}
- Selected section quick run: {section_fixture_command or "not available for whole-audit mode"}

Selected audit section:
{section or "whole audit"}

Previous prompt:
{previous_prompt_path or "not recorded"}

Failure log:
{failure_log_path}

Stageable files currently in the failed batch:
{changed}

Lessons from previous iterations:
{lessons}

Validation failure output:
```text
{tail_text(failure_output)}
```

Repair the failing batch now. After your changes, run the most focused fixture
that reproduces the failure, then the fixed full validation command if feasible.
"""


def write_prompt(config: dict, goal: str, prompt: str, iteration: int, label: str = "iteration") -> Path:
    output_dir = runs_dir(config, goal)
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    safe_label = re.sub(r"[^A-Za-z0-9_-]+", "-", label).strip("-") or "iteration"
    path = output_dir / f"{stamp}-{safe_label}-{iteration:02d}.prompt.md"
    path.write_text(prompt, encoding="utf-8")
    return path


def write_failure_log(config: dict, goal: str, iteration: int, repair_attempt: int, output: str) -> Path:
    output_dir = runs_dir(config, goal)
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = output_dir / f"{stamp}-iteration-{iteration:02d}-repair-{repair_attempt:02d}.failure.log"
    path.write_text(output, encoding="utf-8")
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
    source_path, _, source_label, is_task = control_source(config, goal, section)

    print(f"Repo: {ROOT}")
    print(f"Goal: {goal}")
    print(f"Task: {section or 'whole audit'}")
    print(f"Source: {source_path.relative_to(ROOT).as_posix()}")
    print(f"Mode: {'task folder' if is_task else 'legacy audit fallback'}")
    print(f"Cursor: {task_cursor(source_label, items)}")
    print(f"Task counts: checked={checked}, partial={partial}, missing={missing}")

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

    print("Next unfinished task rows:")
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


def request_stop(config: dict, goal: str) -> None:
    path = stop_request_path(config, goal)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "Stop after the current XLang3 compatibility loop iteration completes.\n",
        encoding="utf-8",
    )
    print(f"Stop requested. The loop will exit after the current iteration: {path}")


def consume_stop_request(config: dict, goal: str) -> bool:
    path = stop_request_path(config, goal)
    if not path.exists():
        return False
    try:
        path.unlink()
    except OSError:
        pass
    print("Stop request acknowledged; exiting after this completed iteration.")
    return True


def read_saved_session_id(config: dict, goal: str) -> str:
    path = session_id_path(config, goal)
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8").strip()


def write_saved_session_id(config: dict, goal: str, output: str) -> None:
    matches = SESSION_ID_RE.findall(output)
    if not matches:
        return
    path = session_id_path(config, goal)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(matches[-1] + "\n", encoding="utf-8")
    print(f"Saved Codex session id: {matches[-1]}")


def invoke_codex(config: dict, goal: str, command_template: str, prompt_path: Path, prompt: str) -> None:
    session_id = read_saved_session_id(config, goal)
    resume_template = default_codex_resume_command(config)
    if session_id and resume_template:
        command_template = expand_session_command_template(config, resume_template, session_id)
        print(f"Resuming Codex session: {session_id}")

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
        output = run_with_input_tee(command, prompt)
        write_saved_session_id(config, goal, output)
        print("Codex backend finished.")
        return

    print()
    print("== Codex backend")
    print(f"Prompt file: {prompt_path}")
    output = run_with_input_tee(command, prompt)
    write_saved_session_id(config, goal, output)
    print("Codex backend finished.")


def resolve_cmake(explicit: str, config: dict) -> str:
    if explicit:
        return explicit
    configured = default_cmake(config)
    if configured:
        return configured

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
    captured: list[str] = []
    config = load_config()
    fixture_timeout = str(int(config.get("repo", {}).get("fixture_case_timeout", 60)))

    def checked_run(command: list[str]) -> None:
        try:
            captured.append(run_capture_tee(command))
        except CommandFailure as exc:
            output = "\n".join(captured)
            if exc.output:
                output += ("\n" if output else "") + exc.output
            raise CommandFailure(exc.command_text, exc.returncode, output) from exc

    if not skip_build:
        print()
        print("Status: building Release xlang3")
        checked_run([python_exe, "agent\\scripts\\build_release.py"])
    if not skip_tests:
        print()
        print("Status: running compatibility fixtures")
        checked_run([
            python_exe,
            "agent\\scripts\\run_fixtures.py",
            "--xlang3",
            xlang3,
            "--case-timeout",
            fixture_timeout,
        ])
        print()
        print("Status: checking patch whitespace")
        checked_run(["git", "diff", "--check"])


def should_resume_as_codex_done(phase: str, saved_state: dict, stageable_changes: list[str]) -> bool:
    if phase not in {"prompt_written", "codex_running", "repair_prompt_written", "repair_codex_running"}:
        return False
    baseline = sorted(saved_state.get("baseline_stageable_paths", []))
    current = sorted(stageable_changes)
    return current != baseline


def stageable(path: str) -> bool:
    normalized = path.replace("\\", "/")
    blocked_prefixes = (
        ".tmp_",
        "agent/python314_compat/.agent_runs/",
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
    output = subprocess.check_output(
        ["git", "status", "--porcelain"],
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
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
        print("No stageable compatibility changes found. Scratch/temp files are ignored.")
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
    configure_no_popup_error_mode()

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
    parser.add_argument("--request-stop", action="store_true", help="Ask a running continuous loop to stop after its current iteration.")
    args = parser.parse_args()

    os.chdir(ROOT)
    config = load_config()
    goal = args.goal or config.get("default_goal", "")
    configured_section = args.section if args.section is not None else default_section(config, goal)
    section = resolve_section(config, goal, configured_section)
    limit = args.limit if args.limit > 0 else default_limit(config, goal)
    commit_message = args.commit_message or default_commit_message(config, goal)
    iterations = args.iterations if args.iterations >= 0 else default_iterations(config, goal)
    continuous = iterations == 0
    no_progress_retry_limit = default_no_progress_retry_limit(config, goal)
    validation_repair_limit = default_validation_repair_limit(config, goal)
    no_progress_count = 0
    xlang3 = args.xlang3 or default_xlang3(config)
    python_exe = default_python(config)
    codex_command = validate_codex_command(config, args.codex_command or default_codex_command(config))
    cmake = resolve_cmake(args.cmake, config) if not args.dry_run and not args.skip_build else ""

    if args.reset_loop_state:
        clear_loop_state(config, goal)
        print(f"Cleared loop state for goal: {goal}")

    if args.request_stop:
        request_stop(config, goal)
        return 0

    if args.status:
        print_loop_status(config, goal, section, limit)
        return 0

    lock = LoopLock(loop_lock_path(config, goal))
    lock.__enter__()

    try:
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
            print(f"Task: {active_section or 'whole audit'}")
            if resume_phase:
                print(f"Resume phase: {resume_phase}")
                print(f"Stageable changed files: {len(stageable_changes)}")
            print("=" * 72)

            if should_resume_as_codex_done(resume_phase, saved_state, stageable_changes):
                print("Saved phase was interrupted, but stageable changes exist; resuming at validation.")
                resume_phase = "codex_done"
            elif resume_phase == "validation_failed":
                print("Saved validation failure exists; retrying validation/repair for the failed batch.")
                resume_phase = "codex_done"
            elif resume_phase in {"prompt_written", "codex_running", "repair_prompt_written", "repair_codex_running"}:
                prompt_path = Path(saved_state.get("prompt_path", ""))
                if not prompt_path.exists():
                    print("Saved prompt is missing; starting a fresh iteration.")
                    clear_loop_state(config, goal)
                    saved_state = {}
                    resume_phase = ""
                else:
                    prompt = prompt_path.read_text(encoding="utf-8")
                    print("Resuming saved Codex prompt.")
                    running_state = dict(saved_state)
                    running_state.update({
                        "active": True,
                        "phase": "repair_codex_running" if resume_phase.startswith("repair_") else "codex_running",
                        "goal": goal,
                        "section": active_section,
                        "prompt_path": str(prompt_path),
                        "iteration": iteration,
                        "baseline_stageable_paths": saved_state.get("baseline_stageable_paths", []),
                    })
                    write_loop_state(config, goal, running_state)
                    try:
                        invoke_codex(config, goal, codex_command, prompt_path, prompt)
                    except SystemExit:
                        retry_state = dict(saved_state)
                        retry_state.update({
                            "active": True,
                            "phase": "repair_prompt_written" if resume_phase.startswith("repair_") else "prompt_written",
                            "goal": goal,
                            "section": active_section,
                            "prompt_path": str(prompt_path),
                            "iteration": iteration,
                            "baseline_stageable_paths": saved_state.get("baseline_stageable_paths", []),
                        })
                        write_loop_state(config, goal, retry_state)
                        raise
                    done_state = dict(saved_state)
                    done_state.update({
                        "active": True,
                        "phase": "codex_done",
                        "goal": goal,
                        "section": active_section,
                        "prompt_path": str(prompt_path),
                        "iteration": iteration,
                        "baseline_stageable_paths": saved_state.get("baseline_stageable_paths", []),
                    })
                    write_loop_state(config, goal, done_state)
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
                print(f"Task counts: checked={checked}, partial={partial}, missing={missing}")
                if not items:
                    print("No unfinished rows remain in this task scope.")
                    clear_loop_state(config, goal)
                    break
                _, _, source_label, _ = control_source(config, goal, active_section)
                print(f"Task cursor: {task_cursor(source_label, items)}")
                print("Next unfinished task rows:")
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
                    invoke_codex(config, goal, codex_command, prompt_path, prompt)
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
                repair_attempt = int(saved_state.get("repair_attempt", 0)) if saved_state else 0
                while True:
                    try:
                        validate(cmake, xlang3, python_exe, args.skip_build, args.skip_tests)
                        break
                    except CommandFailure as exc:
                        if args.dry_run:
                            raise SystemExit(exc.returncode) from exc
                        if repair_attempt >= validation_repair_limit:
                            failure_log = write_failure_log(
                                config,
                                goal,
                                iteration,
                                repair_attempt,
                                exc.output,
                            )
                            write_loop_state(config, goal, {
                                "active": True,
                                "phase": "validation_failed",
                                "goal": goal,
                                "section": active_section,
                                "prompt_path": current_prompt_path,
                                "iteration": iteration,
                                "repair_attempt": repair_attempt,
                                "failure_log_path": str(failure_log),
                                "baseline_stageable_paths": current_baseline_paths,
                            })
                            print()
                            print(
                                "Validation still fails after "
                                f"{validation_repair_limit} repair attempts."
                            )
                            print(f"Failure log: {failure_log}")
                            raise SystemExit(exc.returncode) from exc

                        repair_attempt += 1
                        failure_log = write_failure_log(
                            config,
                            goal,
                            iteration,
                            repair_attempt,
                            exc.output,
                        )
                        print()
                        print(
                            "Validation failed; feeding the failure back into "
                            f"Codex repair attempt {repair_attempt}/{validation_repair_limit}."
                        )
                        print(f"Failure log: {failure_log}")
                        repair_prompt = compose_repair_prompt(
                            config,
                            goal,
                            active_section,
                            current_prompt_path,
                            failure_log,
                            exc.output,
                            repair_attempt,
                            validation_repair_limit,
                        )
                        repair_prompt_path = write_prompt(
                            config,
                            goal,
                            repair_prompt,
                            iteration,
                            label=f"repair-{repair_attempt:02d}",
                        )
                        repair_state = {
                            "active": True,
                            "phase": "repair_prompt_written",
                            "goal": goal,
                            "section": active_section,
                            "prompt_path": str(repair_prompt_path),
                            "previous_prompt_path": current_prompt_path,
                            "iteration": iteration,
                            "repair_attempt": repair_attempt,
                            "failure_log_path": str(failure_log),
                            "baseline_stageable_paths": current_baseline_paths,
                        }
                        write_loop_state(config, goal, repair_state)

                        write_loop_state(config, goal, {
                            **repair_state,
                            "phase": "repair_codex_running",
                        })
                        try:
                            invoke_codex(config, goal, codex_command, repair_prompt_path, repair_prompt)
                        except SystemExit:
                            write_loop_state(config, goal, repair_state)
                            raise

                        write_loop_state(config, goal, {
                            **repair_state,
                            "phase": "codex_done",
                        })
                        saved_state = read_loop_state(config, goal)
                        current_prompt_path = str(repair_prompt_path)
                write_loop_state(config, goal, {
                    "active": True,
                    "phase": "validated",
                    "goal": goal,
                    "section": active_section,
                    "prompt_path": current_prompt_path,
                    "iteration": iteration,
                    "repair_attempt": repair_attempt,
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
                if consume_stop_request(config, goal):
                    break
                if continuous and not committed:
                    no_progress_count += 1
                    if no_progress_count >= no_progress_retry_limit:
                        print(
                            "Stopping continuous loop after "
                            f"{no_progress_count} consecutive no-progress batches."
                        )
                        break
                    print(
                        "No stageable changes in this batch; continuing because "
                        f"continuous mode allows {no_progress_retry_limit} retries."
                    )
                else:
                    no_progress_count = 0

                iteration += 1
    finally:
        lock.__exit__(None, None, None)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
