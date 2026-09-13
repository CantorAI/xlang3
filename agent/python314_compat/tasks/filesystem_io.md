# Filesystem And IO Tasks

- [x] VFS abstraction
  Coverage: `tests/fixtures/core/vfs_file_io.py`
  Coverage update: VFS supports longest-prefix virtual mounts alongside the
  host filesystem and normalizes virtual paths independently from Windows host
  paths. Replacing the root resets the host cwd for embedded runtimes. CLI and
  C API source-file reads now use `Runtime::vfs()`, as do source imports and
  builtin `open()`.
  The RP2040 `DeviceFileSystem` implements the full VFS contract, including
  same-mount rename, implicit-directory creation, and cross-mount rejection.
  RAM and flash stores now return the same direct-child listing shape and
  safely support empty files.
  Validation: `tests/cpp/vfs_tests.cpp` mounts `/ram` and `/flash` over the host
  VFS, imports a real Python source module from flash, writes through builtin
  `open()` to RAM, and covers listing, rename, mkdir, and cross-mount behavior.
  Remaining: none for the scoped VFS contract.

- [x] file object and open
  Coverage: `tests/fixtures/core/file_context_open.py`, `tests/fixtures/core/file_io_compat.py`
  Coverage update: `tests/fixtures/core/filesystem_io_edges.py` covers buffered
  and fd-backed files, `closefd=False`, custom opener callbacks, newline
  translation, and exact path error classes. Windows opener callbacks now
  receive a binary, non-inheritable raw descriptor flag set, matching CPython
  and preventing duplicate CRLF translation in XLang3's text layer.
  Validation: the canonical fixture suite passes this coverage together with
  the existing `_io` buffering, newline, and stream matrix recorded in
  `native_dependencies.md`.
  Remaining: none for the supported file/open surface.

- [x] os fd API
  Coverage: `tests/fixtures/compat_sections/system_stdlib.py` covers CPython
  `Lib/os.py` delegation for `open`, `close`, `read`, `write`, `lseek`,
  `fstat`, `dup`, `dup2`, `pipe`, `isatty`, `get_inheritable`, and
  `set_inheritable`.
  Coverage update: native fd calls preserve CRT errno instead of collapsing
  failures to `EBADF`; `os.open()` maps missing, exclusive, and directory
  failures to CPython's exact exception classes and normalized filename;
  Windows invalid `fstat()` exposes `winerror=6`. File descriptors, sizes,
  offsets, flags, and seek modes accept `__index__`, with CPython-compatible
  overflow handling.
  Validation: CPython 3.14 `test_os.FileTests` passes all 10 applicable cases
  (12 platform/capability skips), `TestInvalidFD` passes all 10 applicable
  cases (10 skips), `FDInheritanceTests` passes all 8 applicable cases (3
  skips), and all 6 `Win32ErrorTests` pass. The Release CTest suite passes
  48/48 and the full canonical fixture suite passes.
  Remaining: none for the supported Windows fd surface.
