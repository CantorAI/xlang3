# XLang3 IR Execution Performance: Direct Dispatch and AsmJit Native JIT Prototype

## Objective

Inspect the current XLang3 IR execution implementation and investigate/prototype two approaches to improve IR execution performance.

Do **not** redesign the XLang3 IR itself. The goal is specifically to reduce the runtime overhead of executing the **existing IR**.

The two experiments are:

1. Replace switch-based IR dispatch with direct handler pointers.
2. Prototype direct XLang3 IR → native machine code using AsmJit.

The primary question this work should answer is:

> **How close can XLang3 IR execution get to native performance without making LLVM a required runtime dependency?**

---

# 1. Repository Investigation

Before modifying code, inspect the XLang3 repository and identify the following.

## 1.1 IR Instruction Representation

Find the current representation of an IR instruction.

For example, determine whether it resembles:

```cpp
struct Instruction {
    Opcode opcode;
    ...
};
```

Document:

- opcode representation
- operands
- result/destination representation
- type information
- constants/immediates
- branch targets
- function/call targets
- source/debug metadata

Do not assume the exact structure above. Use the actual XLang3 implementation.

## 1.2 Current IR Executor

Locate the main IR execution loop.

Determine whether dispatch currently resembles:

```cpp
while (...) {
    switch (instruction.opcode) {
        case ADD:
            ...
            break;

        case CALL:
            ...
            break;

        ...
    }
}
```

Identify additional work performed for every instruction, including:

- operand lookup
- register/slot lookup
- `X::Value` construction/destruction
- type checking
- type conversion
- object access
- branch resolution
- function lookup
- runtime helper calls

The purpose is to determine how much time is actually spent on opcode dispatch versus other interpreter overhead.

## 1.3 `X::Value`

Inspect the current `X::Value` representation and its use by the IR executor.

Pay particular attention to:

- scalar representation
- object representation
- type tags
- copying/moving
- reference management
- constructors/destructors
- boxing/unboxing
- arithmetic operators
- temporary values

Determine whether `X::Value` handling becomes the dominant cost once dispatch overhead is reduced.

## 1.4 Typed/Specialized IR Operations

Identify any existing typed or specialized operations.

For example, determine whether XLang3 already distinguishes operations equivalent to:

```text
ADD_I64
ADD_F64
ADD_DYNAMIC
```

Do not invent new opcodes unless required for the experiment.

Prefer using existing XLang3 type information and existing IR operations.

## 1.5 Functions and Basic Blocks

Inspect how XLang3 represents:

- functions
- basic blocks
- branches
- loops
- calls
- returns
- local values/registers/slots

This will determine the natural compilation unit for native JIT.

## 1.6 Build System

Inspect the current build system and determine the cleanest place for an optional AsmJit dependency.

Prefer:

```text
XLang3
 |-- IR
 |-- Runtime
 |-- NativeCodeGen
 |    `-- AsmJitBackend
 `-- ...
```

The rest of XLang3 should not directly depend on AsmJit APIs.

---

# 2. Experiment A — Direct Handler-Pointer IR Dispatch

First prototype a lower-overhead interpreter while preserving the existing IR semantics.

## 2.1 Current Model

If execution currently resembles:

```text
IR instruction
      |
read opcode
      |
large switch
      |
handler
```

replace repeated opcode decoding with a prepared executable representation.

## 2.2 Prepared/Executable IR

Keep the existing XLang IR representation intact where possible.

Introduce a separate execution representation conceptually similar to:

```cpp
using Handler = void (*)(ExecutionContext&, ExecInstruction&);

struct ExecInstruction {
    Handler handler;

    // prepared operands / metadata
    ...
};
```

During IR preparation/loading:

```text
XLang IR
    |
prepare/link once
    |
Executable IR
    |
direct handler pointer
```

Resolve:

```text
opcode → handler
```

once.

The hot execution loop should then become approximately:

```cpp
while (...) {
    pc->handler(context, *pc);
}
```

instead of repeatedly executing:

```cpp
switch (pc->opcode) {
    ...
}
```

## 2.3 Pre-Resolve More Than the Opcode

Use the preparation stage to eliminate repeated interpreter work wherever practical.

Candidates include:

```text
opcode
    -> handler pointer

operand IDs
    -> direct slot/register references

branch IDs
    -> ExecInstruction* / prepared block target

function IDs
    -> prepared function/call target

constants
    -> prepared constant representation

types
    -> prepared type information
```

Conceptually:

```text
Serialized / canonical IR
          |
      prepare()
          |
Executable IR
          |
minimal runtime decoding
```

Avoid changing serialized IR unless necessary.

## 2.4 Computed Goto / Threaded Dispatch

Investigate whether computed-goto/threaded dispatch is useful on supported compilers.

For example, GCC/Clang may support something conceptually like:

```cpp
goto *dispatch_table[opcode];
```

However:

- portable function-pointer dispatch should be the baseline
- compiler-specific threaded dispatch should remain optional
- do not make XLang3 dependent on compiler-specific behavior

Benchmark it separately if it is easy to support.

## 2.5 Benchmark

Create a microbenchmark containing a very large number of simple IR operations so dispatch overhead is measurable.

For example:

```text
r1 = ...
r2 = ...

repeat many times:
    r3 = ADD r1, r2
    r4 = SUB r3, r1
    r5 = ADD r4, r2
    ...
```

Compare:

```text
A. current switch executor
B. prepared handler-pointer executor
C. computed-goto executor, if implemented
```

Measure:

- total execution time
- IR instructions/sec
- ns per IR instruction
- relative speedup

Also profile where execution time goes.

Specifically determine the percentage attributable to:

```text
dispatch
operand access
X::Value
type handling
object handling
branches
runtime helpers
```

The important result is not simply whether pointer dispatch is faster.

Identify the **next bottleneck after dispatch is removed**.

---

# 3. Experiment B — Direct XLang IR → Native Machine Code with AsmJit

Prototype a lightweight native JIT backend using AsmJit.

Do **not** introduce LLVM for this prototype.

The architecture should be:

```text
XLang IR
     |
Native Lowering
     |
NativeCodeGen abstraction
     |
AsmJit backend
     |
native machine code
     |
execute directly
```

AsmJit should be statically linked or embedded into XLang3 where practical so normal distribution does not require a separate AsmJit DLL/shared-library dependency.

---

# 4. Keep AsmJit Isolated

Do not expose AsmJit APIs throughout the XLang3 IR implementation.

Create an abstraction conceptually similar to:

```cpp
class NativeCodeGen {
public:
    virtual NativeFunction compile(const IRFunction& function) = 0;
};
```

Then:

```text
NativeCodeGen
      ^
AsmJitCodeGen
```

or an equivalent architecture appropriate for the existing repository.

This should make it possible later to support:

```text
AsmJit
LLVM
custom assembler
ARM64 backend
other native backends
```

without redesigning the IR.

---

# 5. Initial Native IR Subset

Start very small.

Support only primitive/typed operations that can map naturally to machine instructions.

Examples:

```text
ADD_I64
SUB_I64
MUL_I64
DIV_I64      if straightforward

ADD_F64
SUB_F64

integer comparisons
floating-point comparisons

conditional branch
unconditional jump

return
```

Use the actual equivalent XLang3 IR opcodes/types discovered in the repository rather than inventing these names unnecessarily.

---

# 6. Compile Functions or Basic Blocks

Do **not** generate one native function per IR instruction.

This:

```text
IR instruction
    |
native function
    |
return to interpreter
    |
next IR instruction
```

would preserve too much interpreter overhead.

Instead compile at:

- basic-block granularity, or preferably
- function granularity

when practical.

For example:

```text
r3 = ADD_I64 r1, r2
r4 = MUL_I64 r3, r5
r6 = SUB_I64 r4, r7
RETURN r6
```

should ideally lower to one continuous native sequence conceptually similar to:

```asm
mov ...
add ...
imul ...
sub ...
ret
```

without returning to the IR executor between operations.

---

# 7. Runtime Helper Calls

Do not attempt to reimplement complex XLang semantics directly in generated assembly.

Use a hybrid strategy:

```text
primitive typed operation
        |
native instruction(s)

dynamic/object operation
        |
existing XLang C++ runtime helper
```

Generated code should be able to call existing runtime functions.

For example:

```text
primitive I64 ADD
      |
ADD machine instruction

dynamic ADD
      |
call XLangRuntime_Add(...)
```

This allows native compilation to grow incrementally without duplicating the runtime.

---

# 8. Initial Register Strategy

Do not over-engineer register allocation for the first prototype.

A simple implementation using:

- stack slots
- fixed registers
- straightforward loads/stores

is acceptable.

The first goal is:

```text
correct native execution
+
measurable reduction in interpreter overhead
```

After correctness is established, evaluate AsmJit's higher-level Compiler/register-allocation facilities.

Compare whether moving from:

```text
stack-based/simple lowering
```

to:

```text
AsmJit register allocation
```

provides a meaningful performance improvement.

---

# 9. JIT Eligibility and Fallback

The JIT prototype should not require every XLang IR instruction to be compilable.

Use a model such as:

```text
IR function
    |
JIT eligibility analysis
    |
+---------------------+
| supported subset?   |
+----------+----------+
           |
      yes  |  no
           |
           v
     AsmJit compile      existing executor
```

Alternatively, supported basic blocks may be compiled while unsupported paths fall back to runtime helpers/interpreter execution.

Keep the first implementation simple.

Correctness is more important than coverage.

---

# 10. Comparable Benchmark

Create comparable benchmarks for:

```text
A. Current switch-based IR executor

B. Prepared direct handler-pointer executor

C. AsmJit native execution
```

Use the same logical workload wherever possible.

Arithmetic-heavy loops and simple functions are preferred because interpreter overhead can be isolated.

## Required Measurements

Measure at least:

```text
execution time

IR instructions/sec

ns / IR instruction

speedup relative to current executor

native JIT compilation time

JIT code size

JIT break-even execution count

XLang3 binary size before AsmJit

XLang3 binary size after AsmJit

additional dependency/runtime size
```

For JIT break-even, estimate:

```text
T_interpreter(N)

versus

T_compile + T_native(N)
```

and determine approximately how many executions are required before:

```text
T_compile + T_native(N)
<
T_interpreter(N)
```

---

# 11. Hardware Performance Counters

If practical on the development platform, also collect hardware performance counters.

Useful metrics include:

```text
instructions retired

cycles

branches

branch misses

IPC

cache misses
```

The particularly interesting metric is:

```text
native CPU instructions retired
-------------------------------
XLang IR instructions executed
```

This provides an estimate of interpreter amplification.

For example, if:

```text
1 XLang IR ADD
```

requires:

```text
100+ native CPU instructions
```

in the interpreter but only a few instructions after JIT compilation, this directly explains the performance difference.

Use available platform tooling where practical rather than making the benchmark infrastructure overly complex.

---

# 12. Correctness

Every optimized execution path must be checked against the existing XLang3 executor.

For each benchmark/test:

```text
existing executor result
        ==
pointer-dispatch result
        ==
AsmJit result
```

Test:

- arithmetic
- comparisons
- branches
- loops
- function arguments
- return values
- edge cases for supported types

The existing executor remains the semantic reference implementation.

---

# 13. Implementation Order

Follow this order.

## Phase 1 — Inspect

Identify:

1. Current IR instruction representation.
2. Current executor and dispatch loop.
3. `X::Value` representation.
4. Operand access mechanism.
5. Existing typed/specialized operations.
6. Function/basic-block representation.
7. Build system.
8. Appropriate location for optional AsmJit integration.

Document findings before performing broad modifications.

## Phase 2 — Establish Baseline

Create the benchmark first.

Measure the existing executor.

This provides a stable baseline for all later comparisons.

## Phase 3 — Direct Dispatch

Implement:

```text
IR
 |
prepare/link
 |
ExecIR
 |
handler pointers
```

Benchmark against the baseline.

Profile the result.

Identify what becomes the dominant overhead after switch dispatch is removed.

## Phase 4 — Minimal AsmJit

Add the isolated AsmJit backend.

Compile a small primitive subset.

Start with something approximately equivalent to:

```text
integer add
integer subtract
integer multiply
return
```

Then add:

```text
comparison
branch
loop
floating point
```

as appropriate.

## Phase 5 — Compare

Produce comparable benchmark results for:

```text
Switch Interpreter
Pointer Interpreter
AsmJit JIT
```

## Phase 6 — Analyze

Determine the next optimization target.

Potential results might show that the main remaining cost is:

```text
X::Value

boxing/unboxing

operand lookup

reference management

dynamic type dispatch

runtime helper calls

memory layout

branch handling
```

Base the conclusion on profiling rather than assumptions.

---

# 14. Avoid Unrelated Refactoring

Keep this work experimental and narrowly scoped.

Do not:

- redesign XLang3 syntax
- redesign the IR
- introduce LLVM
- broadly rewrite the runtime
- redesign the object model
- replace `X::Value` as part of this task
- introduce a sophisticated optimizer
- build a production-quality register allocator
- attempt full IR coverage in AsmJit

Small supporting changes are acceptable where necessary to make the experiment measurable and correct.

---

# 15. Deliverables

Produce:

## A. Investigation Notes

Document:

```text
current IR layout
current execution loop
X::Value costs
typed operations
basic-block/function organization
build/dependency structure
```

## B. Baseline Benchmark

Provide reproducible benchmark commands and results.

## C. Direct-Dispatch Prototype

Include:

```text
prepared ExecIR representation
opcode → handler linking
execution loop
tests
benchmark results
```

## D. AsmJit Prototype

Include:

```text
AsmJit dependency integration
NativeCodeGen abstraction
AsmJit backend
supported IR operations
fallback behavior
tests
benchmark results
```

## E. Performance Report

Provide a final table approximately like:

| Executor | Time | IR Inst/s | ns/IR Inst | Speedup | Startup/JIT Cost |
|---|---:|---:|---:|---:|---:|
| Current switch | | | | 1.0x | 0 |
| Direct pointer | | | | | preparation cost |
| AsmJit native | | | | | compile cost |

Also report:

```text
AsmJit binary-size impact
JIT code size
JIT break-even point
hardware instructions / IR instruction, if available
```

## F. Recommendation

Conclude with evidence-based recommendations for the next step.

In particular answer:

1. How expensive is XLang3's current opcode dispatch?
2. How much does direct dispatch improve performance?
3. After direct dispatch, what is the largest remaining interpreter bottleneck?
4. How much faster is the minimal AsmJit JIT?
5. What is the JIT break-even point?
6. How much binary/dependency overhead does AsmJit add?
7. Is AsmJit small enough to reasonably ship as part of XLang3?
8. Which IR operations should be JIT-compiled next?
9. Is there enough performance headroom with this lightweight architecture to postpone or avoid LLVM for normal XLang3 execution?

The purpose is not to prove that AsmJit is the final XLang3 backend.

The purpose is to experimentally establish the performance spectrum:

```text
Switch Interpreter
        |
Prepared / Direct-Dispatch Interpreter
        |
AsmJit Native JIT
        |
[potential future optimizing backend such as LLVM]
```

and determine how far XLang3 can move toward native execution while keeping the runtime lightweight.

