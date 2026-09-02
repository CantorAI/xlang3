# Pure Stdlib Shim Cleanup

- [x] remove pure Python stdlib C++ shims from the build and source tree
  Coverage: Release build without facade sources; `agent/scripts/check_module_boundaries.py`.
  Remaining: none for the removed facades.

- [x] split mixed native/facade modules
  Coverage: `agent/scripts/check_module_boundaries.py` confirms public
  facades are no longer registered for `asyncio`, `ctypes`, `threading`,
  `warnings`, `signal`, `abc`, `ast`, `io`, `queue`, `pickle`, `string`,
  `weakref`, `collections`, `opcode`, `types`, `locale`, `json`, `argparse`,
  `inspect`, `pathlib`, `re`, `typing`, `subprocess`, or `zipfile`.
  Remaining: none for top-level facade registration removal.

- [x] remove pure facade source files
  Coverage: CMake build, import fallback probes.
  Remaining: none for removed facade files.

- [x] convert compatibility checks to real CPython Lib probes
  Coverage: `tests/tools/probe_xlang3_imports.py`,
  `agent/scripts/check_module_boundaries.py`, and section fixtures verify public
  pure-Python stdlib modules load from `C:/Python/Python314/Lib` while native
  C++ code stays limited to runtime primitives, private accelerators, and
  product modules.
  Remaining: none for probe conversion. Future failures must be fixed by
  runtime semantics or native dependency modules, not by adding public C++
  facades for CPython `Lib/*.py` modules.
