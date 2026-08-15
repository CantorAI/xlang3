# Executor Spec

Status: draft 0

## Purpose

Executors run ProgramIR and GraphIR.

XLang3 must support:

1. IR interpreter
2. optional LLVM JIT
3. optional LLVM AOT/native compiler

All executors consume IR, not AST.

## Executor ABI

Use C-style dispatch, not C++ virtual methods:

```c
typedef struct X3ExecutorVTable {
    X3Status (*run_module)(
        X3Executor* exec,
        X3Runtime* rt,
        X3IRModule* module,
        X3Value* result);

    X3Status (*run_function)(
        X3Executor* exec,
        X3Runtime* rt,
        X3IRFunction* fn,
        const X3Value* args,
        uint32_t argc,
        X3Value* result);

    void (*destroy)(X3Executor* exec);
} X3ExecutorVTable;

typedef struct X3Executor {
    const X3ExecutorVTable* vt;
    void* impl;
} X3Executor;
```

## Interpreter

The interpreter is always available.

Responsibilities:

- execute ProgramIR
- maintain frames
- maintain registers
- maintain refcounts
- update inline caches
- collect profiling counters
- support debugging
- fallback target for JIT deopt

### Interpreter Frame

```c
typedef struct X3InterpFrame {
    X3IRFunction* fn;
    X3Value* locals;
    X3Value* regs;
    uint32_t ip;
    X3InterpFrame* caller;
} X3InterpFrame;
```

### Dispatch

Phase 0:

```c
switch (instr->op) { ... }
```

Later:

- computed goto if compiler/platform supports it
- superinstructions if useful
- specialized instructions

## Inline Caches

Inline caches are executor-owned side tables.

Dynamic instructions reference `cache_index`.

Cache types:

```text
LoadAttrCache
StoreAttrCache
LoadGlobalCache
BinaryOpCache
CallCache
MethodCache
ItemCache
IterCache
```

Example:

```c
typedef struct X3LoadAttrCache {
    X3Type* seen_type;
    uint32_t shape_version;
    uint32_t slot_offset;
    X3GetAttrFastFn fast;
    uint32_t hits;
    uint32_t misses;
} X3LoadAttrCache;
```

## Fast Paths

Required interpreter fast paths:

- `LoadLocal`: direct frame slot
- `StoreLocal`: direct frame slot
- int64 arithmetic
- double arithmetic
- bool truth
- list index by int
- array index by int
- range iteration
- list iteration
- direct native function call
- cached attribute lookup
- cached method call

## Profiling

Interpreter collects:

- instruction execution count
- cache hit/miss counts
- observed operand tags/types
- function call counts
- loop backedge counts

This data feeds JIT decisions.

## LLVM JIT

LLVM JIT is optional.

It must live outside core runtime:

```text
src/executor/jit/llvm
```

JIT policy:

```text
interpreter first
hot function/block detected
lower ProgramIR to LLVM IR
compile machine code
install compiled entry
fallback/deopt to interpreter when assumptions fail
```

Initial JIT subset:

- int/double arithmetic
- local slots
- simple branches
- simple loops
- direct known native calls

Do not require LLVM on machines that only use interpreter.

## AOT/Native

AOT compiler is optional and may share LLVM lowering with JIT.

Output:

- object file
- static library
- shared library
- executable

Deployment should require:

- compiled output
- XLang3 runtime
- required modules

Deployment should not require:

- LLVM
- CPython
- compiler toolchain

unless explicitly requested.

## Graph Executor

Graph executor runs GraphIR.

Backends:

- graph interpreter
- CPU scalar
- CPU SIMD
- runtime thread pool
- optional OpenMP
- optional LLVM kernel JIT
- optional LLVM kernel AOT

Graph executor may be invoked from ProgramIR.

## Deoptimization

JIT assumptions must be explicit:

- type identity
- shape version
- global version
- function target
- array dtype/shape

If assumption fails, compiled code returns/falls back to interpreter.

## Debugging

ProgramIR instructions must preserve source spans. Interpreter supports full debug first.

JIT/AOT debug support can be staged.

