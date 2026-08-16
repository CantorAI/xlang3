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
# XLang3 Roadmap Phase 0 To Phase 3

Status: living checkpoint

Current checkpoint when this document was refreshed: `672d6a0 Support class and method calls in C API`, plus the SDK container helper pass.

Current position:

- Phase 0 is functionally complete.
- Phase 1 correctness is active and substantially complete for the current small-Python subset.
- Phase 1 interpreter performance work has started for scalar/local/call/class hot paths.
- Phase 2 and Phase 3 are design targets only; start them in narrow, gated slices after the Phase 1 runtime/module contracts stay stable.

## Phase 0: Skeleton And Runtime Core

Goals:

- [x] repository structure
- [x] C ABI headers
- [x] `X3Value`
- [x] `X3Object`
- [ ] `X3Type`
- [x] refcount helpers
- [x] minimal runtime
- [x] minimal ProgramIR structs
- [x] minimal IR interpreter

Supported code shape:

```python
x = 1 + 2
return x
```

No LLVM.

No GraphIR execution requirement.

Phase 0 status:

Phase 0 is complete enough to treat as closed. `X3Type` is still not fully modeled, but that belongs with the next object protocol pass rather than blocking interpreter progress.

## Phase 1: Python Core Subset

Goals:

- [~] Python 3.14 lexer/parser subset
- [x] AST syntax tree
- [x] sema binding
- [x] local/global/module slots
- [x] functions
- [x] nested functions and closures
- [x] calls
- [x] `if` / `while` / `for`
- [x] list/dict/tuple/set/string basics
- [x] source `.py` imports
- [x] native built-in module imports
- [x] package import basics
- [x] native package C ABI loading from external dynamic libraries
- [x] exception control-flow foundation
- [x] typed exception handlers and `except E as e`
- [x] `finally`
- [x] `with` cleanup on normal and exceptional exits
- [x] class/object protocol foundation
- [ ] fuller Python expression/operator coverage
- [~] fuller list/dict/set/string methods
- [ ] better diagnostic coverage
- [~] benchmark suite expansion

Performance:

- [x] direct local slot access
- [x] scalar op fast paths for current numeric operations
- [x] fused local superinstructions for hot interpreter loops
- [~] basic inline caches
- [x] global/module lookup cache
- [~] call fast path
- [x] C API calls for native functions, XLang functions, bound methods, and class constructors
- [x] C++ SDK calls for native packages, attributes, class construction, bound methods, and container item basics
- [x] benchmark comparison against CPython for current microbenchmarks
- [~] pyperformance integration track

Implemented Phase 1 syntax/runtime subset:

- literals: `None`, bool, int, double, string
- arithmetic: `+`, `-`, `*`, `/`
- comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`
- boolean operators: `and`, `or`, `not`
- variable binding: local, global, nonlocal, module globals
- assignment: names, attributes, subscript item assignment
- functions: parameters, returns, nested functions, closures
- containers: list, tuple, dict, set
- iteration: range/list/tuple/string/dict/set basics
- comprehensions: basic list comprehension with optional `if`
- imports: `import x`, `import x as y`, `import pkg.mod`, `from x import y`, aliases, package `__init__.py`
- exceptions: explicit `raise expr`, catch-all and typed `try` / `except`, `except E as e`, subclass matching, `finally`, and catchable interpreter/native runtime errors
- classes: class statement, class attributes, instance attributes, `__init__`, and simple bound methods
- builtin methods: initial list/dict/set/string method dispatch through attributes
- native modules: `_builtins`, `math`
- external native packages: `json`, `yaml`, `sqlite3`
- native package loader: requested module `M` checks `M.x3pkg.dll` and then `xlang_M.x3pkg.dll` on Windows, with the same prefix rule intended for other platforms using their native extension suffix

Next Phase 1 implementation candidates:

1. Native package metadata/cleanup hook and stronger load diagnostics.
2. More complete container/string builtin method coverage.
3. Broader parser compatibility.
4. Expand guarded inline caches without hardcoding Python-incompatible assumptions.
5. Grow the `benchmarks/pyperformance/supported.txt` subset as stdlib coverage improves.
6. Keep extending the C++ SDK wrapper in ABI-compatible layers instead of exposing internal C++ runtime types.

## Phase 2: Optimized Interpreter And Standard Modules

Goals:

- [ ] broader Python syntax
- [~] exceptions
- [~] classes
- [ ] descriptors/properties subset
- [~] comprehensions
- [ ] decorators
- [ ] generators later if staged
- [ ] async syntax parse first, runtime later
- [~] std/native modules split from core

Performance:

- [ ] attribute inline cache
- [~] attribute inline cache
- [~] call inline cache
- [~] method fast path
- [ ] list/dict/item cache
- [x] global/module cache
- [ ] range/list iteration fast paths
- [ ] profiling counters

Phase 2 gate:

Do not enter Phase 2 broadly until Phase 1 has stable native package loading/cleanup, class foundation, exceptions foundation, and expanded compatibility tests.

## Phase 3: GraphIR, JIT, AOT

Goals:

- [ ] GraphIR for delayed computation
- [ ] array/tensor elementwise graph
- [ ] graph fusion
- [ ] runtime thread pool backend
- [ ] optional OpenMP backend
- [ ] optional LLVM JIT
- [ ] optional LLVM AOT/native compiler

Deployment:

- [x] interpreter deployment has no LLVM dependency
- [ ] JIT deployment requires optional LLVM package/backend
- [ ] AOT output requires XLang3 runtime but not LLVM

Phase 3 gate:

GraphIR/JIT/AOT must remain optional. The interpreter and source import path must keep working without LLVM installed.

## Performance Targets

Phase 1:

```text
correctness first
current microbenchmarks should remain visible and generally competitive with CPython 3.14
```

Phase 2:

```text
ordinary dynamic code approaches CPython 3.14
some scalar/local-slot code faster due to X3Value
```

Phase 3:

```text
array/tensor/graph/hot numeric code significantly faster than CPython
JIT/AOT selected code paths faster than interpreter
```

## Design Checkpoints

Before writing large code, verify:

- [x] AST has no execution methods
- [x] runtime ABI headers are C-compatible
- [x] extensions can be compiled with different compilers through the C ABI boundary
- [x] ProgramIR can run without parser
- [ ] interpreter can run serialized/prebuilt IR
- [x] LLVM code is optional
- [~] modules do not depend on parser internals

Notes:

- Source module loading currently parses and lowers `.py` files at import time, so source imports still depend on parser/sema. Native packages do not need parser internals.
- Serialized/prebuilt IR is not implemented yet. The IR structure is separate enough to support it later.
- Benchmarking now has two tracks: `benchmarks/cases` for VM microbenchmarks, and `benchmarks/pyperformance` for alignment with the Python ecosystem benchmark suite. The pyperformance supported subset starts empty until benchmarks run unchanged on both CPython and XLang3.
- Current interpreter fast paths include fused local ops, scalar arithmetic fast paths, guarded tiny-function/tiny-method execution, and class-version guarded call caches.
- The runtime deliverables are split into `xlang3_runtime` shared library, `xlang3_runtime_static`, `xlang3` CLI, and external native package binaries under the runtime module search path.
