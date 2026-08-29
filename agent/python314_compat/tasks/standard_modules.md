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
# Standard Module Tasks

- [ ] remove pure-stdlib C++ facade dependency
  Coverage: add CPython `Lib/*.py` import/run probes for modules as runtime support lands.
  Remaining: keep pure stdlib shims out of the runtime tree, split mixed native/facade files, and route pure modules through source import.

- [~] os and nt/posix
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: fd APIs starting with `os.open`, close/read/write/lseek/fstat, environment mutation parity, process helpers, and Windows error mapping.

- [~] os.path, pathlib, stat, glob, fnmatch
  Coverage: `tests/fixtures/core/logging_pathlib_modules.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: complete path normalization, drive/UNC behavior, scandir/stat integration, and path-like edge cases.

- [~] sys
  Coverage: `tests/fixtures/compat_sections/standard_modules.py` plus focused `tests/fixtures/core/sys_*.py`
  Remaining: full startup flags/config internals, remaining profile edge cases, and remaining PEP 669 monitoring events.

- [~] time
  Coverage: `tests/fixtures/core/time_module.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: broader locale-specific parsing and historical DST behavior.

- [~] abc and _abc
  Coverage: `tests/fixtures/core/abc_module_metadata.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: exact weakref lifecycle cleanup and remaining cache invalidation edge cases.

- [~] importlib, pkgutil, runpy, site
  Coverage: `tests/fixtures/core/importlib_module.py`, `tests/fixtures/core/runpy_module.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: CPython source loader path, package resources, namespace packages, site initialization, and import lock parity.

- [~] codecs, locale, string, tokenize
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: full codec registry/error handlers, locale database behavior, formatter helpers, tokenizer edge cases.

- [~] json, pickle, marshal, struct
  Coverage: `tests/fixtures/core/json_module.py`, `tests/fixtures/core/sys_structseq_pickle.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: exact protocol coverage; avoid rewriting pure Python stdlib in C++ unless it is a native dependency.

- [~] argparse, ast, code, dis, enum, dataclasses, typing, numbers
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: support CPython pure Python modules by filling runtime primitives and native dependencies they require.

- [~] functools, itertools, operator, collections, queue
  Coverage: `tests/fixtures/core/collections_queue_modules.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: exact iterator/helper semantics and CPython diagnostics.

- [~] traceback, inspect, linecache, logging, warnings
  Coverage: `tests/fixtures/core/traceback_module.py`, `tests/fixtures/core/inspect_module.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: real frame/code/source integration and warning filter semantics.

- [~] socket, subprocess, winreg, urllib.parse, xmlrpc, http
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: socket/select foundations, subprocess fd/process support, registry APIs, and pure Python stdlib dependency chain.
