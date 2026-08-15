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

Current head when this checkpoint was written: `51e9988 Complete basic import semantics`

Current position:

- Phase 0 is functionally complete.
- Phase 1 correctness is active and partially complete.
- Phase 1 performance work has not meaningfully started.
- Phase 2 and Phase 3 are design targets only; do not start them until the Phase 1 interpreter is coherent.

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
- [ ] native package C ABI loading from external dynamic libraries
- [ ] fuller Python expression/operator coverage
- [ ] fuller list/dict/set/string methods
- [ ] better diagnostic coverage
- [ ] benchmark suite expansion

Performance:

- [x] direct local slot access
- [x] scalar op fast paths for current numeric operations
- [ ] basic inline caches
- [ ] global/module lookup cache
- [ ] call fast path
- [ ] benchmark comparison against CPython for representative cases

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
- native modules: `_builtins`, `math`

Next Phase 1 implementation candidates:

1. Exception foundation: `raise`, `try/except/finally`, runtime error object shape.
2. Class/object protocol foundation: class statement, instance attributes, simple methods.
3. Method calls and container/string builtin methods.
4. External native package loading through the C ABI.
5. Inline caches after the object/type protocol is stable.

## Phase 2: Optimized Interpreter And Standard Modules

Goals:

- [ ] broader Python syntax
- [ ] exceptions
- [ ] classes
- [ ] descriptors/properties subset
- [~] comprehensions
- [ ] decorators
- [ ] generators later if staged
- [ ] async syntax parse first, runtime later
- [ ] std modules split from core

Performance:

- [ ] attribute inline cache
- [ ] call inline cache
- [ ] method fast path
- [ ] list/dict/item cache
- [ ] global/module cache
- [ ] range/list iteration fast paths
- [ ] profiling counters

Phase 2 gate:

Do not enter Phase 2 broadly until Phase 1 has external native package loading, classes foundation, exceptions foundation, and expanded compatibility tests.

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
performance acceptable, not necessarily faster than CPython
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
- [~] extensions can be compiled with different compilers
- [x] ProgramIR can run without parser
- [ ] interpreter can run serialized/prebuilt IR
- [x] LLVM code is optional
- [~] modules do not depend on parser internals

Notes:

- Source module loading currently parses and lowers `.py` files at import time, so source imports still depend on parser/sema. Native modules do not need parser internals.
- Serialized/prebuilt IR is not implemented yet. The IR structure is separate enough to support it later.
