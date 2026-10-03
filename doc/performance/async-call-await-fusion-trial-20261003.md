# Adjacent async Call/Await fusion trial (2026-10-03)

## Result

Rejected. Fusing adjacent `Call` and `Await` handlers in the interpreter loop
did not produce a repeatable speedup in either targeted pyperformance case.
The trial was removed and the fixed Release executable and runtime DLL were
restored to the control hashes.

| Benchmark | XLang3 control | Candidate | CPython 3.14.7 reference |
| --- | ---: | ---: | ---: |
| `coroutines` | 140 ms ± 6 ms | 143 ms ± 4 ms | 17.9 ms |
| `async_tree_eager` | 3.18 s ± 0.26 s | 3.17 s ± 0.21 s | 86.62 ms |

```text
Elapsed time (less is faster; bars run left to right)
coroutines       control    140 ms |███████████████████|
                 candidate  143 ms |████████████████████|
                 CPython   17.9 ms |██|
async_tree_eager control   3.18 s  |███████████████████|
                 candidate 3.17 s  |███████████████████|
                 CPython   86.6 ms |█|
```

Both XLang3 pairs used the official pyperformance 1.14.0 `--fast` harness,
CPython 3.14.7 at `C:\Python\Python314\python.exe`, its shared benchmark
dependencies, and the repository Windows compatibility shim. Pyperf marked
the samples unstable. The async-tree difference is about 0.3%, far below its
sample spread; the coroutine candidate is about 2% slower, also within spread.
Neither result supports a runtime optimization.

## Implementation hypothesis

The trial recognized an adjacent `Call` followed by `Await` that consumes the
call result, ran the existing Call handler, and then invoked the existing
Await handler without returning through the opcode dispatch loop. It skipped
the fusion whenever tracing, profiling, debugging, or monitoring could observe
the instruction boundary. A focused recursive-coroutine probe confirmed the
unguarded shape was reached with no active observers and a normal Call return.
The Await counters remained unchanged because the fusion reused the ordinary
Await implementation; only its dispatch boundary was skipped.

This small reduction in dispatch work was not measurable end to end. Keep
ordinary Call/Await dispatch and focus further async work on the larger shared
VM costs. The full-run profile reports roughly 897,000 native calls and
hundreds of thousands of IR operations in `async_tree_eager`; frame-vector
reuse already delivered a separate measured 7.4% reduction in elapsed time.

## Build identities and raw results

The control executable and runtime DLL are SHA-256
`B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA` and
`330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`.
The temporary candidate was
`00C86990648DA0A17C31723FE6C5C77F2D2CCCA0E58DCBFB7BE918DB614CCB60`
and
`64CA4A00BA5D5B360F4D0C005AE422CAB3A01EC9C364BB86D74C6C2691C8A0D4`.
The fixed executable path remained
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe` throughout.

- `coroutines`: [control](data/coroutines-callawait-control-20261003.json), [candidate](data/coroutines-callawait-candidate-20261003.json).
- `async_tree_eager`: [control](data/async-tree-callawait-control-20261003.json), [candidate](data/async-tree-callawait-candidate-20261003.json).
