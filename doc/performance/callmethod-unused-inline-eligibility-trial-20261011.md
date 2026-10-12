# CallMethod unused inline-eligibility check trial (2026-10-11)

## Hypothesis

`call_method` computed `inline_calls_allowed(runtime)` once near entry, but the
result had no use in that function. The function performs runtime observer and
monitoring queries, so removing a truly executed dead check could reduce cost
on methods that use the fused `CallMethod` opcode.

## Paired test

Using the fixed Release executable path and CPython 3.14.7 / pyperformance
1.14.0, I ran the same two official benchmarks in `--fast` mode immediately
before and after removing the unused expression. The runner reported both runs
unstable.

| Benchmark | Before | After | Direction |
| --- | ---: | ---: | ---: |
| `richards` | 334 ms ± 5 ms | 340 ms ± 5 ms | 1.02× slower |
| `typing_runtime_protocols` | 2.71 ms ± 0.06 ms | 2.83 ms ± 0.14 ms | 1.04× slower |
| Geometric mean | — | — | 1.03× slower |

`pyperf compare_to` found no gain. Because the result is small and both runs
are noisy, it is not evidence of a real regression either. The source edit was
rejected and restored; it is not part of the retained optimization set.

## Artifacts and interpretation

- Before: [`callmethod-unused-inline-eligibility-before-20261011.pyperf.json`](data/callmethod-unused-inline-eligibility-before-20261011.pyperf.json)
- After: [`callmethod-unused-inline-eligibility-after-20261011.pyperf.json`](data/callmethod-unused-inline-eligibility-after-20261011.pyperf.json)
- The experiment also requested `scimark_lu`, but pyperformance did not
  recognize that name. It ran only the two listed benchmarks.

The next target should remove measurable interpreter work from a demonstrated
hot path, not merely simplify source that the optimizer may already eliminate.
