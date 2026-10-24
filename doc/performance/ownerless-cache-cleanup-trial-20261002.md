# Retain ownerless inline-cache guards during frame cleanup (2026-10-02)

## Result

Retained for now. On a source-matched Release control, the change reduced
time by **2.0%** on a Pickler-shaped workload and **1.3%** on `subparsers`.
Both paired 95% intervals were entirely faster than the control. All 11 cases
in the fixed Release gate stayed within its 10% regression threshold.

The change avoids resetting and rebuilding a cache record when an existing
attribute or method cache is already one of the non-owning guard kinds allowed
to survive activation reuse, and every owning `Value` field and vector is
empty. The code comments describe the ownership test beside the optimization.
Fused global payloads and monitoring disable masks still clear on every return.
No CPython pure-Python library code changes.

## Source-matched performance

The order-balanced comparisons used 21 AB/BA pairs and three warmups. Values
below 1.0 mean the candidate used less time.

| Workload | Control median | Candidate median | Candidate / control | 95% paired interval |
|---|---:|---:|---:|---:|
| Pickler-shaped nested payload | 49.562 ms | 48.551 ms | 0.9802x | 0.9733–0.9855 |
| `subparsers` | 269.887 ms | 265.753 ms | 0.9870x | 0.9777–0.9955 |

The focused Pickler case performs 60 fresh pure-Python `_Pickler` dumps to
`BytesIO`; it is diagnostic, not an official pyperformance result. Raw samples
are in [Pickler A/B](data/ownerless-cache-cleanup-pickle-ab-20261002.json) and
[`subparsers` A/B](data/ownerless-cache-cleanup-subparsers-ab-20261002.json).

The candidate was also checked on the default 3-level, 3-branch,
50-iteration scaled asyncio tree. It was effectively unchanged at **1.002x**
candidate/control (95% interval **0.972–1.047**), so this frame-cleanup
optimization does not explain or materially reduce the much larger async-tree
gap. Raw paired samples are in
[`async-tree scaled A/B`](data/ownerless-cache-cleanup-async-tree-scaled-ab-20261002.json).

## Fixed Release gate

The candidate passed all 11 cases against
`scratch/performance/baseline-0336992/xlang3.exe` using 21 pairs per case and
the existing 10% limit. These cumulative ratios are not an attribution of
this change; the source-matched measurements above isolate it.

| Case | Candidate / fixed baseline |
|---|---:|
| `local_slots` | 0.380x |
| `scalar_arithmetic` | 0.963x |
| `range_for` | 1.001x |
| `function_calls` | 0.981x |
| `class_construct` | 1.008x |
| `list_append` | 1.007x |
| `property_access` | 1.011x |
| `deepcopy_memo` | 0.560x |
| `json_dumps` | 0.816x |
| `gc_traversal` | 0.994x |
| `subparsers` | 0.872x |

The final artifact's gate geometric ratio was 0.839x, reflecting the
accumulated improvements since the older fixed baseline. Every case stayed
inside the existing 10% limit. Per-case raw results for the final artifact are
preserved as [`final fixed-baseline JSON files`](data/ownerless-cache-cleanup-final-fixed-baseline-local_slots-20261002.json)
and adjacent files for each case. Earlier gate files are retained separately.

## Correctness and limits

The Release `xlang3_interpreter_tests` target passed, including new checks
that surviving guards do not retain fused global or cache payload owners and
that monitoring masks still reset. `vm_cache_materialization`,
`load_attr_cache_precedence`, `property_access_batch`, and
`custom_getattribute_descriptor` fixtures passed.

CTest ran 50 tests: 42 passed and 8 failed. The failures were in SQLite/native
module compatibility, a native network return type, Python 3.14-only
`annotationlib` and `site` availability, access to the protected Python 3.14
executable, and an existing Visual Studio launch-profile expectation. They do
not exercise frame-cache cleanup; the targeted ownership test and core
fixtures passed. Full CTest is therefore not claimed as green.

`pyperf` and `pyperformance` are not installed in the available Python 3.13
environment, so the official full suite and official `pickle_pure_python`
comparison have not been rerun for this change. The goal of broad wins over
CPython 3.14 remains open.

Final rebuilt candidate `xlang3.exe` SHA-256:
`E0E46ED011542F262ADE92263679C5E4AABFDE86756F256CF2B1C0683406EC78`.
Its runtime DLL SHA-256:
`17DE90B570985D98165C17884A3A0404E85A1D9E492D079CF44EB6CDC0245498`.
The source-matched focused A/B measurements identify the exact control and
candidate binaries in their respective raw JSON files.
