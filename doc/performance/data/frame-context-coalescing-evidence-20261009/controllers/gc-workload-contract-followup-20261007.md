# Distinguish retained traversal from unreachable cycle collection

The live all-97 run reproduces gc_collect's AssertionError. The official
workload creates 100 cycles with 21 Node instances each, deletes the containing
root list, and requires gc.collect() to return None or a count at least 2,100.
The assertion fails; the actual returned count still needs a direct observation.
Do not change the assertion,
the workload, or the reported return count merely to make it pass.

The separate gc_traversal workload keeps a 1,000-level nested container graph
reachable and times collection after a preceding untimed collection. It asserts
None or zero objects collected. A nominal faster timing there is not proof of
unreachable cycle reclamation; qualify it alongside the failed collection case.

After both live suites are terminal/preserved, inspect native gc_module.cpp and
the generic object-reference traversal, plus ordinary VM/register/cache owners.
Use weak-reference/finalizer observations and genuine heap lifetime evidence
to distinguish retained roots, missing traversal edges and wrong counts. A
reference-counted runtime still needs correct behavior for exposed gc.collect;
do not remove cycle handling or justify a fast score by skipping work.

The fix must preserve reachable objects, reentry/finalizer safety and native
references without retaining stale cache Values. Keep Python gc benchmark code
unchanged. Full correctness, unchanged fixed default Release gate and affected
official collection/traversal cases remain required. No probe, engine edit or
competing measurement is authorized while session 47398 is still measuring.

Read-only inspection now identifies a concrete collector coverage gap:
gc_module.cpp gc_collect (around 184-195) delegates to weakref_collect_cycles.
That implementation in weakref_module.cpp (around 1574-2045) seeds local class
components, weakref-target classes/functions/files/instances, and registered
native payload instances. Its instance expansion follows those candidates.
It does not seed from gc_snapshot_tracked_objects or otherwise enumerate the
generic tracked instance/list heap. Searches show the tracked-object snapshot
is used by gc.get_objects/get_referrers paths, not gc.collect.

object_model.cpp registers native GC candidates only when native traversal or
native reference/clear hooks are installed. Ordinary Python Node instances in
bm_gc_collect install no such hooks or weak references; their module-rooted
Node class is explicitly skipped as a rooted class. Likewise gc_traversal's
rooted list graph has no weakref/native-payload candidate seed. The collector
thus lacks the generic graph discovery these workloads require. Capture actual
returned counts and weakref/lifetime observations after the live reference run,
but do not treat the source-confirmed skipped graph traversal as a fair speed
win while cycle collection is incomplete.

For the new comparison, retain gc_traversal's completed worker status and raw
time, but withhold its CPython ratio, chart bar, win count and geometric-mean
contribution with an explicit correctness exclusion. This is distinct from a
timeout/failure: the workload's zero-count assertion alone does not certify
equivalent collection work. Keep all 97 definitions and all raw samples.
Repair generic candidate discovery and ownership accounting; do not add fake
collection counts or attach weakrefs to the benchmark merely to seed the
existing selective collector.
