# WorldSense release GC module-root lookup trial — 2026-10-15

This is an unmeasured draft source trial. The new native code has not yet been
built or accepted. The fixed performance gate, baseline and workloads remain
unchanged; receiving installations remain held.

## Evidence and hypothesis

Workspace `1813203c8c7fa08538b93b362a1392152d48e55a` with XLang
`2be6f359823a48688796493d5f9792f4d9c2e15e` passes the Windows, Linux x86-64,
Intel/ARM64 Mac and Linux ARM64 GitHub builds. The accepted fixed baseline is
workspace `20827bcaa4846728cf152fec16a2e0e572461dc7`, whose XLang pin is
`8d6285634ad20f377d539db9b43960e02ce42b82`. Both complete 11-case performance
runs on idle CantorAI003 ended inconclusive: ten cases pass, while GC's four
attempts range from 1.102x to 1.110x candidate/baseline elapsed time. These are
unaccepted results, not grounds for changing the baseline or repeating until a
passing sample appears.

An unscored profile used the exact downloaded Windows executable/runtime and
matching GitHub CI PDB on the Intel N100. It sampled only the owned process's
primary thread, identified before process start. The original 1000-level graph
was retained, with 12000 collection calls for sampling. This changes diagnostic
duration only; it does not modify an acceptance workload. Of 25377 samples,
16396 resolve into the runtime with zero unresolved runtime symbols. Notable
counts are `class_has_module_root` 3759, `collect_isolated_class_component`
2835, and vector initialization 4417. The class-root predicate accounts for
about 14.8% of all samples and 22.9% of resolved runtime samples.

[Profile summary](data/worldsense-gc-root-profile-20261015.json) records exact
binary hashes and scope. The first report check incorrectly expected graph
dimensions 1000/999; the saved output is 999/998, matching the unchanged fixed
workload's normalized stdout SHA256. The report was recovered from existing
raw samples without repeating the workload. Earlier captures with ambiguous
thread selection are excluded.

The predicate currently scans module slots for every class and class edge.
The trial first checks the existing `name_to_slot` index for the class's
current name and verifies exact object identity. A miss or rebound value still
uses the full existing scan of slots, extra globals and namespace dictionary.
There is no root cache across mutations, added per-instruction work, changed
collector discovery, removed ownership edge or new CPU instruction requirement.
The hypothesis is that common positive module bindings avoid repeated linear
scans, reducing GC cost while preserving alias and mutation behavior.

## Prior work and validation

The accepted module-root filtering mechanism is documented in
[the September 29 profile](data/gc-traversal-module-root-profile-20260929.txt).
[Generic cycle discovery](generic-cycle-discovery-and-scan-cost-20261009.md),
[coverage tracing](coverage-native-tracer-trial-20261010.md),
[MRO ownership](mro-gc-edge-fastpath-trial-20261010.md) and
[attribute dispatch](instance-class-attribute-dispatch-trial-20261010.md) retain
previous cumulative GC costs and failed/inconclusive trials. This trial keeps
those correctness features and the existing bounded graph indexing.

The existing class-component fixture now asserts canonical roots, class
renaming, canonical-name rebinding with an alias, dictionary-only roots, and
collection after the last alias is removed. Its expected transcript is
unchanged. CPython 3.12 and the downloaded unoptimized 1813203c release both
pass these assertions before the source trial is built.

Required next validation is the GitHub-built candidate's complete correctness
suite, native contracts and unchanged 11-case fixed gate (21 pairs, five
warmups, 10% tolerance), followed by the original affected GC benchmark.
Neither a focused diagnostic nor this draft is a performance acceptance claim.
