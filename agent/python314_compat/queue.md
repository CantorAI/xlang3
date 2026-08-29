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
# Queue

This queue is implemented as compact task files under
`agent/python314_compat/tasks/`. The legacy audit in
`doc/python314-compat-audit.md` is reference material, not the normal loop input.

P0 runtime compatibility:

- `tasks/system_stdlib.md`
- `tasks/native_sys_time_audit.md`
- `tasks/standard_modules.md`
- `tasks/native_dependencies.md`
- `tasks/filesystem_io.md`
- `tasks/runtime_core.md`

P1 native dependency modules:

- `tasks/builtin_functions.md`
- `tasks/builtin_types.md`
- `tasks/async_threads.md`

P2 CPython standard-library probes:

- `tasks/debugger.md`
- `tasks/deferred_exact_cpython.md`
