# Runtime protocol primitive refresh — 2026-10-10

This refresh runs the existing diagnostic once on the current Release candidate
and CPython **3.14.7**. It is a diagnostic, not an official pyperformance
score. The interpreter and helper code are the same in each run, with 20,000
warmed operations per row and no profiler or opcode counters enabled.

| Primitive | CPython 3.14.7 | XLang3 candidate | XLang3 / CPython elapsed |
|---|---:|---:|---:|
| Python identity call | 0.513 ms | 1.826 ms | 3.56× |
| Native `type.__mro__` getter | 0.666 ms | 6.520 ms | 9.79× |
| Native weakref creation/reuse | 0.721 ms | 18.285 ms | 25.36× |
| Native weakref dereference | 0.439 ms | 5.970 ms | 13.59× |
| Python `inspect._shadowed_dict` | 5.031 ms | 99.852 ms | 19.85× |

CPython reports stable `__mro__` tuple identity; the XLang3 candidate still
returns a fresh tuple. The current `object_model.cpp` getter builds a tuple
from the cached MRO vector on each read. This confirms the MRO issue is still
present in the latest candidate, but the probe does not show how much of the
official `typing_runtime_protocols` or `sqlalchemy_imperative` gap it explains.

**Source follow-up:** that statement describes the executable and source hash
captured by this primitive run. A later working-tree change added an owning,
stable public MRO tuple cache with hierarchy invalidation and class-cycle GC
edges. Its targeted official measurements and the GC traversal follow-up are
recorded in [the MRO cache trial](mro-gc-edge-fastpath-trial-20261010.md).
Those later measurements do not retroactively change this primitive capture.

Weakref registry indexes are present in the current source and remove scans of
unrelated references from ordinary lookup. The creation and dereference costs
remain high in this probe, consistent with the earlier indexed-weakref report:
the indexes improved scaling with unrelated references but did not make weakref
operations CPython-fast. `weakref_get_target` still takes the shared registry
mutex, looks up the reference in a native map, then performs a retain-if-alive
operation. Weakref creation also allocates an instance and callback attribute
before registering it in the indexes. This is a concrete path to investigate;
these timings do not apportion its individual costs.

The identity-call row is itself 3.56× slower, so call/runtime overhead remains
part of the comparison. The protocol helper takes 19.85× longer overall. These
rows support multiple accumulated costs and do not establish one cause for the
full suite.

The latest full-suite report remains the older 77/97 capture; its
`typing_runtime_protocols` ratio was 22.61×, unpaired to the saved CPython
reference. The primitive results here are current-candidate diagnostics and do
not update that official suite matrix.

## Provenance

- XLang3 executable: `build-repro/main-verify-20261006/Release/xlang3.exe`,
  SHA-256 `B6DE33BE3002F7B72F16AF353E6030EBE2AE031F00C397F84ACD31D801D726D1`.
- CPython: `C:\Python\Python314\python.exe`, SHA-256
  `4942B86A6597E5AEE0128DAA00050ED79BC21F6E709A78EB19CBFEB0C2F39AC9`.
- Helper: [`typing_protocol_runtime_primitives.py`](../../benchmarks/diagnostics/typing_protocol_runtime_primitives.py).
- [CPython raw output](data/typing-protocol-primitives-cpython3147-current-r6-20261010.log),
  [XLang3 raw output](data/typing-protocol-primitives-xlang3-current-r6-20261010.log).
- Result SHA-256: CPython `31D0296533A4A3AB67A031F0BA51A4EFA8429DDBB54059F0830FE928C660FB2E`,
  XLang3 `765364DFEB86F2CE92E17418D2C12BCF4DA3C5CDE09E7EEB15CA0D93DB8C511E`.

## Rejected single-reference reuse fast path

A guarded fast path was tried for the single-entry, callback-free weakref cache
case, which avoids constructing `weakref_candidates_for_target`'s temporary
owning vector. The focused `weakref_registry_index` fixture passed, including
identity reuse, callback order, subclasses and repeated target lifetimes. One
unpaired 20,000-operation primitive sample measured weakref reuse at 22.568 ms
with the fast path versus 18.285 ms in the earlier candidate run. CPython's
control also moved from 0.721 ms to 0.796 ms, and other probe rows shifted by
roughly 15–25%, so this is not a reliable regression estimate. It did not
establish a gain; the source change was removed and the candidate rebuilt back
to its pre-trial state. Preserve the [trial fixture output](data/weakref-single-reuse-fastpath-r1-fixture-xlang3-20261010.log),
the [trial CPython values](data/typing-protocol-primitives-cpython3147-weakref-r1-20261010.log),
the [trial XLang3 values](data/typing-protocol-primitives-xlang3-weakref-r1-20261010.log),
and [build/revert logs](data/build-weakref-single-reuse-fastpath-r1-Release-20261010.log).
