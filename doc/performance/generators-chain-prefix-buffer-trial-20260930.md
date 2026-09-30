# Generator delegation-chain scratch-buffer initialization (2026-09-30)

`try_generator_delegation_trampoline` used value-initialization for a fixed
200-pointer local chain buffer. Each scan iteration writes `chain[chain_size]`
before incrementing `chain_size`, and all later reads are bounded by that
count. The array does not escape the function. Default-initializing the array
therefore preserves the existing behavior while avoiding initialization of
unused slots on every resume. The write-before-read invariant and reason for
the optimization are documented beside the declaration in
[`generator.cpp`](../../src/runtime/generator.cpp#L264).

This is an XLang3 VM/runtime optimization. It does not replace or implement
Python standard-library code in C++; pure-Python library modules remain
Python, with native XLang3 modules reserved for modules CPython itself
implements natively.

## Measurements

The official pyperformance 1.14.0 `generators --fast` benchmark was run twice
against the immediate pre-change executable, alternating which runtime ran
first:

| Pair | Control | Candidate | Candidate speedup |
| --- | ---: | ---: | ---: |
| 1 | 361 ±6 ms | 349 ±4 ms | 1.03× |
| 2 | 355 ±4 ms | 352 ±4 ms | 1.01× |
| Combined pyperf runs | 358 ±6 ms | 350 ±4 ms | 1.02× |

`pyperf compare_to --verbose` reports the combined result as statistically
significant (`t=6.95`). Each individual fast run warned that it did not collect
enough samples to establish a stable result at the 1% variation target, so
this is a small, measured improvement rather than a claim of a large gain.
The fixed-baseline Release gate passed all 11 cases, and the complete Python
fixture suite passed.

The saved CPython 3.14.7 full-fast measurement for `generators` is **27.008
ms**. At about **350 ms**, XLang3 remains roughly **13.0× slower** on this
benchmark. This local improvement does not close the CPython gap; generator
execution remains an active optimization target.

## Evidence

- Raw pyperf runs: [control pair 1](data/generators-yieldfrom-chain-prefix-control-fast-20260930.json), [candidate pair 1](data/generators-yieldfrom-chain-prefix-candidate-fast-20260930.json), [control pair 2](data/generators-yieldfrom-chain-prefix-control-repeat-fast-20260930.json), and [candidate pair 2](data/generators-yieldfrom-chain-prefix-candidate-repeat-fast-20260930.json)
- Combined pyperf runs: [control](data/generators-yieldfrom-chain-prefix-control-merged-fast-20260930.json) and [candidate](data/generators-yieldfrom-chain-prefix-candidate-merged-fast-20260930.json)
- Fixed-baseline Release gate: [11-case report](data/generators-chain-prefix-fixed-baseline-20260930.json)
- CPython 3.14.7 reference: [saved full-fast results](data/pyperformance-cpython314-full-fast-20260928.json)

The control runtime DLL SHA-256 was
`F20F99A176EA258B8849670A083F548801C3987FB0B394F123AC1E30AD37BA32`; the
candidate runtime DLL SHA-256 was
`17244BF749C172422F79C7AE1C705CF2A6B6A053239570C178C5C839E02E4D5F`.
The launcher executable was identical for both runs.
