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
# Rules

- The final goal is Python 3.14 runtime compatibility, not debugpy-only support.
- Prefer runtime compatibility over native reimplementation of pure Python
  standard-library modules.
- Native modules are appropriate for CPython native/core dependency layers such
  as `_io`, `_thread`, `_weakref`, `_abc`, `_collections`, `_struct`, `_pickle`,
  `zlib`, `_socket`, and product-specific accelerated modules.
- Do not mark an audit item `[x]` unless it has real fixture coverage.
- For every new or changed compatibility feature, identify the fixture
  assertion that proves it. If no assertion exists, add one to the relevant
  combined section fixture or add a focused fixture with expected output.
- Batch summaries must include a compact feature-to-fixture coverage map.
- Do not hide gaps behind stubs, placeholders, importable empty facades, or
  return-`None` behavior.
- Do not add benchmark-specific or module-specific cheats.
- Do not change fixtures blindly. For each output mismatch, confirm whether the
  runtime behavior is Python-compatible or the runtime needs fixing.
- Use `agent/scripts/build_release.py` and `agent/scripts/run_fixtures.py` for
  repeatable local validation instead of constructing ad hoc commands.
- Use `agent/scripts/run_section_fixture.py` for section fixture checks.
- On Windows PowerShell, prefer `rg -F` for literal searches. Audit rows,
  checkboxes, backticks, brackets, quotes, and C++/Python punctuation are
  literals unless the task explicitly needs regex matching.
- Add or update fixtures for every compatibility change.
- Use `tests/fixtures` as the canonical fixture location.
- Build and run tests before commit.
- Commit only after validation passes.
