# Constant multiply IR trial for comprehensions (2026-10-06)

The `comprehensions` benchmark repeatedly evaluates `x * 3`. Unlike `% 3`,
which already has a constant-operand opcode, multiplication emitted a separate
constant load and general multiply. I tested a `MulConst` IR instruction that
kept the literal in the constant pool while retaining ordinary and reflected
Python multiplication for values that miss the numeric fast path.

The Release candidate built successfully. The added semantic fixture matched
CPython 3.14.7 for positive and negative integers, floats, bigint promotion,
user-defined `__mul__`, and an `int` subclass override. The full fixture suite
passed.

The official pyperformance 1.14.0 fast-mode result was not significant:

| Build | `comprehensions` |
| --- | ---: |
| Fixed Release control | 169 µs ± 2 µs |
| Candidate | 172 µs ± 14 µs |

`pyperf compare_to` hid the comparison as insignificant. This opcode is not
retained because removing a constant-load dispatch did not measurably improve
the benchmark. XLang3 remains roughly 12× slower than CPython 3.14.7 on this
workload; the larger cost is elsewhere in comprehension execution.

Raw data: [control](data/pyperformance-mul-const-comprehensions-control-fast-20261006.json),
[candidate](data/pyperformance-mul-const-comprehensions-candidate-fast-20261006.json).
