# Borrowed coefficient in native Decimal quantize (2026-10-02)

## Result

Rejected. Borrowing the immutable coefficient avoided one temporary string
copy in `_decimal`'s exact-`Decimal` quantize fast path, while retaining an
owning `Value` across context callbacks. The fixture and the complete fixed
Release gate passed, but the official pyperformance `telco` case showed no
significant improvement in a same-source A/B comparison. The runtime change
was removed.

| Official pyperformance 1.14.0 `telco` | Mean | Standard deviation |
|---|---:|---:|
| Control | 256 ms | 11 ms |
| Candidate | 256 ms | 15 ms |

`pyperf compare_to` hid the difference as statistically insignificant. A
separate fast-mode candidate sample measured 259 ms ± 17 ms and was also too
noisy to establish a gain. The copy is therefore not a material part of this
workload's remaining cost.

The candidate passed the existing quantize correctness fixture and the
complete 11-case Release gate against `baseline-0336992`; its complete report
is [here](data/decimal-quantize-coeff-view-fixed-baseline-20261002.json). The
gate validates the candidate's lack of local regressions but does not change
the rejection based on the official workload result.

## Reproduction evidence

- [Control rigorous pyperf JSON](data/telco-decimal-quantize-coeff-view-control-rigorous-20261002.json)
- [Candidate rigorous pyperf JSON](data/telco-decimal-quantize-coeff-view-candidate-rigorous-20261002.json)
- [Candidate fast pyperf JSON](data/telco-decimal-quantize-coeff-view-official-fast-20261002.json)
- [Candidate Release artifacts](../../scratch/performance/decimal-quantize-coeff-view/candidate)
- [Control Release artifacts](../../scratch/performance/decimal-quantize-coeff-view/control)

The candidate and control differ only in the quantize coefficient-copy
implementation. `decimal_native_quantize.py` passed on the candidate. No
engine change from this trial remains in the source tree.
