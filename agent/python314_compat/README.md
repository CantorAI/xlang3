<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# Python 3.14 Compatibility Goal

This folder is the goal package for the XLang3 Python 3.14 compatibility work.
It stores the goal, rules, current state, and queue used by the Codex-driven
development loop.

The shared runner scripts live in:

```text
agent/scripts/
```

Those scripts are intentionally XLang3-aware. They read `agent/config.toml`,
which records this repository layout, the Visual Studio CMake build, fixture
locations, this goal folder, the task files, and the Codex prompt contract.

The compact active task plan lives in:

```text
agent/python314_compat/tasks/
```

Each file in that folder is a small task list. `queue.md` stores task IDs such
as `system_stdlib`, not file paths; the loop maps IDs through `tasks_dir` from
`agent/config.toml`. The loop can run one named task, for example
`--section system_stdlib`, or `--section auto` to pick the first unfinished task
from `queue.md`. Keep these files compact; they are the normal Codex prompt
input.

Loop prompts identify work with a compact cursor:

```text
file=agent/python314_compat/tasks/system_stdlib.md; offset=21; line=22
```

`offset` is the zero-based row offset in the task markdown file. `line` is the
one-based editor line for human review.

The old product audit remains as a legacy snapshot/reference in:

```text
doc/python314-compat-audit.md
```

The compatibility fixtures remain in:

```text
tests/fixtures/core/
tests/fixtures/compat_sections/
tests/fixtures/expected/
```

The agent loop reads the selected task file, finds unfinished items, builds
XLang3 with the Visual Studio CMake toolchain, runs the fixture suite, and
commits only after the tree passes validation.

Typical use:

```text
powershell -ExecutionPolicy Bypass -File agent\run_python314_compat.ps1
C:\Python\Python314\python.exe agent\scripts\codex_loop.py --dry-run
```

To let the loop ask a Codex backend to do one batch, pass the backend command.
The loop sends Codex a compact extracted prompt, not the full audit/control
markdown every time. The command can use `{prompt_file}` or `{prompt}`. If
neither placeholder is present, the prompt file path is appended.

```text
C:\Python\Python314\python.exe agent\scripts\codex_loop.py ^
  --codex-command "codex exec --dangerously-bypass-approvals-and-sandbox {prompt_file}" ^
  --commit-message "Close Python 3.14 runtime compatibility gaps"
```

After manual Codex work, the lower-level fixture runner can still be used
directly:

```text
C:\Python\Python314\python.exe agent\scripts\run_fixtures.py --xlang3 build\Release\xlang3.exe
```

The script is intentionally conservative: it does not edit code by itself, and
it does not stage root-level scratch files or build output.

Lessons learned are accumulated in:

```text
agent/python314_compat/lessons.md
```

Each batch should add a short lesson when it discovers a recurring mistake,
compatibility trap, or workflow rule that should guide later iterations.

Resume behavior:

```text
agent/python314_compat/.agent_runs/loop_state.json
```

The loop records its current phase there. If the process is stopped while Codex
is running, the next run checks for stageable source changes. With changes, it
continues to validation; without changes, it reruns the saved prompt. If it is
stopped after validation, the next run resumes at commit.

Use `--status` to inspect the saved phase and next task rows. Use
`--reset-loop-state` only when intentionally discarding the saved batch.
