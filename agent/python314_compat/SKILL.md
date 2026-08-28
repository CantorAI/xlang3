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
# XLang3 Python 3.14 Compatibility

Use this goal package when working on XLang3 Python 3.14 compatibility.

Read these files before choosing a batch:

- `agent/python314_compat/goal.md`
- `agent/python314_compat/rules.md`
- `agent/python314_compat/state.md`
- `agent/python314_compat/queue.md`
- `doc/python314-compat-audit.md`

Work rules:

- Treat Python 3.14 runtime compatibility as the product goal.
- Do not make debugpy-only, benchmark-only, or module-name-specific shortcuts.
- Prefer fixing runtime primitives so CPython standard-library `.py` files can
  run naturally.
- Use native C++ only for runtime primitives, CPython native dependency modules,
  and performance-critical product modules.
- Add fixture coverage under `tests/fixtures`.
- Use deterministic agent scripts for local checks; do not invent build/test
  command lines during each batch.
- On Windows PowerShell, use `rg -F` for literal searches, especially for audit
  checkboxes, brackets, backticks, quotes, C++ punctuation, and Python syntax.
  Use regex mode only when the pattern is intentionally a regex.
- When a fixture mismatch appears, decide whether it is a runtime bug or an
  obsolete golden file by comparing against Python 3.14 behavior or the changed
  runtime contract. Do not update expected output just to make tests pass.
- Update `doc/python314-compat-audit.md` only for behavior that is really
  implemented and tested.
- Build Release with `agent/scripts/build_release.py` and run
  `agent/scripts/run_fixtures.py` before committing.
