# Program IR Spec

Status: draft 0

## Purpose

ProgramIR is the normal executable representation for Python-style code.

It represents:

- functions
- modules
- basic blocks
- control flow
- exception flow
- local/global/free variable slots
- operations over `X3Value`

ProgramIR is consumed by:

- interpreter executor
- LLVM JIT
- LLVM AOT/native compiler
- verifier
- optimizer
- debugger/source mapper

## Not A Python Bytecode Clone

ProgramIR should not copy CPython bytecode. It should be designed around:

- `X3Value`
- XLang refcount rules
- C ABI type slots
- inline caches
- future JIT/AOT lowering

## Top Level

```text
IRModule
  name
  source_files
  constants
  globals
  functions
  classes metadata
  imports metadata
```

```text
IRFunction
  name
  flags
  arg_count
  kwonly_count
  local_count
  register_count
  constants
  blocks
  exception_regions
  debug_map
```

```text
IRBasicBlock
  id
  instructions
  successors
  predecessors
```

## Registers And Slots

Use virtual registers for expression temporaries.

Use slots for locals/cells/free vars/globals.

```text
local slot: fast frame-local variable
cell slot: captured variable owned by closure cell
free slot: variable captured from outer function
global slot/cache: module/global lookup
```

Current XLang AST-bound `Scope* + Index` should become sema-bound `ScopeId + SlotIndex`, lowered to direct IR operands.

## Instruction Format

Initial simple form:

```c
typedef struct X3IRInstr {
    uint16_t op;
    uint16_t flags;
    uint32_t dst;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t imm;
    uint32_t cache_index;
    uint32_t source_span;
} X3IRInstr;
```

Specific instructions may use side tables for variable-size operands.

## Instruction Families

### Load/Store

```text
LoadConst dst, const_id
LoadNone dst
LoadBool dst, value
LoadLocal dst, slot
StoreLocal slot, src
LoadCell dst, slot
StoreCell slot, src
LoadFree dst, slot
LoadGlobal dst, name_id/cache
StoreGlobal name_id/cache, src
LoadAttr dst, obj, name_id/cache
StoreAttr obj, name_id/cache, value
LoadItem dst, obj, key/cache
StoreItem obj, key/cache, value
```

### Object Construction

```text
MakeTuple dst, values...
MakeList dst, values...
MakeDict dst, pairs...
MakeSet dst, values...
MakeSlice dst, start, stop, step
MakeFunction dst, function_id, defaults, closure
MakeClass dst, ...
```

### Ops

```text
UnaryOp dst, op, src
BinaryOp dst, op, left, right
CompareOp dst, op, left, right
BoolAnd/BoolOr via control flow, not eager binary op
Is dst, left, right
In dst, left, right
```

### Calls

```text
Call dst, callee, arg_base, argc
CallKw dst, callee, arg_base, argc, kw_table
LoadMethod dst_method, dst_self, obj, name_id/cache
CallMethod dst, method, self, arg_base, argc
```

### Control

```text
Jump block
JumpIfTrue cond, block_true, block_false
JumpIfFalse cond, block_false, block_true
Return src
Raise src
Reraise
```

### Iteration

```text
GetIter dst, iterable/cache
IterNext dst, iterator, block_next, block_done/cache
```

Specialized forms may exist:

```text
RangeIterInit
RangeIterNext
ListIterNext
ArrayIterNext
```

### Exception

Phase 0 may use simple runtime exception checks.

Later IR should model exception edges:

```text
TryBegin region
TryEnd region
ExceptionMatch
FinallyEnter
FinallyExit
```

## Inline Cache Slots

Every dynamic instruction may reference an inline cache entry:

```text
LoadAttr
StoreAttr
LoadGlobal
Call
CallMethod
BinaryOp
CompareOp
LoadItem
StoreItem
IterNext
```

IR does not define cache layout. Executor owns cache layout.

## Verification

Verifier checks:

- register ids valid
- slot ids valid
- block targets valid
- terminators end blocks
- exception regions valid
- refcount ownership constraints if encoded
- no AST pointers

## Lowering Rules

AST:

```python
x = a + b * c
```

ProgramIR:

```text
r0 = LoadLocal slot(a)
r1 = LoadLocal slot(b)
r2 = LoadLocal slot(c)
r3 = BinaryOp Mul r1 r2
r4 = BinaryOp Add r0 r3
StoreLocal slot(x) r4
```

