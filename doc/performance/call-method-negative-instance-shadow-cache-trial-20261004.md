# CALL_METHOD negative instance-shadow cache trial (2026-10-04)

## Result

Rejected. Skipping the repeated per-instance attribute scan behind a receiver
identity and instance-shape guard did not produce a pyperformance win on its
intended workloads. The candidate measured 1% slower by pyperf's geometric
mean; only `logging_silent` was statistically distinguishable, at 4% slower.
The cache and its shape-version bookkeeping were removed after this comparison.

## Method

The parent binary was preserved before the cache change, and both executables
used the same Release configuration, Python 3.14 benchmark dependencies, and
pyperformance 1.14 fast settings. The parent ran first, followed by the
candidate. Fast-mode samples were directional and pyperf flagged several as
unstable, so the small differences should not be treated as precise estimates.

| Benchmark | Parent | Candidate | Direction |
|---|---:|---:|---:|
| `argparse_subparsers` (`subparsers`) | 152 ms ± 3 ms | 153 ms ± 9 ms | No significant change |
| `logging_silent` | 1.16 µs ± 0.01 µs | 1.20 µs ± 0.07 µs | 1.04× slower (significant in `pyperf compare_to`) |
| `logging_format` | 94.6 µs ± 1.7 µs | 94.6 µs ± 1.8 µs | No significant change |
| `logging_simple` | 90.1 µs ± 12.4 µs | 89.5 µs ± 9.2 µs | No significant change |

`pyperf compare_to --table` reported a 1.01× slower geometric mean and hid the
three changes it judged insignificant. The Pickler-writer paired probe also
regressed slightly (candidate/control 1.0185×; 95% interval 1.0050–1.0264).
Together, these results do not justify the extra guard or per-instance shape
tracking.

## Raw data and verification

- Parent pyperformance JSON:
  [`call-method-negative-shadow-parent-fast-20261004.json`](data/call-method-negative-shadow-parent-fast-20261004.json)
- Candidate pyperformance JSON:
  [`call-method-negative-shadow-candidate-fast-20261004.json`](data/call-method-negative-shadow-candidate-fast-20261004.json)
- Pickler-writer paired data:
  [`call-method-negative-shadow-pickle-writer-ab-20261004.json`](data/call-method-negative-shadow-pickle-writer-ab-20261004.json)

After removing the candidate cache, the Release build succeeded at
`build-repro/Release`; `xlang3_interpreter_tests.exe` passed, as did the
`call_method_negative_instance_shadow_cache` and `load_attr_cache_precedence`
fixtures. The first fixture remains useful as a behavioral regression test for
instance overrides, deletion, `__dict__`, object churn, and class mutation,
independent of this rejected optimization.
