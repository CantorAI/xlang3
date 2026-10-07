# Native factorial owned-limb checkpoint — 2026-10-07

XLang3 now computes native factorial on a private limb accumulator, batches factors into 32-bit products, and publishes one final immutable integer. It retains argument/index conversion and adds CPython’s platform C-long upper bound before computation. Small results through 20! use an exact table.

This changes the native `math.factorial` counterpart only. CPython implements that function in C. Python asyncio scheduling and other Python library algorithms remain in Python; generic bigint arithmetic is unchanged.

## Official benchmark evidence

| Benchmark | Previous XLang3 | New XLang3 | CPython 3.14.7 | Previous / new | CP / new |
| --- | ---: | ---: | ---: | ---: | ---: |
| `async_tree_cpu_io_mixed` | Timed out (300 s case cap) | 5503 ms | 453.4 ms | No previous score | 0.0824× |
| `async_tree_eager_cpu_io_mixed` | 7040 ms | 3083 ms | 357.6 ms | 2.2836× | 0.1160× |

Both targeted definitions completed in official pyperformance fast mode at the unchanged 300-second cap. The prior non-eager timeout has no speed ratio. The eager before/after measurements are separate fast runs, not a paired statistical experiment. Both workloads remain slower than CPython; task scheduling and interpreted work remain material costs.

## Diagnostic arithmetic evidence

Five diagnostic samples per input use the same probe and loop counts before/after. These medians include Python loop/call overhead and are not official suite scores.

| n | XLang3 before / call | XLang3 after / call | CPython / call | Before / after | CP / after |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 191 ns | 209.7 ns | 19 ns | 0.911× | 0.091× |
| 20 | 337.5 ns | 207.7 ns | 79.5 ns | 1.625× | 0.383× |
| 100 | 17.62 µs | 582.5 ns | 496.5 ns | 30.242× | 0.852× |
| 500 | 157.6 µs | 4.401 µs | 6.38 µs | 35.804× | 1.450× |
| 1000 | 432.2 µs | 19.21 µs | 22.99 µs | 22.500× | 1.197× |
| 5000 | 8.9 ms | 1.086 ms | 501 µs | 8.196× | 0.461× |

## Correctness, design constraints and fixed baseline

- The new fixture checks an independent Python recurrence through 100 and selected inputs through 5000, including small-integer and chunk/growth boundaries, retained results, index invocation, int-subclass arithmetic overrides, bools, invalid argument types, negative values and platform overflow. `perm(n)` and `perm(n, None)` use the same range and result behavior.
- The bounded pre-change Windows probe observed CPython reject `2**31` immediately while the preserved XLang3 child exceeded two seconds and was terminated. The corrected native function rejects it before entering arithmetic.
- The complete fixture suite and all eight C++/SDK/graph tests passed. The initial fixture run failed because its controller set a bytecode cache prefix contrary to a fixture expectation; that failure is retained separately, and the corrected environment rerun passed without an engine change.
- The complete fixed accepted Release gate passed all 11 cases, with default 21 paired repeats, five warmups, 10% tolerance and exit 0. The original baseline was not changed.
- The pre-change Release and native packages remain at `build-repro/controls/super-method-call-checkpoint-20261007`; the candidate executable path remains `build-repro/main-verify-20261006/Release/xlang3.exe`.
- Performance comments in the native module explain why the accumulator is private, why per-step Value publication is unnecessary, how batching avoids repeated limb traversals, and the little-endian import contract. The 64-bit-C-long fallback preserves wider accepted inputs on those platforms.

The native-source proof records both raw and canonical-LF SHA-256 because Git normalizes C++ source line endings. Binary and raw benchmark evidence hashes identify the exact measured build and archived results.

## Evidence

[Checkpoint provenance](data/factorial-owned-limbs-checkpoint-20261007.json) records exact raw inputs, native source and executable/library hashes. The previous all-97 report remains a historical snapshot; these two targeted results are not spliced into that dataset.

[CPython 3.14.7 native factorial and platform range](https://github.com/python/cpython/blob/v3.14.7/Modules/mathmodule.c#L1885).
