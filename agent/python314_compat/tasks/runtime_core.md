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
# Runtime Core Tasks

- [x] core value and object model
  Coverage: `tests/fixtures/compat_sections/core_value_and_object_model.py`
  Remaining: none in the current scoped audit.

- [x] functions and calls
  Coverage: `tests/fixtures/compat_sections/functions_and_calls.py`
  Remaining: none in the current scoped audit.

- [x] exceptions
  Coverage: `tests/fixtures/compat_sections/exceptions.py`
  Remaining: none in the current scoped audit.

- [~] import runtime internals
  Coverage: `tests/fixtures/compat_sections/imports_and_modules.py`
  Remaining: `.pyc` cache execution, exact import-lock behavior, and frozen bytecode table parity.

- [~] frame, code, and traceback internals
  Coverage: `tests/fixtures/core/debug_frame_metadata.py`, `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: full CPython frame/code object edge behavior required by pure Python stdlib and debuggers.

