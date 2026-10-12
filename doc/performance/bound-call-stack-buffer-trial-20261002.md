# Stack-backed arguments for nested bound-method calls (2026-10-02)

## Result

Rejected. Replacing the temporary heap vector in `runtime_call_callable` with
an owning stack buffer for bound calls of up to eight arguments produced only
small directional reductions. None of the three 21-pair, order-balanced
comparisons established a gain; every 95% interval includes parity.

| Workload | Control median | Candidate median | Candidate / control | 95% paired interval |
|---|---:|---:|---:|---:|
| Scaled asyncio tree (3 levels, 3 branches, 50 iterations) | 392.983 ms | 383.130 ms | 0.9801x | 0.9626–1.0045 |
| Pure-Python Pickler-shaped dumps | 48.154 ms | 47.771 ms | 0.9962x | 0.9845–1.0074 |
| `subparsers` | 269.314 ms | 266.560 ms | 0.9884x | 0.9851–1.0046 |

The implementation change was removed. Raw data is preserved in
[async-tree samples](data/bound-call-stack-async-tree-scaled-ab-20261002.json),
[Pickler samples](data/bound-call-stack-pickle-writer-ab-20261002.json), and
[`subparsers` samples](data/bound-call-stack-subparsers-ab-20261002.json).
The control includes the separately measured ownerless-cache cleanup change;
these comparisons isolate the stack-buffer trial.

The new `context_run_bound_callback` fixture remains in the core runner. It
checks bound Python callback arguments, ContextVar isolation, and restoration
after a callback raises. It passed together with the existing context-run and
asyncio fixtures and the C++ interpreter tests on the trial build.

This diagnostic did not run official pyperformance. It does not establish a
speedup or change the overall CPython comparison.
