# Constant subscript fusion trial (2026-09-30)

XLang3 now fuses a constant index load with the following `GetItem` into one
IR dispatch. The fused handler writes the same constant into its original
temporary register, then delegates to the ordinary `GetItem` implementation.
That retains exact built-in checks, custom `__getitem__` dispatch, errors, and
frame switching while avoiding one VM loop iteration. The change stays in
generic lowering and the VM; it does not move Python library code into C++.

This shape appears in the pure-Python pickle loop after the paired local load:
`LoadLocalPair; LoadConst(0); GetItem(key, index)`. The lowerer now emits
`LoadLocalPair; GetItemConst` for the constant-index operation. The following
`GetItem(dispatch, key[0])` and Python function call keep their existing
semantics and handlers.

## Official pyperformance result

The unchanged pyperformance 1.14.0 `bm_pickle/run_benchmark.py` workload ran
with pyperf 2.10.0, CPython 3.14.7 as the manager, and protocol 5's
`unpickle_pure_python` case. Candidate and parent each ran once in both orders
with `--rigorous`:

| Pair order | Parent | Fused candidate | Candidate result |
|---|---:|---:|---:|
| Parent, then candidate | 3.39 ± 0.29 ms | 3.31 ± 0.24 ms | 1.03× faster (`t=2.41`) |
| Candidate, then parent | 3.47 ± 0.37 ms | 3.31 ± 0.28 ms | 1.05× faster (`t=3.84`) |

All four XLang3 runs reported host jitter above pyperf's 1% stability target.
The candidate means agree across orders, while the parent varied by 2.4%; both
order-specific comparisons were statistically significant. The two pair
means correspond to about **1.04× faster** elapsed time for the candidate,
which is a small, targeted VM improvement rather than a resolution of the
interpreter gap.

The fresh CPython 3.14.7 run measured 180 ± 19 μs and also warned of 10%
variation. Against that same-run reference, the fused XLang3 candidate takes
about 18.4× as long, or runs at **0.054× CPython's speed** when CPython is
1.00×. The broader goal remains open.

```text
unpickle_pure_python elapsed time (shorter is faster; one block ≈ 0.18 ms)
CPython 3.14.7       0.180 ms |█
XLang3 parent        3.43  ms |███████████████████
XLang3 fused         3.31  ms |██████████████████
```

The matched raw runs are [parent, first order](data/getitem-const-parent-rigorous-20260930.json),
[candidate, first order](data/getitem-const-candidate-rigorous-20260930.json),
[candidate, reverse order](data/getitem-const-candidate-repeat-rigorous-20260930.json),
[parent, reverse order](data/getitem-const-parent-repeat-rigorous-20260930.json),
and [CPython 3.14.7](data/getitem-const-cpython314-rigorous-20260930.json).
Each run's captured output is stored beside its JSON file.

| Runtime | Executable SHA-256 | Runtime SHA-256 |
|---|---|---|
| XLang3 parent | `2D3BD37943E0B077110567B12AB2338D021862CE602858B486E771BD3B3FA67B` | `0486C734D53EB04CDF1C4C5DFE2EB60A071B5859A35C239A04DC5A31B8EE6776` |
| XLang3 fused | `5CDE9F716D7464C8E304DCD5963DFE63CD59499128BB6FB837DC5F9E63C16362` | `0950F854D9BED91A8D3547882AB6B7B148141613A81DD67BFFD38CA1671D9AD6` |
| CPython 3.14.7 manager/worker | `4942B86A6597E5AEE0128DAA00050ED79BC21F6E709A78EB19CBFEB0C2F39AC9` | — |

## Correctness and regression checks

The IR codec test asserts that lowering emits the new op and round-trips it.
The new fixture checks both a built-in tuple result and a user-defined
`__getitem__` with observable evaluation order. The complete Python fixture
suite and the IR codec, interpreter, and runtime-value C++ tests passed. The
complete 11-case fixed Release regression gate also passed; its report is
[here](data/getitem-const-fixed-baseline-20260930.json).

The source comment beside the lowering fusion records why it exists and why
the normal `GetItem` path remains in charge of semantics. A matching comment
beside the handler explains why it preserves the removed constant temporary.
