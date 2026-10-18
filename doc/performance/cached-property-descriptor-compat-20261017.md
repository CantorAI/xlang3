# Cached-property descriptor compatibility regression gate

The xMind-required cached-property inline-lookup fix at `b4a53192` was
compared against the preserved main Release runtime at `fcecf499`. The complete
fixed 11-case Release regression gate ran through the candidate xlang3 CLI
itself, with 21 order-balanced repeats, five warmups and a 10% limit. All 11
cases passed; candidate/baseline ratios ranged from 0.982x to 1.023x. No
CPython interpreter was run.

The baseline executable and its runtime DLL were copied from
`build/Release`; the candidate executable and runtime DLL were copied from
`build-pytest-pathaware/Release`. Both copies and the per-case measurements
are captured in the [raw gate report](data/cached-property-descriptor-compat-regression-gate-20261017.json).
This gate covers the repository's fixed regression workload. It is not a
claim of a whole-program performance improvement.
