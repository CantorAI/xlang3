# Filesystem And IO Tasks

- [~] VFS abstraction
  Coverage: `tests/fixtures/core/vfs_file_io.py`
  Remaining: unify host filesystem, embedded filesystem, and import file reads through one VFS contract.

- [~] file object and open
  Coverage: `tests/fixtures/core/file_context_open.py`, `tests/fixtures/core/file_io_compat.py`
  Remaining: exact buffering, newline translation, fd-backed files, opener callbacks, and platform error classes.

- [~] os fd API
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: `os.open`, `os.close`, `os.read`, `os.write`, `os.lseek`, `os.fstat`, inheritability, and Windows flag mapping.

