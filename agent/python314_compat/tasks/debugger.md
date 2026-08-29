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
# Debugger Compatibility Tasks

- [~] Python CLI compatibility for IDE launch
  Coverage: `tests/fixtures/core/sys_command_path.py`
  Remaining: full CPython command-line flag matrix and environment startup behavior.

- [~] sys.settrace and threading.settrace
  Coverage: `tests/fixtures/core/trace_hooks.py`, `tests/fixtures/core/trace_events.py`
  Remaining: CPython edge cases for line offsets, exceptions, returns, generators, coroutines, and thread startup.

- [~] sys.setprofile and monitoring
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: complete C call/return/exception matrix and all PEP 669 event paths.

- [~] frame/code/source APIs for debugpy
  Coverage: `tests/fixtures/core/debug_frame_metadata.py`, `tests/fixtures/core/inspect_currentframe.py`
  Remaining: real frame locals/globals/code attributes, linecache/source mapping, and inspect parity.

- [~] debugpy adapter execution
  Coverage: manual Visual Studio/debugpy probes.
  Remaining: run Visual Studio's `debugpy/adapter` naturally through XLang3 after runtime stdlib compatibility is strong enough.

