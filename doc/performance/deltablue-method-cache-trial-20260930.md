# DeltaBlue non-owning method-cache cleanup trial (2026-09-30)

This trial tested whether frame return could skip reconstructing guarded,
non-owning `CallMethod` cache entries. CPython keeps method guards at their
code sites. XLang3 already retained selected raw function/native pointers with
receiver-class version guards, but rebuilt the cache aggregate when each frame
returned. The candidate skipped that reconstruction only when the listed
non-owning cache kind had no retained `Value` or owning vectors; all other
entries still used the existing cleanup path.

## Result

The change did not produce a significant improvement in either of the two
call-heavy targets. The fast runs had opposite directions and pyperf warned
about instability. The rigorous runs also warned about host variation and
`compare_to` hid both benchmarks as statistically insignificant.

| Workload | Parent | Candidate | Candidate / parent | Decision |
|---|---:|---:|---:|---|
| `deltablue` | 45.1 ± 3.6 ms | 45.8 ± 4.3 ms | 1.016× | No significant change |
| `unpickle_pure_python` | 3.37 ± 0.23 ms | 3.38 ± 0.33 ms | 1.003× | No significant change |

The candidate is rejected and its source change was reverted. This avoids
turning an attractive cache-ownership rationale into an unsupported
performance claim. The latest CPython comparison and the separate native VM
timing evidence remain in the
[DeltaBlue implementation analysis](deltablue-cpython314-implementation-analysis-20260930.md).

## Diagnostic context

The separate instrumented Release probe attributed about 4.40 ms per
DeltaBlue iteration to cache cleanup, 4.78 ms to frame reset, 2.56 ms to frame
view publication, and 2.22 ms to current-frame identity updates. These are
instrumented exclusive times: the timer adds substantial overhead and they
are not benchmark scores or estimates of achievable speedup. Skipping the
safe method-cache reconstruction did not measurably reduce the ordinary
pyperf result, so cache cleanup is not the next optimization target.

The profile subtracts a one-iteration process from a 21-iteration process and
divides the delta by 20. It runs the unchanged pyperformance 1.14.0 DeltaBlue
body under the same direct diagnostic harness described in the implementation
analysis. Raw logs, CSV, and the temporary instrumentation patch are retained
below. The instrumented source was restored before building the pyperf
candidate.

## Reproduction data

- DeltaBlue fast: [parent JSON](data/deltablue-method-cache-parent-fast-20260930.json), [parent log](data/deltablue-method-cache-parent-fast-20260930.log), [candidate JSON](data/deltablue-method-cache-candidate-fast-20260930.json), [candidate log](data/deltablue-method-cache-candidate-fast-20260930.log).
- DeltaBlue rigorous: [parent JSON](data/deltablue-method-cache-parent-rigorous-20260930.json), [parent log](data/deltablue-method-cache-parent-rigorous-20260930.log), [candidate JSON](data/deltablue-method-cache-candidate-rigorous-20260930.json), [candidate log](data/deltablue-method-cache-candidate-rigorous-20260930.log).
- Pure-Python unpickle fast: [candidate JSON](data/unpickle-frame-cache-candidate-fast-20260930.json), [candidate log](data/unpickle-frame-cache-candidate-fast-20260930.log), [parent JSON](data/unpickle-frame-cache-parent-fast-20260930.json), [parent log](data/unpickle-frame-cache-parent-fast-20260930.log).
- Pure-Python unpickle rigorous: [parent JSON](data/unpickle-frame-cache-parent-rigorous-20260930.json), [parent log](data/unpickle-frame-cache-parent-rigorous-20260930.log), [candidate JSON](data/unpickle-frame-cache-candidate-rigorous-20260930.json), [candidate log](data/unpickle-frame-cache-candidate-rigorous-20260930.log).
- Frame-cost probe: [one iteration](data/deltablue-frame-costs-1loop-20260930.txt), [21 iterations](data/deltablue-frame-costs-21loops-20260930.txt), [startup-subtracted CSV](data/deltablue-frame-costs-delta20-20260930.csv), and [instrumentation patch](data/deltablue-frame-cost-probe-20260930.patch).
- [Rejected candidate patch](data/deltablue-method-cache-candidate-20260930.patch).

The parent runtime was built from source commit `be989fce1a8d25aa5bc151e61b3a58f279c09cb5`; the candidate changed only the cache cleanup branch. Both used the same executable SHA-256 `D3FCD013158F522B30ABD8781DDABE67C3C2755AC19BCA4FE5D923D67E788D95`. The parent runtime DLL SHA-256 was `22A716FDE9C9A39E1D0A6AF87D6FDCF49CA02B6EECF276B7E627BBA94EBB034F`; the rejected candidate runtime DLL SHA-256 was `74D778EEB1DB7EB0EA9E057E4CD1BA5D276BB338F9A11C05FF97E44FB6C05F07`.
