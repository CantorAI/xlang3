# Runtime protocol native-cost investigation — 2026-10-07

The compliant runtime-protocol benchmark remains 25.3× slower than the saved CPython 3.14.7 reference. The immediate objective is to improve shared runtime costs without replacing inspect or typing's Python algorithms.

## Warmed evidence

The call-profile helper now supports an explicit `PYPERF_PROFILE_WARMUP_CALLS` count, defaulting to zero. One unprofiled call over 20 benchmark loops warms lazy inspect imports and native caches; the subsequent 20-loop profile contains 4,020 `_shadowed_dict` calls and 1,140 `_check_class` calls in both runtimes. Those frequency counts identify repeated MRO, weakref and class-dictionary operations.

These are diagnostics only. Profiling changes optimization eligibility. XLang3 and CPython also have different totals of matching profile events for some functions, so their inclusive/self profile times and all-function call totals cannot establish complete comparative accounting. The native primitive and scaling probes below run without sys.setprofile.

## Primitive diagnostic

The primitive probe runs 20,000 warmed operations in each runtime; each value includes the same Python loop and call overhead. These are not official pyperf scores.

| Operation | CPython 3.14.7 total | XLang3 total |
|---|---:|---:|
| Python identity function | 0.663 ms | 2.016 ms |
| Native MRO getter | 0.779 ms | 7.062 ms |
| Native weakref creation/reuse | 0.840 ms | 20.155 ms |
| Native weakref dereference | 0.547 ms | 7.008 ms |
| Python inspect._shadowed_dict | 5.685 ms | 91.345 ms |

MRO identity is stable in CPython and unstable in XLang3. Source confirms XLang3 materializes a new tuple from its already cached MRO vector for each public getter. Caching that tuple must account for invalidation, observable identity, self references and collection; the internal vector deliberately borrows self, so adding an owning cached tuple without lifetime work would be unsafe.

## Verified weakref scaling defect

The second diagnostic creates unrelated live references, then performs 5,000 warm lookups/dereferences of one probe reference. It verifies that ref reuse preserves identity and dereferencing yields the live target.

| Unrelated live references | CPython reuse | XLang3 reuse | CPython dereference | XLang3 dereference |
|---:|---:|---:|---:|---:|
| 0 | 0.202 ms | 5.333 ms | 0.098 ms | 1.708 ms |
| 100 | 0.197 ms | 6.214 ms | 0.103 ms | 1.758 ms |
| 1,000 | 0.208 ms | 9.394 ms | 0.095 ms | 2.915 ms |
| 5,000 | 0.200 ms | 20.324 ms | 0.099 ms | 7.334 ms |

CPython stays approximately flat; XLang3 lookup cost grows as unrelated references accumulate. In `weakref_module.cpp`, `weakref_candidates_for_target` scans the global registry to reuse a ref, and `weakref_get_target` scans it again to dereference one. Registration and invalidation also scan that vector. This introduces costs proportional to all registered references into Python inspect and native ABC/cache operations.

[CPython's native weakref implementation](https://github.com/python/cpython/blob/v3.14.7/Objects/weakrefobject.c) keeps references on their target's list, puts callback-free basic references at the head for reuse, and stores the target on the reference for dereferencing. Its lookup avoids scanning unrelated targets. XLang3 can improve its own native implementation while preserving the Python import/API contract.

The next optimization should index reference-to-target and target-to-reference relationships, preserving raw weak ownership, callback order, invalidation, dead-reference hash/equality, thread lifetime and GC enumeration. Keep the existing global enumeration if collection requires it; do not disable collection or replace the Python protocol loop. Measure the same scaling probe and official runtime protocols after full correctness and the unchanged Release gate.

## Evidence

- [Diagnostic provenance and input/binary hashes](data/typing-protocol-weakref-diagnostic-provenance-20261007.json)
- [Warm CPython profile](data/typing-runtime-protocols-python-call-profile-warm-cpython3147-20261007.log), [warm XLang3 profile](data/typing-runtime-protocols-python-call-profile-warm-xlang3-20261007.log)
- [Primitive CPython data](data/typing-protocol-runtime-primitives-cpython3147-20261007.log), [primitive XLang3 data](data/typing-protocol-runtime-primitives-xlang3-20261007.log)
- [Weakref CPython scaling](data/weakref-registry-scaling-cpython3147-control-20261007.log), [weakref XLang3 scaling](data/weakref-registry-scaling-xlang3-control-20261007.log)
- [Current official benchmark checkpoint](format-repr-runtime-checkpoint-20261007.md)
