# Native Sys/Time Audit

Purpose: keep `sys` and `time` honest. These modules are allowed to be native,
but they must expose real interpreter or OS behavior, not pure-stdlib facades.

- [x] `time` remains a native module
  Coverage: release build; exported functions are OS/C-runtime clock, sleep,
  calendar conversion, formatting/parsing, timezone, and `struct_time` behavior.
  Remaining: exact locale/timezone/DST edge cases tracked in standard modules.

- [x] `sys` remains a native module
  Coverage: release build; exported state includes module registry, path,
  argv/orig_argv, stdio, exception state, frame hooks, trace/profile hooks,
  runtime metadata, and interpreter cache/debug hooks.
  Remaining: none in this compact audit; broader `sys` parity remains tracked
  in `standard_modules.md` and `debugger.md`.

- [x] `sys.is_remote_debug_enabled`
  Coverage: the native query reads `Runtime::debug_enabled()`; ordinary
  execution reports disabled, while `DebugSession` enables the same runtime
  state. `tests/fixtures/core/native_sys_time_audit.py` covers the ordinary
  execution state.
  Remaining: none for the native runtime-state query.

- [x] `sys._is_gil_enabled`
  Coverage: the native query derives its answer from
  `XLANG3_VM_GLOBAL_LOCK`, and `_sysconfig` derives `Py_GIL_DISABLED` from the
  same build setting. The focused fixture verifies their agreement; CPython's
  `test.test_sys.SysModuleTest.test_is_gil_enabled` passes.
  Remaining: none for reporting the configured VM locking mode.

- [x] `sys.__interactivehook__` and `sys._baserepl`
  Coverage: the native startup fallbacks are callable and inactive in
  noninteractive execution. Importing CPython's source-backed `Lib/site.py`
  replaces `sys.__interactivehook__` with the real Python
  `site.register_readline`; the XLang3 CLI continues to own interactive input.
  The focused fixture verifies the source-backed `site` replacement.
  Remaining: none for the startup-hook audit.

- [x] `sys.activate_stack_trampoline` family
  Coverage: unavailable state, activation failure, deactivation, and invalid
  backend behavior match the Windows CPython 3.14 build: activation raises
  `ValueError: perf trampoline not available`, including for unsupported
  backend names. The focused fixture covers the family.
  Remaining: none while XLang3 has no stack-trampoline backend.

- [x] `sys._jit`
  Coverage: the CPython 3.14-shaped module truthfully reports unavailable,
  disabled, and inactive because XLang3 has no JIT. CPython's availability and
  enabled tests pass, and the focused fixture covers all three queries.
  Remaining: none while XLang3 has no JIT.

- [x] `sys.monitoring`
  Coverage: monitoring state is connected to real VM dispatch for Python
  start/resume/return/yield/throw, source lines, instructions, jumps/branches,
  calls/C returns/C raises, and exception paths. C-event callbacks use the
  CPython four-argument shape and `MISSING`; branch aliases and tool cleanup
  match CPython behavior. `MonitoringBasicTest` passes all 3 tests, and the
  focused fixture plus `standard_modules.py` cover live dispatch.
  Remaining: none for proving real VM dispatch in this audit. The complete PEP
  669 event/count matrix remains tracked in `debugger.md`.
