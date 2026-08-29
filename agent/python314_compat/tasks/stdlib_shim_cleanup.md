<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# Pure Stdlib Shim Cleanup

- [x] remove pure Python stdlib C++ shims from the build and source tree
  Coverage: Release build without facade sources; registration/source-reference search is clean.
  Remaining: none for the removed facades.

- [ ] split mixed native/facade modules
  Coverage: module-specific CPython Lib probes.
  Remaining: split `_queue` from `queue`, `_socket` from `socket`, `_io` from `io`, `_pickle` from `pickle`, `_stat` from `stat`, `_string` from `string`, `_opcode` from `opcode`, `_warnings` from `warnings`, `_abc` from `abc`, and `_ast` from `ast`.

- [x] remove pure facade source files
  Coverage: CMake build, import fallback probes.
  Remaining: none for removed facade files.

- [ ] convert compatibility checks to real CPython Lib probes
  Coverage: manual probes confirm `json` reaches `C:/Python/Python314/Lib/json/__init__.py`, `argparse` reaches `C:/Python/Python314/Lib/argparse.py`, and `inspect` reaches `C:/Python/Python314/Lib/inspect.py`.
  Remaining: add scripted probes; current real-source failures include `enum.py` class/super behavior and `annotationlib.py` parser/runtime coverage.
