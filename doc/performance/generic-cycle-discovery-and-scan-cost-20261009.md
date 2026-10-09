# Generic cycle discovery and scan cost, 2026-10-09

Ordinary unreachable instance/container cycles were missing from XLang3's collector discovery. The candidate adds a generic owning-edge graph pass and passes fresh correctness validation, but its first two versions fail the unchanged fixed performance gate. No engine acceptance or whole-suite speed win is established by these results. The cached-adjacency version is built and correctness-tested; its timing is pending an idle host.

The existing accepted `95feaff2` engine and fixed `build-repro/Release` baseline remain preserved. GitHub `aef4dc0b` records the original independent missing-cycle reproduction and the previously rejected frame-context experiment. The generic GC candidate remains uncommitted.

## Repair and ownership rules

The specialized class/weakref/native collector did not enumerate the generic tracked list/instance heap. The new private `gc_plain_cycles.h` pass snapshots supported storage objects under VM serialization, counts their owning edges, and propagates external-root reachability. It subtracts exactly one snapshot pin per object. Borrowed Values contribute no incoming ownership; repeated owning references contribute their entire multiplicity. Modules, frames, functions and native payloads retain external ownership through their refcounts.

Weakrefs, finalizers, native instance payloads and opaque cleanup owners are conservative boundaries. The pass propagates those boundaries backward before clearing any storage. It allocates retirement capacity before mutation, publishes the complete empty graph and coherent container indexes, then releases moved ownership while class metadata remains pinned. The outer collection guard prevents cleanup from reentering collection. Unsynchronized builds with global VM locking disabled do not use the new graph pass. This is not a claim of complete native/finalizer cycle collection parity.

All Python library bodies remain Python. This is a generic runtime repair in XLang3's own native `gc` module, not a native translation of a pure-Python library or reuse of CPython's DLLs.

## Measurements so far

Each version runs the complete unchanged 11-case fixed Release gate: 21 repeats, five warmups, 10% tolerance, original case source hashes. The gate retains its fresh regression confirmation. Every affected original benchmark runs afterward, including when the gate fails. Original `gc_collect` and `gc_traversal` definitions run through the same shared dependency site and runner in CPython 3.14.7 and XLang3, fast mode, 20 scored values per case. These are separate directional fast runs, not paired confidence intervals or a new full97 aggregate.

| Version | Graph work | Fixed gate: GC candidate/baseline time | Gate result |
| --- | --- | ---: | --- |
| R2 | Lookup each owning edge; rescan storage for reachability | 3.438× | Failed |
| R3 | Group consecutive equal owning targets; preserve multiplicity | 1.791× | Failed |
| R4 | Also reuse unique forward adjacency for reachability | Pending | Pending |

| Original benchmark | R2 CPython 3.14.7 | R2 XLang3 | R3 CPython 3.14.7 | R3 XLang3 |
| --- | ---: | ---: | ---: | ---: |
| `create_gc_cycles` (definition `gc_collect`) | 1.5255 ms | 3.3363 ms | 1.5173 ms | 3.3011 ms |
| `gc_traversal` | 2.3086 ms | 7.0990 ms | 2.3336 ms | 4.1426 ms |

Grouping reduces observed traversal time by about 1.714× against the repaired R2 XLang3, but R3 still takes about 1.775× its fresh CPython reference's time. Cycle creation/collection remains about 2.176× CPython's time. No engine change can be committed while the fixed gate fails or remains missing.

R4 removes the second storage scan: unique forward edges are captured alongside reverse edges during counting, and reachability visits those edges. Counting still includes every owning reference, and retirement still moves every stored Value. Comments in the helper explain why reachability deduplication must not become ownership deduplication.

## Correctness and provenance

R2, R3 and R4 each pass fresh CPython/XLang3 oracle transcripts, 406 core fixtures, 11 compatibility sections, three expected-failure checks, nine selected CTests and both SQLite API checks. Tests cover unseeded instance/list/dict/mixed/slotted/cell cycles, duplicate ownership, borrowed refs, external native roots, another thread's VM root, opaque cleanup boundaries and pending/handled exception identity. Fixture identity checks use a non-owning marker in addition to addresses, avoiding a false failure when `gc.get_objects()` allocates a list at a reclaimed address.

The first R2 attempt had a C++ test link failure from using an internal unexported snapshot helper. The integration repair tests through registered `gc.get_objects()` instead, without introducing a new export; collection sequencing is explicit. The earlier failed build inputs and raw log remain available. No benchmark ran on that failed build.

Before each subsequent engine edit, complete Release178 and source142 maps were authenticated and copied into separate controls. R2 and R3 controls are correctness-passing, performance-failing experimental controls, not accepted baselines. The fixed baseline177 was unchanged throughout. These are selected-source inventories from a dirty worktree; they do not prove a clean-checkout reproduction. Unrelated working files were protected rather than included in the engine change.

R4's first timing launch was refused before any phase because another project's `ctest.exe` and reusable `MSBuild.exe` worker were present. Its invalid/preflight receipt is retained. The follow-up waits for actual OS process disappearance and then uses the same original protocol and guards; it does not kill other projects' processes or relax the idle criterion.

## Evidence

- R2: [application](data/gc-generic-cycles-applied-source-r2-20261009.json), [successful build](data/gc-generic-cycles-build-r2-20261009.json), [correctness](data/gc-generic-cycles-correctness-20261009.json), [gate and original scores](data/gc-generic-cycles-performance-20261009.json).
- R3: [application](data/gc-generic-cycles-applied-source-r3-20261009.json), [build](data/gc-generic-cycles-build-r3-20261009.json), [correctness](data/gc-generic-cycles-correctness-r3-20261009.json), [gate and original scores](data/gc-generic-cycles-performance-r3-20261009.json).
- R4: [application](data/gc-generic-cycles-applied-source-r4-20261009.json), [build](data/gc-generic-cycles-build-r4-20261009.json), [correctness](data/gc-generic-cycles-correctness-r4-20261009.json), [initial preflight refusal](data/gc-generic-cycles-performance-r4-20261009.json).

The earlier full97 report is unchanged: individual repaired GC results do not turn its recorded failures into completed cases. The overall performance goal remains unfinished. This goal turn makes progress by implementing and validating graph-scan reductions and measuring their effects; it does not claim goal completion.
