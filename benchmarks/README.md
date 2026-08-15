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
# XLang3 Benchmarks

Benchmarks compare XLang3 against CPython on the same pure Python source files.

Initial goals:

- keep benchmark sources small and readable
- compare interpreter-only XLang3 first
- record command lines, Python version, compiler, CPU, and build mode
- avoid benchmarking startup cost unless the benchmark is explicitly about startup

Current groups:

- `scalar_arithmetic`: integer and floating-point operator dispatch
- `local_slots`: local variable load/store and loop behavior
- `function_calls`: direct function calls and argument passing
- `class_construct`: class calls, `__init__`, instance attributes, and method calls
- `branches`: if/else and comparison dispatch
- `list_append`: native method binding and list append behavior
- `range_for`: range iteration and for-loop dispatch

Additional tracks:

- `cases/`: XLang3 microbenchmarks for IR and interpreter tuning.
- `pyperformance/`: integration notes and helper scripts for the Python ecosystem benchmark suite.

XLang3 runs `.py` files directly. Benchmark files should therefore be valid Python files that can run on both CPython and XLang3 whenever the implemented language subset allows it.

The first milestone is not to beat CPython everywhere. It is to make interpreter performance visible early, especially where `X3Value` scalar fast paths and sema-assigned local slots should help.

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\benchmarks\run.ps1 -XLang3 .\build\Release\xlang3.exe -Python python
```

For the larger Python benchmark suite, see `benchmarks/pyperformance/README.md`.
