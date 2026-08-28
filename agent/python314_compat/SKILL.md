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
- `agent/python314_compat/lessons.md`
- `doc/python314-compat-audit.md`

Work rules:

- Treat Python 3.14 runtime compatibility as the product goal.
- Do not make debugpy-only, benchmark-only, or module-name-specific shortcuts.
- Prefer fixing runtime primitives so CPython standard-library `.py` files can
  run naturally.
- Use native C++ only for runtime primitives, CPython native dependency modules,
  and performance-critical product modules.
- Add fixture coverage under `tests/fixtures`.
- For every new or changed compatibility feature, first check whether an
  existing fixture assertion covers it. If coverage is missing, add assertions
  to the existing combined section fixture or add a focused fixture plus its
  expected output.
- In the final batch summary, include a compact coverage map naming each
  feature and the fixture file/assertion that proves it.
- Use deterministic agent scripts for local checks; do not invent build/test
  command lines during each batch.
- Treat any crash, hang, Windows popup, access violation, or negative exit code
  as a runtime regression. Stop feature work, isolate the smallest repro under
  `tests/fixtures`, fix the runtime cause, and add/update the lesson before
  allowing the loop to continue.
- Do not add broad locks, sleeps, retries, or expected-output changes to hide a
  failing fixture. A stabilization change is valid only when it fixes the
  runtime invariant and still passes the deterministic fixture gate.
- On Windows PowerShell, use `rg -F` for literal searches, especially for audit
  checkboxes, brackets, backticks, quotes, C++ punctuation, and Python syntax.
  Use regex mode only when the pattern is intentionally a regex.
- When a fixture mismatch appears, decide whether it is a runtime bug or an
  obsolete golden file by comparing against Python 3.14 behavior or the changed
  runtime contract. Do not update expected output just to make tests pass.
- Update `doc/python314-compat-audit.md` only for behavior that is really
  implemented and tested.
- Update `agent/python314_compat/lessons.md` when the batch discovers a
  reusable mistake pattern or workflow lesson.
- Build Release with `agent/scripts/build_release.py` and run
  `agent/scripts/run_fixtures.py` before committing.
