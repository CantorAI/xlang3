# Direct `Call` exact-positional frame shortcut — 2026-10-04

## Hypothesis

The pyperformance `argparse_subparsers` profile reports substantial cost in the
VM `Call` handler. Like `CallMethod`, a warmed direct Python function call could
retain proof that positional arguments exactly fill a fixed signature and skip
the generic signature scan at frame entry.

## Result: rejected and removed

The 21-pair local `subparsers` workload improved 2.2% (candidate/control
0.9783×, 95% CI 0.9691–0.9830). A 250,000-call `function_calls` workload
regressed 1.7% (1.0170×, 95% CI 1.0140–1.0273). Most importantly, official
pyperformance 1.14.0 `argparse_subparsers` measured 143 ± 2 ms on the candidate
and 143 ± 1 ms on the control. The official benchmark showed no improvement,
so the generic `Call` specialization and its temporary fixture were removed.

The measured local subparsers gain did not transfer to the official benchmark.
Do not add this specialization again without a new mechanism that improves the
official case and does not regress direct-call workloads.

## Evidence

- Local subparsers pair: `data/call-exact-positional-cache-subparsers-order-balanced-20261004.json`.
- Direct-call pair: `data/call-exact-positional-cache-function-calls-order-balanced-20261004.json`.
- Official candidate: `data/pyperformance-xlang3-call-exact-positional-cache-subparsers-candidate-rigorous-20261004.json`.
- Official control: `data/pyperformance-xlang3-call-exact-positional-cache-subparsers-control-rigorous-20261004.json`.

Both official runs used Python 3.14.7, pyperformance 1.14.0, and the same
dependency site. This trial did not modify the fixed Release baseline.
