# Builtin Function Tasks

- [x] common builtin functions
  Coverage: `tests/fixtures/compat_sections/builtins.py`, `tests/fixtures/core/builtin_function_batch.py`
  Remaining: none for the current common surface.

- [~] open
  Coverage: `tests/fixtures/core/file_io_compat.py`, `tests/fixtures/compat_sections/builtins.py`
  Remaining: exact buffering, newline, opener, fd, and error-class semantics.

- [~] globals and locals mapping identity
  Coverage: `tests/fixtures/compat_sections/builtins.py`
  Remaining: exact live `dict` identity/type semantics for module and frame mappings.

