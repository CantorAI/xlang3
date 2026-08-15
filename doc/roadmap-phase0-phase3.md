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

Status: draft 0

## Phase 0: Skeleton And Runtime Core

Goals:

- repository structure
- C ABI headers
- `X3Value`
- `X3Object`
- `X3Type`
- refcount helpers
- minimal runtime
- minimal ProgramIR structs
- minimal IR interpreter

Supported code shape:

```python
x = 1 + 2
return x
```

No LLVM.

No GraphIR execution requirement.

## Phase 1: Python Core Subset

Goals:

- Python 3.14 lexer/parser subset
- AST syntax tree
- sema binding
- local/global/module slots
- functions
- calls
- if/while/for
- list/dict/tuple/string basics
- imports for source and native packages
- native package C ABI

Performance:

- direct local slot access
- scalar op fast paths
- basic inline caches

## Phase 2: Optimized Interpreter And Standard Modules

Goals:

- broader Python syntax
- exceptions
- classes
- descriptors/properties subset
- comprehensions
- decorators
- generators later if staged
- async syntax parse first, runtime later
- std modules split from core

Performance:

- attribute inline cache
- call inline cache
- method fast path
- list/dict/item cache
- global/module cache
- range/list iteration fast paths
- profiling counters

## Phase 3: GraphIR, JIT, AOT

Goals:

- GraphIR for delayed computation
- array/tensor elementwise graph
- graph fusion
- runtime thread pool backend
- optional OpenMP backend
- optional LLVM JIT
- optional LLVM AOT/native compiler

Deployment:

- interpreter deployment has no LLVM dependency
- JIT deployment requires optional LLVM package/backend
- AOT output requires XLang3 runtime but not LLVM

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

- AST has no execution methods
- runtime ABI headers are C-compatible
- extensions can be compiled with different compilers
- ProgramIR can run without parser
- interpreter can run serialized/prebuilt IR
- LLVM code is optional
- modules do not depend on parser internals

