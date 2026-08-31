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

- [ ] convert compatibility checks to real CPython Lib probes
  Coverage: manual probes confirm `json` reaches `C:/Python/Python314/Lib/json/__init__.py`, `argparse` reaches `C:/Python/Python314/Lib/argparse.py`, and `inspect` reaches `C:/Python/Python314/Lib/inspect.py`.
  Remaining: add scripted probes; current real-source failures include `abc.py`/`enum.py` class/super behavior, `types.py` descriptor/string behavior, `_weakref._remove_dead_weakref`, `_collections_abc`, `annotationlib.py` parser/runtime coverage, and `encodings` import/runtime coverage.
