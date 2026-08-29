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
# Python 3.14 Compatibility Tasks

This folder is the compact control plane for the compatibility loop. Keep task
rows brief so each loop prompt stays small.

Use this status strictly:

- `[x]`: implemented, fixture-covered, and no known gap in the scoped row.
- `[~]`: usable foundation exists, but `Remaining` still has known work.
- `[ ]`: not implemented or not audited.

Task files:

- `syntax.md`
- `runtime_core.md`
- `builtin_types.md`
- `builtin_functions.md`
- `native_dependencies.md`
- `native_sys_time_audit.md`
- `standard_modules.md`
- `stdlib_shim_cleanup.md`
- `filesystem_io.md`
- `async_threads.md`
- `debugger.md`
- `deferred_exact_cpython.md`
