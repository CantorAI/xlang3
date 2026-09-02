# XLang3 Python 3.14 Compatibility

Use this goal package for XLang3 Python 3.14 compatibility work.

Read before choosing a batch:

- `agent/python314_compat/system_prompt.md`
- `agent/python314_compat/goal.md`
- `agent/python314_compat/rules.md`
- `agent/python314_compat/context/module_policy.md`
- `agent/python314_compat/queue.md`
- `agent/python314_compat/lessons.md`
- `agent/python314_compat/tasks/index.md`
- the selected task file under `agent/python314_compat/tasks/`

Use `doc/python314-compat-audit.md` only when a compact task item is ambiguous.

Do not implement pure Python CPython standard-library modules as public C++
facades. Make the XLang3 runtime capable of running the real CPython 3.14
`Lib/*.py` source, and add fixture coverage for every compatibility claim.
