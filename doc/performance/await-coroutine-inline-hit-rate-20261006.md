# Coroutine inline-await hit-rate diagnostic (2026-10-06)

I temporarily counted exact child-coroutine frame pushes and coroutine
`.send()` fallbacks, then removed the counters after the diagnostic. This
distinguishes the existing `Await` fast path from the separate Task-resume path.

On the scaled async-tree workload (`--levels 4 --branches 3 --iterations
100`), XLang3 executed 8,000 `Await` opcodes but recorded zero child-coroutine
inline pushes and zero coroutine `.send()` fallbacks. Its awaited values are
Future or other awaitable objects, so the recursive-coroutine `SEND` fast path
is not involved in this benchmark's remaining gap.

On the official `coroutines` workload body (`fibonacci(25)`), XLang3 recorded
242,784 inline child-coroutine pushes and zero `.send()` fallbacks, matching
the 242,784 child `Await` operations. The existing guarded path therefore
covers this recursive-coroutine case completely. Its remaining gap cannot be
fixed by duplicating that same shortcut.

This narrows the next async-tree work to Task/Future scheduling and resuming a
Task's saved coroutine frame from the native `_asyncio.Task` step. It also
confirms that more recursive-await inlining work would target a path already
fully taken by the standalone coroutine benchmark. These counters are
diagnostic only; the instrumented elapsed time is not a performance score.

The measurement used the Release build, the Python 3.14.7 runtime library, and
`--perf-counters`. The temporary counter code was removed. The fixed Release
binary pair was restored:

- Executable SHA-256: `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548`
- Runtime DLL SHA-256: `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D`

## Raw counter excerpts

- [Probe commands and selected output](data/await-coroutine-inline-hit-rate-20261006.txt)
