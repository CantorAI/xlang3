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
# XLang3 Implementation Spec

Status: draft 0

Purpose: implementation guide for XLang3. This document is for engineering decisions and future coding work, not end-user documentation.

## Core Goals

XLang3 is a redesign of XLang with these fixed goals:

1. Keep `X::Value`-style universal value representation.
2. Keep scalar values stored directly in the value carrier.
3. Keep complex values as XLang runtime objects.
4. Do not base the runtime on `PyObject*` or CPython internals.
5. Do not make tracing GC the core memory model.
6. Target Python 3.14 syntax compatibility.
7. Use a clean pipeline:

```text
source -> lexer/parser -> AST -> sema/binding -> IR -> executor -> runtime
```

## Non-Goals

XLang3 is not:

- a CPython fork
- a CPython extension ABI clone
- a TensorGraph-only language
- a pure C language runtime if C++ can be used behind a stable C ABI
- a parser extension of the current XLang token/operator stack design

## Major Design Rules

1. AST does not execute.
2. Parser does not know runtime object details.
3. Sema owns name binding, scope resolution, and slot assignment.
4. ProgramIR owns executable semantics.
5. GraphIR owns delayed/optimizable computation regions.
6. Executors consume IR only.
7. Runtime object ABI is C-style and compiler-neutral.
8. C++ helper APIs are allowed, but must not define the extension ABI.
9. LLVM is optional.
10. Native extension packages must be buildable by MSVC, Clang, GCC/MinGW, Rust, Zig, or C, as long as they export the C ABI.

## Proposed Source Tree

```text
xlang3/
  doc/
  include/
    xlang3/
      xapi.h
      xvalue.h
      xtype.h
      xobject.h
      xruntime.h
      xmodule.h
      xpackage.h
      xir.h
      xgraph.h
      cpp/
        value.hpp
        package.hpp
        binding.hpp
  src/
    core/
      value/
      object/
      type/
      memory/
      runtime/
      frame/
      module/
      import/
      package/
      abi/
    parser/
      lexer/
      grammar/
      ast/
      diagnostics/
    sema/
      symbols/
      scopes/
      binding/
      lowering/
    ir/
      program/
      graph/
      verify/
      print/
      serialize/
    executor/
      interpreter/
      jit/
        llvm/
      native/
        llvm/
  modules/
    std/
      text/
      json/
      yaml/
      http/
      os/
      time/
      sqlite/
      net/
      image/
      tensor/
  tools/
  tests/
  examples/
  third_party/
  cmake/
```

## Layer Ownership

### Runtime Core

Owns:

- `X3Value`
- `X3Object`
- `X3Type`
- refcount helpers
- frame memory
- runtime errors/exceptions
- module/package registry
- native ABI

Does not own:

- parser grammar
- AST nodes
- Python syntax rules
- LLVM lowering

### Parser

Owns:

- Python 3.14-compatible tokenization
- indentation handling
- AST construction
- syntax errors and source spans

Does not own:

- runtime variable lookup
- type layout
- execution
- XPackage loading

### Sema

Owns:

- symbol resolution
- local/global/nonlocal binding
- scope creation
- slot index assignment
- closure/free-var planning
- import metadata
- lowering AST to ProgramIR and GraphIR

### IR

Owns:

- explicit executable representation
- slots/registers
- basic blocks
- control-flow edges
- graph nodes
- source spans for debugging
- IR validation

### Executors

Own:

- execution strategy
- dispatch
- inline caches
- profiling counters
- JIT/AOT optional backends

Executors must not consume AST directly.

## Compatibility Strategy

Syntax target:

```text
Python 3.14
```

Runtime target:

```text
Phase 0: minimal executable subset
Phase 1: common Python core
Phase 2: broader Python syntax/runtime behavior
Phase 3: JIT/AOT/Graph optimization
```

XLang3 should parse more than it can initially execute. Unsupported runtime features should lower to explicit `UnsupportedFeature` diagnostics, not parser failures when syntax is valid Python 3.14.

## Current XLang Concepts To Retain

- universal value carrier
- direct scalar value storage
- refcounted object model
- indexed variable slots after binding
- package/native extension development experience
- module object/package concepts
- tensor/graph idea as separate GraphIR inspiration

## Current XLang Concepts To Replace

- parser operator registry as grammar engine
- single-pass token consumption as a hard constraint
- AST `Exec()` as primary execution model
- C++ virtual object ABI across extension boundary
- parser-driven semantic shape decisions
- TensorGraph as the only graph model

## Initial Implementation Order

1. Define C ABI headers.
2. Implement `X3Value`, `X3Object`, `X3Type`, refcount.
3. Implement minimal ProgramIR structs.
4. Implement IR interpreter.
5. Implement minimal lexer/parser subset.
6. Implement sema binding to slots.
7. Lower subset to IR.
8. Add native package loading with C ABI.
9. Add inline caches.
10. Add GraphIR.
11. Add optional LLVM JIT.
12. Add optional LLVM AOT.

