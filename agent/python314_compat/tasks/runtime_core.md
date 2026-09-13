# Runtime Core Tasks

- [x] core value and object model
  Coverage: `tests/fixtures/compat_sections/core_value_and_object_model.py`
  Remaining: none in the current scoped audit.

- [x] functions and calls
  Coverage: `tests/fixtures/compat_sections/functions_and_calls.py`
  Remaining: none in the current scoped audit.

- [x] exceptions
  Coverage: `tests/fixtures/compat_sections/exceptions.py`
  Remaining: none in the current scoped audit.

- [x] import runtime internals
  Coverage: `tests/fixtures/compat_sections/imports_and_modules.py`,
  `tests/fixtures/core/imp_stat_modules.py`, and
  `tests/cpp/runtime_value_tests.cpp`
  Completed: `.pyc` cache handling is intentionally source-authoritative; XLang3 may
  emit cache artifacts for compatibility but does not need to reuse them for execution.
  The import lock is recursive, owner-aware, blocks competing threads, and rejects
  unowned release. XLang3 uses native bootstrap protocol modules and intentionally has
  no CPython frozen-bytecode table; pure Python library modules remain Python source.
  Remaining: none in the current scoped audit.

- [x] frame, code, and traceback internals
  Coverage: `tests/fixtures/core/debug_frame_metadata.py`,
  `tests/fixtures/compat_sections/exceptions.py`, and
  `tests/fixtures/compat_sections/standard_modules.py`
  Completed: Python 3.14 frame/code inspection attributes, persistent `f_trace*`
  metadata, `code.replace()` name metadata, line/branch inspection protocols, and
  read-only, type-checked, cycle-safe traceback links required by stdlib and debuggers.
  Remaining: none in the current scoped audit.

