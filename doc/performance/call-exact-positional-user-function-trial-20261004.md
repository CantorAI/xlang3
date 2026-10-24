# Exact-positional generic `Call` cache trial — 2026-10-04

## Decision

Rejected. A warmed generic `Call` site that repeatedly invokes a Python
function with an exact positional signature can skip the argument binder after
the first validated call. Despite that plausible reduction, the end-to-end
pyperformance results show no speedup. The path, its temporary fixture, and
its call-site cache specialization were removed.

Merged source-matched pyperf results found no significant change in five of
the six measured subtests. `subparsers` was the sole significant result and
was **1% slower** in the candidate. The candidate geometric mean was **1.01×
slower** than control; this is too small to explain the call-dispatch cost and
does not justify retaining another guarded branch.

## Hypothesis and guard

The `Call` handler already cached monomorphic Python-function targets, but
still ran `call_user_function` and generic argument binding on every hit. The
trial reused `CallSiteKind::ExactPositionalFunction` for a no-keyword,
no-expansion call whose current IR target had exactly the supplied number of
positional-only or positional-or-keyword parameters. It skipped binding while
keeping the standard frame push, recursion checks, tracing, and register
liveness transfer. Function identity and exact arity guarded cache reuse;
dynamic targets and calls with defaults omitted or expansions kept generic
binding.

The hypothesis was that even a short binder pass was measurable across the
many Python calls in deepcopy, pickle, and argparse. The unchanged total case
times show that binding is not the dominant cost on these paths. The next
investigation should measure call-handler and frame-switch stages directly,
not add more signature-specific binder shortcuts.

## Measurements

All four runs used pyperformance **1.14.0**, CPython **3.14.7** dependencies,
and the same fixed XLang3 Release executable path. Run order was
control-candidate, then candidate-control.

| Benchmark | Control 1 | Candidate 1 | Candidate 2 | Control 2 |
|---|---:|---:|---:|---:|
| `subparsers` | 144 ms ± 1 ms | 146 ms ± 2 ms | 145 ms ± 1 ms | 146 ms ± 2 ms |
| `async_tree_eager` | 2.83 s ± 0.03 s | 2.84 s ± 0.03 s | 2.84 s ± 0.04 s | 2.86 s ± 0.04 s |
| `deepcopy` | 2.87 ms ± 0.22 ms | 2.86 ms ± 0.16 ms | 2.83 ms ± 0.04 ms | 2.88 ms ± 0.19 ms |
| `deepcopy_reduce` | 29.8 µs ± 0.3 µs | 30.4 µs ± 3.0 µs | 30.4 µs ± 3.0 µs | 29.8 µs ± 0.2 µs |
| `deepcopy_memo` | 310 µs ± 5 µs | 310 µs ± 5 µs | 313 µs ± 5 µs | 314 µs ± 3 µs |
| `pickle_pure_python` | 5.22 ms ± 0.04 ms | 5.36 ms ± 0.45 ms | 5.35 ms ± 0.38 ms | 5.24 ms ± 0.05 ms |

On the merged files, `pyperf compare_to` reports `subparsers` at **1.01×
slower** for the candidate. The other five subtests are not statistically
significant. The fixture for alternating targets and exact positional calls
matched CPython 3.14.7 before the candidate path was removed.

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Control | `78759AC13404C2A05ED26F2ADA691E094BF7944C5C58952FC4635DD4116171AC` | `AA32D00FEBF2AC72666026D58DF544F6D649F9C2C14009227A90098D51B5EE61` |
| Candidate | `78759AC13404C2A05ED26F2ADA691E094BF7944C5C58952FC4635DD4116171AC` | `898B455444FC42BD31F0CD371431BEB8B39B5A095586222C56A190E09E324AC5` |

The four raw runs, worker logs, and merged pyperf files are preserved:

- [Control run 1 JSON](data/call-exact-positional-user-function-control-r1.json) and [log](data/call-exact-positional-user-function-control-r1.log)
- [Candidate run 1 JSON](data/call-exact-positional-user-function-candidate-r1.json) and [log](data/call-exact-positional-user-function-candidate-r1.log)
- [Candidate run 2 JSON](data/call-exact-positional-user-function-candidate-r2.json) and [log](data/call-exact-positional-user-function-candidate-r2.log)
- [Control run 2 JSON](data/call-exact-positional-user-function-control-r2.json) and [log](data/call-exact-positional-user-function-control-r2.log)
- [Merged control JSON](data/call-exact-positional-user-function-control-merged.json) and [merged candidate JSON](data/call-exact-positional-user-function-candidate-merged.json)

This trial changes no Python standard-library source and leaves the existing
`CallMethod` exact-positional optimization unchanged.
