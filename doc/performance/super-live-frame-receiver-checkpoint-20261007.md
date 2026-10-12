# Live-frame receiver for super() — 2026-10-07

Zero-argument `super()` now reads its receiver from guarded live frame storage instead of constructing a dictionary of every local. Official Chameleon time falls nominally from **178.068 to 168.822 ms**, a **5.19% reduction / 1.055× previous-candidate speed**. It remains **14.25× slower than the saved CPython 3.14.7 reference**. The overall speed goal is unfinished.

## Runtime design

`Runtime::try_current_frame_first_argument` checks the physical frame identity, current storage, function metadata and the first local's name before reading slot zero. Module entry, logical inlined frames and unusual layouts retain the existing snapshot fallback. The returned receiver is retained for the native operation. Lexical defining-class inference and ordinary Python method dispatch remain in place.

Captured first parameters read the actual cell value. Native frame views expose non-owning cell storage for the active interpreter frame; they do not create extra ownership, GC roots or cross-thread cell snapshots. A deleted local or cell is rejected instead of reviving a stale receiver. Comments explain the layout guards, logical-frame fallback, cell semantics and why a locals dictionary must stay out of this call path. [CPython 3.14.7's native super initialization](https://github.com/python/cpython/blob/v3.14.7/Objects/typeobject.c#L11315) also reads the first frame argument and handles a captured cell.

Both the native `super` builtin and the VM builtin-constructor adapter use the shared reader. The initial candidate changed only the latter; the preserved and candidate fixtures continued to fail the captured-receiver check. The native builtin has its own entry path, so wiring that path was necessary. These failed attempts are retained rather than reported as validated improvements.

No Python Chameleon, inspect, typing or standard-library algorithm was translated into C++.

## Correctness and limits

All **360 core fixtures, 11 section fixtures and three expected-failure checks** pass, together with all **eight C++/SDK/graph checks**. The new CPython 3.14.7 fixture covers a receiver with an unconventional parameter name, reassignment, nested `nonlocal` assignment, classmethods, copied methods within a valid hierarchy, generators, async resumption, deleted receivers and traced original Python frames.

The captured-receiver assertion failed in the preceding Release too: the old locals snapshot used a stale physical local after a nested function updated its cell. The candidate fixes that case. Deleted-receiver tests call `super()` separately before attribute access, exercising native constructor errors; CPython's fused direct-super attribute instruction instead reports `UnboundLocalError` in the initial direct-attribute diagnostic. Both source variants and reference records are retained.

This checkpoint does not establish complete super ABI compatibility. An initial invalid-hierarchy diagnostic now reaches a separate existing validation gap: a method copied onto a sibling outside its lexical class hierarchy reaches the base method and raises `AttributeError`, where CPython rejects the super receiver with `TypeError`. Defining-class inference and receiver/class validation were not rewritten here. The diagnostic and failure remain recorded for follow-up; the final fixture's copied method uses a valid subclass hierarchy.

## Performance evidence

All **11 default fixed-gate cases** pass with **21 paired repeats, five warmups, the unchanged 10% threshold and exit 0**. The accepted Release baseline and candidate run path are unchanged. The preceding verified Release is preserved separately. Builds, correctness checks and benchmark runs finish sequentially before the next measurement starts.

| Official Chameleon | Mean | Speed relative to this candidate |
|---|---:|---:|
| Saved CPython 3.14.7 | 11.851 ms | 14.2456× |
| Previous XLang3 checkpoint | 178.068 ms | 0.9481× |
| Candidate | 168.822 ms | 1.0000× |

Both XLang3 runs and the saved reference have 20 timed values. These are nominal fast-mode comparisons, not a significance claim. The previous run has substantial variation; historical CPython binary identity limitations remain in the [saved-reference audit](data/cpython3147-saved-reference-provenance-audit-20261007.json). This selected run is not a new full-suite report and must not be spliced into the frozen 97-definition comparison.

Separate untimed renders in CPython and the candidate produce identical **222,553-character** output with UTF-8 SHA-256 `ee20adc6250db78d5443e8d50cc9e940f448151dab8ce51e5d83aea93531616c`. Official executable, runtime DLL and native hashlib hashes match at start/end.

The focused five-sample diagnostic measures 20,000 Python `super().get()` calls at a median **32.435 ms** in the candidate. The freshly rerun preserved control takes **45.852 ms** (1.414× control/candidate); the earlier checkpoint diagnostic was **44.325 ms**. CPython takes **2.632 ms**. These totals include loop/call overhead and are not official pyperf scores. Other operations also vary between passes, so the diagnostic does not isolate every cost or establish improvements for all native dictionary calls.

## Evidence

- [Source/binary identities and validation](data/super-frame-receiver-validation-20261007.json), [fixed gate](data/release-super-frame-receiver-fixed-gate-20261007.json), [gate log](data/release-super-frame-receiver-fixed-gate-20261007.log), [C++ checks](data/super-frame-receiver-cpp-sdk-final-20261007.log)
- [Official timings](data/pyperformance-xlang3-super-frame-receiver-chameleon-fast-20261007.json), [official log](data/pyperformance-xlang3-super-frame-receiver-chameleon-fast-20261007.log), [run provenance](data/pyperformance-xlang3-super-frame-receiver-chameleon-fast-20261007-provenance.json), [comparison and input hashes](data/super-frame-receiver-chameleon-comparison-20261007.json)
- [CPython fixture reference](data/super-frame-receiver-cpython3147-reference-20261007.json), [candidate fixture](data/super-frame-receiver-focused-fixture-20261007.log), [preserved Release](data/super-frame-receiver-preserved-control-20261007.json)
- [Candidate render identity](data/chameleon-super-frame-receiver-render-identity-20261007.log), [CPython render identity](data/chameleon-super-frame-receiver-cpython3147-render-identity-20261007.log)
- [Candidate call diagnostic](data/python-super-dict-get-xlang3-frame-receiver-20261007.log), [fresh control diagnostic](data/python-super-dict-get-preserved-control-current-20261007.log), [diagnostic comparison and hashes](data/super-frame-receiver-diagnostic-comparison-20261007.json)
- [Initial preserved-control cell failure](data/super-frame-receiver-preserved-control-fixture-20261007.log), [initial VM-only candidate failure](data/super-frame-receiver-initial-fixture-20261007.log), [cells-only fixture failure](data/super-frame-receiver-cells-only-fixture-failure-20261007.log), [cells-only full-suite failure](data/super-frame-receiver-cells-only-full-fixtures-failure-20261007.log), [invalid-hierarchy diagnostic](data/super-frame-receiver-invalid-hierarchy-diagnostic-20261007.log)
- [Original CPython fixture reference](data/super-frame-receiver-cpython3147-reference-initial-20261007.json), [direct-attribute deletion reference](data/super-frame-receiver-cpython3147-reference-deleted-attr-20261007.json)
- [Initial build](data/build-super-frame-receiver-Release-20261007.log), [cell-view build](data/build-super-frame-receiver-cells-Release-20261007.log), [native-entry build](data/build-super-frame-receiver-native-entry-Release-20261007.log)
