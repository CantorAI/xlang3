# Python 3.14 Compatibility Goal

This folder is the compact control plane for the XLang3 Python 3.14
compatibility loop.

Important files:

- `goal.md`: product goal.
- `rules.md`: loop rules.
- `queue.md`: task order, using task IDs only.
- `tasks/`: compact task files.
- `context/module_policy.md`: native-vs-Python stdlib boundary.
- `lessons.md`: reusable mistakes and fixes learned by the loop.

The legacy full audit remains only as reference:

```text
doc/python314-compat-audit.md
```

Default run:

```text
powershell -ExecutionPolicy Bypass -File agent\run_python314_compat.ps1
```

Status:

```text
C:\Python\Python314\python.exe agent\scripts\codex_loop.py --status
```

Queue behavior:

```text
queue.md task ID -> tasks_dir/<task-id>.md
```

A no-argument run picks the first unfinished task from `queue.md`. The loop
extracts only the active unfinished rows and passes a cursor like:

```text
file=agent/python314_compat/tasks/system_stdlib.md; offset=7; line=8
```

Validation is deterministic:

```text
C:\Python\Python314\python.exe agent\scripts\build_release.py
C:\Python\Python314\python.exe agent\scripts\run_fixtures.py --xlang3 build\Release\xlang3.exe
```

The loop records resumable state in:

```text
agent/python314_compat/.agent_runs/loop_state.json
```
