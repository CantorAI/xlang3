# Builtin Function Tasks

- [x] common builtin functions
  Coverage: `tests/fixtures/compat_sections/builtins.py`, `tests/fixtures/core/builtin_function_batch.py`
  Remaining: none for the current common surface.

- [x] open
  Coverage: `tests/fixtures/core/file_io_compat.py`,
  `tests/fixtures/core/filesystem_io_edges.py`,
  `tests/fixtures/compat_sections/builtins.py`
  Coverage update: the completed filesystem/IO work covers validated buffering,
  newline translation, descriptor-backed files and `closefd=False`, custom
  opener callbacks, and exact path error classes.
  Remaining: none for the supported `open()` surface.

- [x] globals and locals mapping identity
  Coverage: `tests/fixtures/compat_sections/builtins.py`,
  `tests/fixtures/core/dynamic_execution_builtins.py`,
  `tests/fixtures/core/globals_locals_identity.py`,
  `tests/cpp/module_slot_tests.cpp`
  Coverage update: module namespaces now expose one stable exact `dict` shared
  by `globals()`, module-level `locals()`/`vars()`, `module.__dict__`,
  `vars(module)`, and `frame.f_globals`. Writes and deletions through the dict
  stay synchronized with compiled module slots, including non-string keys.
  Optimized function `locals()` and zero-argument `vars()` return fresh dict
  snapshots whose mutations do not write back, matching Python 3.14.
  Remaining: none for the builtin mapping identity surface.

