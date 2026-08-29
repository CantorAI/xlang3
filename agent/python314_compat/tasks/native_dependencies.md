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
# Native Dependency Tasks

- [x] errno
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: none for the current dependency surface.

- [x] _thread subset
  Coverage: `tests/fixtures/core/threading_module.py`
  Remaining: none for the current subset.

- [~] _winapi
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: process, handle, wait, pipe, environment, and detailed Windows error surfaces.

- [~] _stat and os stat structures
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: complete mode constants and all stat result edge cases.

- [~] _io
  Coverage: `tests/fixtures/core/io_module_streams.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: full TextIOWrapper, BufferedIOBase, FileIO, StringIO/BytesIO, detach/reconfigure, and exact errors.

- [~] _socket, select, and _signal
  Coverage: `tests/fixtures/core/socket_select_modules.py`
  Remaining: real socket operations, selectors, signal delivery, and platform constants.

- [~] _weakref and _collections
  Coverage: `tests/fixtures/core/weakref_module.py`, `tests/fixtures/core/collections_queue_modules.py`
  Remaining: lifecycle cleanup, proxy behavior, deque/defaultdict/OrderedDict parity.

- [~] zlib and zipimport
  Coverage: `tests/fixtures/core/zlib_module.py`, `tests/fixtures/core/zipfile_module.py`, `tests/fixtures/core/sys_path_importer_cache.py`
  Remaining: full compression matrix, encrypted ZIP behavior deferred, and import edge cases.

- [~] _pickle and marshal
  Coverage: `tests/fixtures/core/sys_structseq_pickle.py`
  Remaining: full protocol compatibility, recursive object graphs, persistent ids, extension codes, and marshal code-object parity.

