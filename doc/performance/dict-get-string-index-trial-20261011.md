# Rejected string-only `dict.get` lookup fast path — 2026-10-11

## Result

Rejected. The target `typing_runtime_protocols` case measured 2.75 ms ±
0.07 ms on its same-day control and 2.79 ms ± 0.07 ms with the candidate.
`pyperf compare_to` hid the case as statistically insignificant. The
specialization was removed.

| Measurement | CPython 3.14.7 | XLang3 control | XLang3 candidate |
|---|---:|---:|---:|
| `dict.get` primitive, 20,000 calls | 1.284 ms | 20.721 ms | 20.013 ms |
| `typing_runtime_protocols` | 0.128 ms | 2.75 ms | 2.79 ms |

The primitive probe's roughly 3% `dict.get` change did not carry through to
the official workload. Both the probe and pyperf include VM call/loop costs;
the data does not show that string hashing or dictionary lookup is a major
share of the protocol benchmark.

## Change tested

The trial added a string-index lookup inside `mapping_get_item_runtime` when
the dictionary index proved there were no non-string keys. Mixed-key tables
kept the existing hash/equality path because a custom key can compare equal to
`str`. The added fixture checked both the string-only hit/miss and a custom
non-string key whose equality matches a string. Those semantics passed in the
focused fixture run, but the optimization did not improve the target benchmark
and was removed.

## Artifacts and limitations

- Control: [`typing-runtime-protocols-lru-fastpath-control-20261011.json`](data/typing-runtime-protocols-lru-fastpath-control-20261011.json), same fixed Release executable and source state used immediately before this trial.
- Candidate: [`typing-runtime-protocols-dict-string-fastpath-candidate-20261011.json`](data/typing-runtime-protocols-dict-string-fastpath-candidate-20261011.json).
- Restored runtime executable SHA-256: `6633C0A520DABCECA4DA4046F78243569045FFDEFA51160C3883A4CB0C6DF590`.
- The candidate runtime DLL hash was not captured before rebuilding the restored source; do not use this trial as a binary-reproducible comparison.
- Fixed Release path: `build-repro/main-verify-20261006/Release`.
