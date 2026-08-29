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
