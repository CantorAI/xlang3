# Generator chain eligibility trial — 2026-10-06

## Question

The official pyperformance `generators` benchmark was about 10.7× slower than
CPython 3.14.7 in the current full-fast run. The synchronous `yield from`
trampoline checked runtime-wide trace, profile, and monitoring state again for
each generator in a delegated chain, even though those checks are common to
the root and its children. This trial checked whether moving those repeated
checks out of the chain walk reduced the cost of each yielded item.

## Result

The change did not produce a measurable improvement. The rigorous control run
measured **322 ± 25 ms**; the candidate measured **320 ± 59 ms**. `pyperf
compare_to` hid the benchmark because the difference was not significant.
The candidate's larger variation makes the two-millisecond difference
particularly weak evidence, so the code change was removed.

The full 11-case Release regression gate passed all cases. Fixtures and both
Release C++ test executables passed. This gate confirms that the trial did not
cause a broad slowdown; it does not turn the insignificant generator result
into a win.

| Artifact | SHA-256 |
| --- | --- |
| Control executable | `7FB9FD272F16D0374596DA1149F61226F3011BB2EC80539F70900CDBB68B357D` |
| Control runtime DLL | `CCC5EC8DDA34F0686799FCBEF0080907B30F27D79F8E44D0C28B9A9EE4C33259` |
| Candidate runtime DLL | `E2B842D3CDC0EC4CB262F48CC7E79C01BAEA3AB47C9CD5017BFBD923847EB7CC` |

The fixed Release path was `build-repro/main-verify-20261006/Release/xlang3.exe`.
After measurement, the control executable and DLL were restored there and their
hashes verified. The candidate pair is retained under
`scratch/performance-baselines/generator-chain-eligibility-20261006/` for
reproduction; it is not the active run binary.

## Evidence

- [Official pyperf control](data/generators-resume-runtime-control-rigorous-20261006.json)
- [Official pyperf candidate](data/generators-resume-runtime-candidate-rigorous-20261006.json)
- [11-case Release regression gate](data/generator-chain-eligibility-fixed-gate-20261006.json)

No source optimization from this trial remains. The generator benchmark
remains a high-priority gap; the next experiment needs to remove repeated
resume/frame work, not only the redundant observability checks.
