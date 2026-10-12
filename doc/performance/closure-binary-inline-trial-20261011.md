# Guarded closure-call inlining trial — 2026-10-11

## Result

A cached inliner for a one-argument function whose complete body is one binary
operation over that argument and one captured scalar cell cut the measured
`global_closure_call` median from **16.384 ms to 3.966 ms** for 50,000 calls
(9 repeats), a **4.13× improvement**. CPython 3.14.7 measured **1.901 ms**;
XLang3 is now **2.09× slower** on this call shape. The loop-only control is
1.33× slower than CPython, so the remaining gap is mostly call execution, not
loop dispatch. This is a material local result; no full-suite gain is claimed
until pyperformance finishes.

The optimization recognizes only an exact fixed-positional signature, one
free cell, no cell variables, and a four-instruction expression plus implicit
return. It checks monitoring/trace eligibility, exact function identity and
code version, and reads the live closure cell at each call. It executes the
shortcut only for immediate scalar operands. Keywords, expanded arguments,
empty cells, custom arithmetic objects, tracing, code replacement, and every
other function body use ordinary execution. The VM design note and regression
coverage are in `src/executor/xlang_vm/xlang_vm_inline_call.h`,
`src/executor/xlang_vm/ops/xlang_vm_ops_call.h`, and
`tests/fixtures/core/closure_arg_binary_inline.py`.

## Validation

- Fixed Release build succeeded at the existing `build-repro/main-verify-20261006/Release` path.
- The new fixture produced byte-for-byte matching output under XLang3 and CPython 3.14.7. It covers live `nonlocal` cell changes, big-integer overflow, a reversed free-variable operand, non-scalar fallback, `__radd__`, empty-cell `NameError`, and `__code__` replacement/restoration.
- The targeted fixture runner also passed the existing `native_callback_slot_compare` and `dynamic_builtins_lookup` fixtures.
- The broader fixture sweep passed 443 cases when excluding the known `int_bytes_small_fastpaths` failure (`True.to_bytes` currently raises `AttributeError`). The ordinary full fixture command stops at that pre-existing failure.
- `xlang3_interpreter_tests.exe` passed.

## Call-shape measurements

The candidate measurements are sequential, with 50,000 calls per case and 9 repeats. Raw medians and all samples for the candidate, pre-cache XLang3 control, and CPython 3.14.7 are in
[`python-call-shapes-closure-binary-inline-20261011-r1.json`](data/python-call-shapes-closure-binary-inline-20261011-r1.json).
The immediately previous fixed Release candidate, before this closure inline,
measured the same call at 16.384 ms; its data is
[`python-call-shapes-builtin-comparator-cache-20261011-r1.json`](data/python-call-shapes-builtin-comparator-cache-20261011-r1.json).
The earlier control/candidate comparison is also available in
[`native-callback-slot-double-compare-ab-20261011-r1.md`](native-callback-slot-double-compare-ab-20261011-r1.md).

The candidate executable SHA-256 is
`e42d357f75850a54f7a5eefb25d0f3c979077d6b89e797b66451a2f43cd23f53`; its
runtime DLL SHA-256 is
`aeef5da71d48af98b33651746cfce52bbd0a2046181905845dc9eaa3679ebbdd`.
The CPython executable is Python 3.14.7.

## Partial full-suite screening — 2026-10-11

The all-97 XLang3 fast run is still active. Its completed async-tree cases
show that the local closure-call win has not closed the broad async gap:

| pyperformance case | CPython 3.14.7 mean | Closure-inline XLang3 mean | XLang3 slower by |
|---|---:|---:|---:|
| `async_tree_none` | 229 ms | 4.09 s | 17.8x |
| `async_tree_cpu_io_mixed` | 428 ms | 5.22 s | 12.2x |
| `async_tree_cpu_io_mixed_tg` | 425 ms | 6.19 s | 14.6x |
| `async_tree_eager` | 85.7 ms | 1.61 s | 18.8x |
| `async_tree_eager_cpu_io_mixed` | 338 ms | 3.26 s | 9.6x |
| `async_tree_eager_cpu_io_mixed_tg` | 385 ms | 5.89 s | 15.3x |
| `async_tree_eager_memoization` | 188 ms | 3.38 s | 18.0x |

These are screening ratios, not the completed suite aggregate. XLang3 values
are the pyperf means printed in the live log; several carry pyperf instability
warnings. CPython values come from the saved 3.14.7 full-run suite, using
`pyperf.BenchmarkSuite` statistics rather than JSON worker-duration metadata.
The XLang3 `async_tree_eager_io` and `async_tree_eager_io_tg` definitions both
hit the runner's 300-second whole-definition timeout. At this checkpoint the
run had advanced to case 14 of 97. No closure inliner claim is made for these
async workloads; their large gap points to broader VM/task execution costs.

The current full run also timed out on `async_tree_eager_memoization_tg`, while
two earlier full runs on pre-closure binaries completed it in 5.46 s and 6.55
s (the latter had 35% standard deviation). A case-only rerun against the saved
pre-closure Release binary and the current closure-inline binary did **not**
reproduce that timeout: pyperf measured 5.61 s ±0.36 s and 5.69 s ±0.29 s,
respectively, and `pyperf compare_to` hid the difference as not significant.
Both case-only runs still carried pyperf's instability warning. The full-run
timeout is therefore unconfirmed and is not attributed to the closure change.
The current binary remains 21.40x slower than CPython 3.14.7 on this case.

The all-97 candidate run was stopped at case 15 after the unexplained timeout;
it produced no complete JSON or suite aggregate. The case-only raw files are
[`async-tree-eager-memoization-tg-preclosure-fast-20261011-r2.pyperf.json`](data/async-tree-eager-memoization-tg-preclosure-fast-20261011-r2.pyperf.json)
and
[`async-tree-eager-memoization-tg-closure-inline-fast-20261011-r1.pyperf.json`](data/async-tree-eager-memoization-tg-closure-inline-fast-20261011-r1.pyperf.json);
the `pyperf compare_to` output confirms the XLang3-to-XLang3 difference is not
significant. The earlier 120-second baseline attempt was invalid because the
per-case cap was shorter than the normal 300 seconds; it is kept separately
and excluded from comparison.

Raw XLang3 output is
[`pyperformance-xlang3-closure-binary-inline-full-fast-20261011-r1.log`](data/pyperformance-xlang3-closure-binary-inline-full-fast-20261011-r1.log),
and the CPython reference is
[`pyperformance-cpython3147-live-eval-full-fast-20261007.json`](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json).

The next gate remains the completed full 97-definition run and comparison.
The suite-wide goal remains open.
