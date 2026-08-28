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
# State

Current phase:

```text
Python 3.14 runtime compatibility.
```

Current doctrine:

```text
Make the runtime compatible enough to run CPython standard-library .py files.
Do not grow native pure-Python stdlib clones as the main strategy.
```

Last stable compatibility checkpoint:

```text
sys jit helper text signatures
```

Current next loop:

```text
Read doc/python314-compat-audit.md.
Pick the next unfinished runtime primitive or native dependency item.
Implement the runtime behavior.
Add fixture coverage under tests/fixtures.
Build with Visual Studio CMake.
Run agent/scripts/run_fixtures.py.
Commit only after tests pass.
If stopped or killed, rerun the same command and resume from loop_state.json.
```
