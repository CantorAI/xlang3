# Filesystem And IO Tasks

- [~] VFS abstraction
  Coverage: `tests/fixtures/core/vfs_file_io.py`
  Remaining: unify host filesystem, embedded filesystem, and import file reads through one VFS contract.

- [~] file object and open
  Coverage: `tests/fixtures/core/file_context_open.py`, `tests/fixtures/core/file_io_compat.py`
  Remaining: exact buffering, newline translation, fd-backed files, opener callbacks, and platform error classes.

- [~] os fd API
  Coverage: `tests/fixtures/compat_sections/system_stdlib.py` covers CPython
  `Lib/os.py` delegation for `open`, `close`, `read`, `write`, `lseek`,
  `fstat`, `dup`, `dup2`, `pipe`, `isatty`, `get_inheritable`, and
  `set_inheritable`.
  Remaining: exact Windows flag/error mapping and deeper fd edge cases.
