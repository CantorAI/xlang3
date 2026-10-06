# Warm `GetItem` probe-order trial (2026-10-06)

## Result

Moving the warmed exact-sequence integer-subscript guard before generic dict
shape probes did not improve either profiled target. `pyperf compare_to` found
no statistically significant change for SQLGlot or argparse subparsers. The
candidate was removed, and the saved Release executable and runtime DLL were
restored at the established path.

| Benchmark | Control | Candidate | Comparison |
| --- | ---: | ---: | --- |
| `sqlglot_v2_parse` | 21.2 ± 1.4 ms | 21.4 ± 1.1 ms | Not significant |
| `subparsers` | 146.98 ms | 152 ± 17 ms | Not significant |

The rigorous SQLGlot control and candidate both carried stability warnings.
The fast subparsers candidate was also noisy. Neither difference is evidence
of a speedup or regression. The remaining gaps against CPython 3.14.7 are still
large: the saved full-run references are 1.01 ms for SQLGlot and 8.151 ms for
subparsers.

## Hypothesis

The VM already specializes hot list, tuple, string, bytes, and bytearray
integer indexing. Before this trial, every `GetItem` checked exact dictionary
integer and string shapes before consulting the warmed sequence specialization.
The candidate moved only the specialized cache hit ahead of those generic
mapping probes; cold and unsupported operations kept their existing fallback.
This reduced work in the intended sequence-shaped case without changing
Python library code. The end-to-end results show that this guard ordering is
not a material bottleneck for these two workloads.

## Validation and artifacts

The candidate passed the full fixture suite, `xlang3_interpreter_tests.exe`,
and `xlang3_runtime_value_tests.exe`.

- SQLGlot control:
  [`pyperformance-sqlglot-hash-direct-call-control-rigorous-20261006.json`](data/pyperformance-sqlglot-hash-direct-call-control-rigorous-20261006.json)
- SQLGlot candidate:
  [`pyperformance-sqlglot-getitem-early-specialized-candidate-rigorous-20261006.json`](data/pyperformance-sqlglot-getitem-early-specialized-candidate-rigorous-20261006.json)
- Subparsers control:
  [`pyperformance-argparse-subparsers-current-fast-20261006.json`](data/pyperformance-argparse-subparsers-current-fast-20261006.json)
- Subparsers candidate:
  [`pyperformance-subparsers-getitem-early-specialized-candidate-fast-20261006.json`](data/pyperformance-subparsers-getitem-early-specialized-candidate-fast-20261006.json)

After the comparison, the candidate source edit was removed. The same-path
Release executable and runtime DLL were restored and verified at SHA-256
`FF66E309BED7F3226F52F59E06842F448992F31805D54685EA755365CCD2D389` and
`395BF96C94508A8E9E326D2C79C427489B729AC84CD244BACD4EDC037F331B1E`,
respectively. The optimization is not retained.
