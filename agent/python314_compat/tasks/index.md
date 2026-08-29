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
- `system_stdlib.md`
- `stdlib_shim_cleanup.md`
- `filesystem_io.md`
- `async_threads.md`
- `debugger.md`
- `deferred_exact_cpython.md`
