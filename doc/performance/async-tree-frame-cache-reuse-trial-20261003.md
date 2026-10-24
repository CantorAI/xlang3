# Cross-activation VM instruction-cache reuse trial (2026-10-03)

## Hypothesis

The accepted thread-local frame pool reuses storage but clears a frame's
function identity after each completed interpreter activation. A new eager
Task therefore rebuilds that function's instruction caches even when a later
Task at the same interpreter depth runs the same still-loaded code. This trial
kept the cache identity with a weak module owner, restoring it only while the
module remained alive. Expired modules fell back to the existing cache reset.

## Measurement and decision

The target was pyperformance 1.14.0 `async_tree_eager`, with the Python 3.14.7
standard library and the same dependency/shim configuration used by the full
comparison. Each fast-mode run emitted 20 values, but both triggered
pyperf's low-sample stability warning.

| Build | Mean | Standard deviation | Median |
| --- | ---: | ---: | ---: |
| Accepted frame-pool control | 3.105 s | 0.273 s | 3.051 s |
| Weak-owner cache candidate | 3.086 s | 0.227 s | 3.035 s |

The candidate is only **0.6% faster by mean**, well inside the observed run
variation. Three additional single-value pairs changed direction with large
run-order swings. The C++ interpreter/runtime tests and four focused
async/generator fixtures passed, but the measurement does not justify keeping
the extra weak-owner bookkeeping. The candidate code was removed and the
fixed Release executable was restored to the accepted frame-pool build.

| Build | Release executable SHA-256 | Runtime DLL SHA-256 |
| --- | --- | --- |
| Control | `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA` | `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F` |
| Candidate | `CA0E4B3BCC3B52EE29C9E06E592B3D88BC1C2B9B7F3AA102A8CFFD73B836DFB5` | `69CB1791E51EFC68CA5F70EE2BF8BFD62A50A2528C8CCF5889623F6FBDFDB3AA` |

## Raw data

- [Control pyperf JSON](data/frame-vector-pool-weak-cache-control-fast-20261003.json)
- [Candidate pyperf JSON](data/frame-vector-pool-weak-cache-candidate-fast-20261003.json)

