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
# System Stdlib Compatibility

Purpose: make CPython 3.14 `Lib/*.py` system modules run naturally on XLang3.
Do not re-create public pure-Python modules in C++. For each row, run the real
CPython library source, identify the missing runtime/native dependency, add
fixture coverage, then update the row truthfully.

- [ ] `abc`, `types`, and `enum`
  Coverage: public imports currently reach `C:/Python/Python314/Lib`, but fail
  on runtime object-model behavior such as `super()`/descriptor/class metadata.
  Remaining: fix runtime semantics and native `_abc` gaps needed by real
  `abc.py`, `types.py`, and `enum.py`.

- [ ] `io`, `encodings`, and `codecs`
  Coverage: `_io` and `_codecs` native foundations exist.
  Remaining: make CPython `io.py`, `encodings`, and common codec paths run
  through VFS-backed source import without public C++ facades.

- [ ] `collections`, `_collections_abc`, `queue`, and `weakref`
  Coverage: `_collections.deque`, `_queue.SimpleQueue`, and `_weakref` native
  foundations exist.
  Remaining: run real `collections`, `_collections_abc`, `queue`, and
  `weakref.py`; implement missing native helpers such as weak lifetime cleanup
  and `_weakref._remove_dead_weakref` without restoring public facades.

- [ ] `json`, `pickle`, `copy`, and `copyreg`
  Coverage: `_pickle`, `marshal`, and `_struct` native dependencies exist.
  Remaining: run CPython pure modules on top of runtime/native support; do not
  revive native `json` or public `pickle` facades.

- [ ] `traceback`, `inspect`, `linecache`, and `logging`
  Coverage: frame/debug foundations exist.
  Remaining: complete real code/frame/source/traceback objects so these Python
  modules work from CPython `Lib`.

- [ ] `os`, `os.path`, `ntpath`, `posixpath`, `pathlib`, `glob`, and `fnmatch`
  Coverage: native `nt`/`posix`, `_stat`, VFS, and file basics exist.
  Remaining: fill OS/VFS/path protocol gaps required by the real Python modules
  instead of restoring `os.path` or `pathlib` C++ facades.

- [ ] `subprocess`, `_winapi`, `socket`, `select`, and `threading`
  Coverage: `_winapi`, `_socket`, `select`, and native thread foundations exist.
  Remaining: make CPython system/process/thread libraries run against truthful
  native primitives, including XLang3 extensions only through explicit keyword
  options such as future shared-memory transport.

- [ ] `site`, `runpy`, `importlib`, `pkgutil`, and package metadata/resources
  Coverage: import bootstrap native modules exist and CPython source imports
  are preferred over native fallback.
  Remaining: finish source loader/package/resource behavior needed by real
  Python startup and package-discovery modules.
