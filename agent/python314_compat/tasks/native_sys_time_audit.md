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
  Remaining: audit individual hard-coded compatibility answers below.

- [~] `sys.is_remote_debug_enabled`
  Coverage: native export exists.
  Remaining: currently returns a constant enabled answer; replace with real
  runtime debugger/config state before marking complete.

- [~] `sys._is_gil_enabled`
  Coverage: native export exists.
  Remaining: must report actual XLang3 threading/runtime mode. Do not hard-code
  CPython's GIL answer, because XLang3's design target is no global GIL.

- [~] `sys.__interactivehook__` and `sys._baserepl`
  Coverage: callable metadata exists.
  Remaining: verify whether these are needed by CPython Lib startup. No-op
  hooks are acceptable only if CPython also treats absence/inactivity as valid.

- [~] `sys.activate_stack_trampoline` family
  Coverage: diagnostics and unavailable state exist.
  Remaining: confirm CPython-compatible unavailable behavior; otherwise remove
  or replace with real runtime profiler/trampoline state.

- [~] `sys._jit`
  Coverage: CPython 3.14-shaped module exists and currently reports unavailable.
  Remaining: keep unavailable state truthful until XLang3 JIT exists; never use
  it to fake performance capability.

- [~] `sys.monitoring`
  Coverage: PEP 669-shaped native state exists for debug/profile integration.
  Remaining: continue toward real VM event dispatch; avoid satisfying debugpy
  with metadata-only behavior.
