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
# Module Policy

XLang3 compatibility is runtime-first.

Pure Python CPython standard-library modules must normally run from Python
source. Do not implement or extend them as C++ facades just to satisfy imports
or fixtures.

Native C++ is appropriate for:

- runtime core: values, objects, frames, calls, exceptions, import, and VFS
- builtins and builtin types
- real CPython native dependency modules such as `_io`, `_thread`, `_abc`,
  `_weakref`, `_collections`, `_queue`, `_socket`, `_signal`, `_stat`, `_struct`,
  `_pickle`, `_winapi`, `errno`, `time`, `marshal`, `zlib`, `winreg`, and
  `unicodedata`
- XLang3 product modules and package ABI support

Temporary C++ shims for pure Python stdlib modules are not product completion.
Do not add or keep them in the runtime tree. Historical facade sources should
be removed, not disabled behind build flags.

When `import typing`, `import inspect`, `import argparse`, or another pure
stdlib module fails, the fix should normally be one of:

- add a missing syntax/runtime primitive
- add a missing builtin/builtin-type behavior
- add a missing native dependency module
- fix import/source/bytecode/VFS behavior
- add fixture coverage proving the real CPython `Lib/*.py` path moves farther

Do not mark a task `[x]` because a native facade imports. Mark it done only when
the scoped CPython-compatible behavior is implemented and fixture-covered.
