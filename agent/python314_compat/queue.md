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

This queue lists compact task IDs. The loop maps each ID through
`tasks_dir` in `agent/config.toml`; queue rows must not include `tasks/` or
`.md`. The legacy audit in `doc/python314-compat-audit.md` is reference
material, not the normal loop input.

P0 runtime compatibility:

- `system_stdlib`
- `native_sys_time_audit`
- `standard_modules`
- `native_dependencies`
- `filesystem_io`
- `runtime_core`

P1 native dependency modules:

- `builtin_functions`
- `builtin_types`
- `async_threads`

P2 CPython standard-library probes:

- `debugger`
- `deferred_exact_cpython`
