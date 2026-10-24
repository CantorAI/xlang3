# Asyncio FutureState free-list trial (2026-10-05)

## Result

The proposed thread-local free-list for native `_asyncio` `FutureState`
payloads was rejected. The official pyperformance 1.14.0 `async_tree_eager`
fast-mode run measured a mean of **1.54414 s** with the pool and **1.53429 s**
without it, so the candidate took **1.0064×** the control time (about 0.6%
slower). This is within measurement noise and provides no demonstrated gain.
The CPython 3.14.7 reference for the same benchmark is 86.62 ms.

The implementation attempted to reuse up to 256 cleared `FutureState` objects
per thread, and rejected payloads whose additional-callback vector capacity
exceeded eight entries. The path does not remove the dominant work in eager
asyncio: it only avoids occasional state allocations, while every lifecycle
still clears all of the Future's owned `Value` edges and unregisters it.
Because the focused result did not improve, the code was discarded; no runtime
change from this trial is included in the checkpoint.

## Validation

- Release build succeeded with the configured Visual Studio 2026 compiler and
  Windows SDK tools.
- All **55/55** CTest tests passed.
- The full default 11-case fixed-Release regression gate passed at its 10%
  threshold. Its report records baseline executable SHA-256
  `94F65647D7E3116A81CC7D1E7783D5E951E7A260101667177257F9266502E033` and
  candidate executable SHA-256
  `85F6FC5446C35F85E031B7866B470D85367C58AA42DF7D448E8DD708C6F2ECA5`.
- The official pyperformance comparison used the shared CPython 3.14.7
  dependency site and the repository's Windows pyperf compatibility shim.
  Both runtimes ran the unchanged `async_tree_eager` benchmark in fast mode.

## Raw evidence

- [Pool candidate pyperf JSON](data/asyncio-future-state-pool-candidate-20261005.json)
- [No-pool fixed Release control pyperf JSON](data/asyncio-future-state-pool-control-20261005.json)
- [Fixed-Release regression-gate JSON](data/asyncio-future-state-pool-fixed-release-gate-20261005.json)
