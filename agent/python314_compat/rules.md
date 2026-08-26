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
- Do not hide gaps behind stubs, placeholders, importable empty facades, or
  return-`None` behavior.
- Do not add benchmark-specific or module-specific cheats.
- Add or update fixtures for every compatibility change.
- Use `tests/fixtures` as the canonical fixture location.
- Build and run tests before commit.
- Commit only after validation passes.
