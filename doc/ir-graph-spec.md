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
# Graph IR Spec

Status: draft 0

## Purpose

GraphIR represents delayed, optimizable dataflow computation.

It is inspired by current XLang TensorGraph, but generalized and separated from ProgramIR.

GraphIR is not the whole language. It is used for regions that can be delayed, optimized, fused, parallelized, JIT compiled, or AOT compiled.

## Relationship To ProgramIR

```text
ProgramIR: control flow, functions, modules, Python-style dynamic execution
GraphIR: dataflow computation regions
```

ProgramIR may contain:

```text
BuildGraph
RunGraph
CallGraph
```

or references to graph functions.

## Entry Points

Possible graph creation mechanisms:

```python
@xlang.graph
def f(a, b, c):
    return sin(a * b + c)
```

```python
with xlang.graph:
    y = sin(a * b + c)
```

Tensor/Array operations may also lazily create GraphIR depending on runtime policy.

## Allowed Phase 0 Graph Operations

Only allow operations with clear purity and dataflow behavior:

- scalar arithmetic
- array/tensor elementwise ops
- known pure functions
- comparisons
- select/where
- shape operations
- simple reductions later

Forbidden initially:

- arbitrary mutation
- dynamic import
- arbitrary object method calls
- I/O
- exceptions inside graph
- reflection
- unknown side effects

## Graph Structure

```text
GraphIR
  inputs
  outputs
  nodes
  edges
  constants
  attributes
```

```text
GraphNode
  id
  op
  inputs[]
  outputs[]
  attrs
  source_span
```

```text
GraphValue
  id
  producer
  type_hint
  shape_hint
```

## Control Flow

Borrow concept from TensorGraph `FlowBlock`, but generalize it.

Initial support:

```text
Select cond true_value false_value
```

Later support:

```text
GraphIf
GraphLoop
```

Do not map arbitrary Python `if/while` into GraphIR unless the body is graph-safe.

## Optimizations

Graph optimizer should support:

- constant folding
- dead node elimination
- common subexpression elimination
- elementwise fusion
- broadcast planning
- layout planning
- shape inference
- kernel selection
- parallel scheduling

## Backends

GraphIR can execute through:

```text
graph_interpreter
cpu_scalar_kernel
cpu_simd_kernel
cpu_threadpool_kernel
cpu_openmp_kernel optional
llvm_jit_kernel optional
llvm_aot_kernel optional
gpu backend later
```

OpenMP must be optional. The default parallel backend should be XLang3 runtime thread pool when implemented.

## Array/Tensor Policy

Array operations should lower to GraphIR when:

- operation is elementwise or reduction-like
- no visible side effect is required immediately
- data size is large enough or lazy policy is enabled

Execution policy:

```text
small arrays: scalar/SIMD direct
medium arrays: SIMD single-thread
large arrays: thread-pool or optional OMP
hot graph: LLVM JIT
production: AOT graph kernel
```

Thresholds must be benchmark-controlled, not hard-coded forever.

## Current TensorGraph Concepts To Keep

Keep conceptually:

- build context
- visited map
- run item list
- flow/branch metadata
- graph code generation

Do not keep as general design:

- tensor-only node assumptions
- graph tied to XObj virtual calls
- graph builder reading arbitrary AST execution state

