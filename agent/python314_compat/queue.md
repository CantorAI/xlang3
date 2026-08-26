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
# Queue

This queue is derived from `doc/python314-compat-audit.md`. The script reports
the exact unfinished rows from the audit; this file records the intended order.

P0 runtime compatibility:

- Import system model: specs, loaders, packages, package paths, resources.
- Code/frame/traceback model: frame stack, code metadata, trace hooks, exception
  chains, source line mapping.
- Function call binding: positional-only, keyword-only, varargs, kwargs,
  defaults, annotations, signatures.
- Type/object model: descriptors, MRO, metaclass behavior, module/class
  dictionaries, attribute lookup/update invalidation.
- File/runtime path model: VFS-backed `open`, `_io`, path-like protocol,
  encoding, newline, stdio behavior.

P1 native dependency modules:

- `_abc`
- `_thread`
- `_weakref`
- `_collections`
- `_queue`
- `_struct`
- `_pickle`
- `zlib`
- `_socket`
- `select`

P2 CPython standard-library probes:

- Run CPython `Lib/tokenize.py`.
- Run CPython `Lib/linecache.py`.
- Run CPython `Lib/inspect.py`.
- Run CPython `Lib/traceback.py`.
- Run CPython `Lib/importlib`.
- Expand to additional `Lib/*.py` modules once their dependency rows pass.
