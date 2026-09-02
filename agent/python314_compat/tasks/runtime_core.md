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

- [~] import runtime internals
  Coverage: `tests/fixtures/compat_sections/imports_and_modules.py`
  Remaining: `.pyc` cache execution, exact import-lock behavior, and frozen bytecode table parity.

- [~] frame, code, and traceback internals
  Coverage: `tests/fixtures/core/debug_frame_metadata.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: full CPython frame/code object edge behavior required by pure Python stdlib and debuggers.

