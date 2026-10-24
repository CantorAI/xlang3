# Fixed Release candidate r3 vs r4 — 2026-10-11

## Result

The complete r4 run has several specific wins, but no full-suite gain over r3.
Both builds attempted all 97 definitions, completed 61, and failed or timed out
on 36. They have 70 matched subtests. `pyperf compare_to` reports an overall
geometric mean of **1.00x slower** for r4; this is effectively no change and
does not support a suite-wide speedup claim.

| Case | r3 | r4 | r4 vs r3 |
|---|---:|---:|---:|
| `json_dumps` | 34.6 ms | 28.8 ms | 1.20x faster |
| `richards` | 337 ms | 335 ms | 1.01x faster |
| `richards_super` | 415 ms | 404 ms | 1.03x faster |
| `scimark_lu` | 1.02 s | 303 ms | 3.35x faster |
| `scimark_monte_carlo` | 544 ms | 524 ms | 1.04x faster |
| `async_tree_none` | 3.89 s | 4.99 s | 1.28x slower |

The SciMark LU result is the largest build-to-build improvement, but r4 still
takes **4.03x** CPython 3.14.7's 75.3 ms. The `json_dumps` result remains
**3.78x** slower than CPython, and `richards` remains **9.81x** slower. The
async tree non-I/O case is **21.7x** slower than CPython in r4. The r4 vs CPython
full comparison and left-to-right ratio chart are in the
[r4 report](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147.md).

These are comparisons between two Release binaries built at different stages
of a heavily modified working tree. A later current-source A/B isolated the
guarded integer-branch `__getitem__` inline: with only that path disabled,
SciMark LU measured 771 ms; enabled it measured 306 ms (2.52x faster). This
identifies the frame-elision optimization as the mechanism behind the LU gain,
though the older 3.35x r3-to-r4 ratio is still a cross-build comparison. See
the [source-matched SciMark trial](scimark-arraylist-getitem-call-frame-root-cause-20261011.md)
for hashes, raw JSON and limits. Separately, the string-dict cache's isolated
pyperformance comparison was only a 1.03x `richards` improvement and showed no
significant `typing_runtime_protocols` change. The async rich-proof cache gives
a 1.02x debug-mode whole-case improvement while the official fast
`async_tree_io` case still times out.

## Reproduction and evidence

- [Complete `pyperf compare_to` output](data/pyperformance-xlang3-full-r3-vs-r4-20261011.txt)
- [r3 XLang3 JSON](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r3.pyperf.json)
- [r4 XLang3 JSON](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.pyperf.json)
- [CPython 3.14.7 reference JSON](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json)
- [r4 97-case log](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.log)

The r4 binary hashes and exact per-subtest CPython ratios are in the
[r4 report](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147.md)
and its [matched CSV](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147-subtests.csv).
