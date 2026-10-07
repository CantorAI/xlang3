# Indexed native weak-reference lookup checkpoint â€” 2026-10-07

Native weak-reference reuse and dereferencing no longer scan the global registry of unrelated references. The candidate passes full correctness and the unchanged fixed Release gate. Official runtime-protocol time falls nominally by 5.2% relative to the preceding repr checkpoint, and GC traversal completes. The overall goal remains unfinished: protocols are still 24.0Ã— slower than the saved CPython 3.14.7 reference.

## Runtime design and preserved semantics

Two pointer indexes map reference objects to their targets and target objects to their registered references. They hold raw pointers and share the existing registry mutex; neither side gains strong ownership. Target invalidation clears target entries, and reference destruction removes lookup keys before allocator address reuse. Canonical ref/proxy reuse, reference subclasses, cached hashes, dead-reference behavior and target enumeration retain their existing API.

The global enumeration vector remains for GC discovery and reverse registration callback order. Invalidation still uses that ordering, and rare reinitialization retains its original enumeration position. Ordinary lookup visits only references for the requested target, or performs one reference-key lookup. Comments explain weak ownership, locking, invalidation, ordering and the requirement to avoid scans of unrelated inspect/library references. No Python inspect or typing algorithm was translated to C++; this changes XLang3's own native `_weakref` implementation. [Measured source investigation and CPython native reference](typing-protocol-native-cost-investigation-20261007.md).

The indexes use additional native hash-table/vector storage. Registration and invalidation maintain both representations; invalidation and global GC enumeration are still linear operations. This checkpoint optimizes lookup rather than claiming all weakref costs are constant-time or CPython-fast.

## Correctness and fixed gate

The new lifetime fixture passes CPython 3.14.7, the preserved preceding Release and the candidate. It verifies ref/proxy reuse, counts and enumeration amid 1,000 unrelated references, callback order, invalidation, dead hashes, reference subclasses and 256 successive short lifetimes after setup activation exit. Existing thread/lifetime/GC fixtures remain in the complete suite.

All 356 core fixtures, 11 section fixtures and 3 expected-failure checks pass; eight C++/SDK/graph checks pass. All 11 default fixed-gate cases pass, with 21 order-balanced paired repeats, 5 warmups, the unchanged 10% threshold and exit code 0. The accepted baseline is unchanged.

An initial module-scope version of the fixture fails to release the referent in both the preserved control and the candidate. A named callback factory at module scope also fails. The successful fixture tests lifetime after a setup function returns; it does not establish that the module-frame issue is repaired. The original source and failed logs are preserved for separate investigation. The exact retention cause is not established.

## Official pyperformance results

Both selected definitions complete under the existing 300-second full-definition cap; exit code 0. Each has 20 timed values. Executable, runtime DLL and native hashlib hashes match at start/end. Fast-mode stability warnings remain; these are nominal means, not significance claims or a full-suite report.

| Case | Saved CPython 3.14.7 | Previous XLang3 candidate | Indexed candidate | CPython / XLang3 speed |
|---|---:|---:|---:|---:|
| typing_runtime_protocols | 0.132 ms | 3.349 ms | 3.175 ms | 0.0417Ã— |
| gc_traversal | 2.332 ms | 0.778 ms | 0.747 ms | 3.1243Ã— |

Ratios above 1Ã— favor XLang3. The previous protocol result comes from the immediate repr checkpoint; previous GC comes from the AST-owner checkpoint, with another runtime change between it and this candidate. GC improvement therefore does not isolate indexed lookup. The saved CPython reference retains its [historical provenance limitations](data/cpython3147-saved-reference-provenance-audit-20261007.json). Do not splice these two results into the frozen 97-definition run.

## Diagnostic scaling and limits

At 5,000 unrelated live references, 5,000 reuse operations fall from 20.324 to 5.250 ms (3.87Ã— previous/current); dereferencing falls from 7.334 to 1.404 ms (5.23Ã—). Candidate lookup times stay approximately flat across the tested registry sizes. These diagnostic totals include Python loop/call overhead and are not official benchmark scores.

The single-pass inspect primitive diagnostic does not improve: its total rises from 91.345 to 105.137 ms. Native weakref creation of class targets is also roughly unchanged. Do not use the scaling gain to claim a corresponding speedup for every inspect call. Official protocols show only a modest nominal gain. More shared call, class/descriptor and MRO costs remain to be investigated.

Chameleon is not rerun because its preceding verified `bytes.decode` class-exposure failure is unchanged by this source change. That native registration repair remains outstanding, along with the broader full-suite slowdowns and failures.

## Evidence

- [Compiled source identities and validation](data/weakref-registry-index-validation-20261007.json), [passing fixed gate](data/release-weakref-registry-index-fixed-gate-20261007.json), [gate log](data/release-weakref-registry-index-fixed-gate-20261007.log)
- [Official raw timings](data/pyperformance-xlang3-weakref-registry-index-targeted-fast-20261007.json), [full log](data/pyperformance-xlang3-weakref-registry-index-targeted-fast-20261007.log), [run provenance](data/pyperformance-xlang3-weakref-registry-index-targeted-fast-20261007-provenance.json), [comparison values and input hashes](data/weakref-registry-index-official-and-scaling-comparison-20261007.json)
- [Candidate scaling](data/weakref-registry-scaling-xlang3-index-candidate-20261007.log), [candidate primitives](data/typing-protocol-runtime-primitives-xlang3-index-candidate-20261007.log)
- [Preserved preceding Release](data/weakref-index-preserved-control-20261007.json), [CPython fixture reference](data/weakref-registry-index-cpython3147-reference-r4-20261007.json), [control fixture](data/weakref-registry-index-preserved-control-fixture-r3-20261007.log), [candidate fixture](data/weakref-registry-index-xlang3-fixture-r4-20261007.log)
- [Initial control lifetime failure](data/weakref-registry-index-preserved-control-fixture-20261007.log), [initial candidate lifetime failure](data/weakref-registry-index-xlang3-fixture-20261007.log)
