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
# XLang3 Tests

The test tree is arranged by checkpoint layer.

- `cpp/`: C++ unit tests for internal parser, runtime, IR lowering, and interpreter behavior.
- `fixtures/core/`: `.py` programs that must run through the public CLI.
- `fixtures/expected/`: expected stdout for fixture programs.
- `run_fixtures.ps1`: Windows fixture runner used by CTest.
- `sdk_c_header_smoke.c`: C-only public SDK header check.

Checkpoint rule: before growing the language surface, Release and Debug builds should pass CTest.

Current checkpoint:

```text
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Function work can proceed after this checkpoint stays green. Tuple should be added before Python-compatible varargs or argument unpacking.
