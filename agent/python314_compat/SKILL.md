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
- Update `doc/python314-compat-audit.md` only for behavior that is really
  implemented and tested.
- Build Release with Visual Studio CMake and run `agent/scripts/run_fixtures.py`
  before committing.
