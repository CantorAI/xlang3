# Frame-context trial rejected; generic GC discovery reproduced

The frame-context optimization was removed. It improves a branching Python-call diagnostic, but it does not produce a significant gain in rigorous official pure-Python unpickling or the three preselected call-heavy official cases. The exact accepted source137 and Release178 from main `95feaff2` have been restored at the existing run path. No new engine optimization is committed in this checkpoint.

## Mechanism and correctness

The held proposal combines three exported Runtime setters and their cached TLS guards at ordinary VM frame switches. It uses the freshly published top physical frame view only when the owned globals Object is unchanged, updates borrowed locals and identity fields in their previous order, and keeps the original changed-owner setter/finalizer sequence as fallback. It does not change IR shapes, remove inspection publication, reuse owning frame snapshots, or replace Python library bodies. Code comments and meaningful ownership/admission tests are preserved in the rejected source archive.

This is distinct from rejected CurrentGlobalsGuard save/restore, callee-module owner selection, and R6 materialized snapshot reuse. Source review found no concrete safety blocker. Fresh validation passed the CPython 3.14.7 four-group oracle, the matching XLang3 result, 406 core fixtures, 11 compatibility sections, three expected-failure checks, nine selected CTests and both SQLite API checks. The full fixed performance gate was not run for this discarded candidate. Its correctness pass is not an acceptance or speed result; the restored accepted binary retains its previously recorded complete default11 gate pass.

## Balanced diagnostic

The unchanged `python_callback_boundary.py` checks outputs and runs three samples per body over 50000 items. All six permutations of CPython/control/candidate ran serially: 18 processes, 270 timed body values. The table uses the median of all 18 values per runtime/body. These are complete body times, including loop or sorting work; they are not exclusive function-entry CPU costs or official scores.

| Body, ms | CPython 3.14.7 | Accepted X control | Trial X | Trial speed / control | Trial speed / CP |
|---|---:|---:|---:|---:|---:|
| `loop` | 1.3197 | 3.3293 | 3.3676 | 0.9886× | 0.3919× |
| `small_calls` | 1.9448 | 16.1910 | 15.3922 | 1.0519× | 0.1263× |
| `branch_calls` | 2.4383 | 18.0770 | 16.6087 | 1.0884× | 0.1468× |
| `sort_plain` | 0.2524 | 2.1940 | 2.2346 | 0.9818× | 0.1130× |
| `sort_callback` | 2.5335 | 38.4562 | 37.7866 | 1.0177× | 0.0670× |

The branching call body improves by about 1.088× against the accepted X control, while remaining about 6.8× longer than CPython. That local result does not predict a library or whole-suite speedup.

## Original official cases

All original benchmark definitions and workloads are unchanged. The same CPython 3.14.7 manager, XLang3 Release workers, compatibility hook and dependency site are used. Each four-score sequence is control/candidate/candidate/control. Fast mode retains 20 scored values per case/score; rigorous unpickling retains 120. Warmups/calibration are retained in the original pyperf JSON but excluded from scored values.

The ratio below is control time / candidate time: above 1× favors the candidate, below 1× favors the control. Pair 2 reverses execution order. Every pyperf comparison for rigorous unpickling and the three fast call-heavy cases is hidden as statistically insignificant. DeltaBlue's descriptive means are slower in both orders; Richards changes direction. No official speed gain is claimed.

| Case | Pair 1 speed / control | Pair 2 speed / control |
|---|---:|---:|
| unpickle fast | 1.01248× | 1.00412× |
| unpickle rigorous | 0.99757× | 1.00594× |
| `deltablue` fast | 0.98203× | 0.97275× |
| `hexiom` fast | 1.01299× | 1.00979× |
| `richards` fast | 0.98531× | 1.00546× |

No additional run was used to search for a favorable score. The initial fast unpickle screen justified a rigorous check; its apparent small gain did not survive that check. All 1070 scored timing values are in [CSV](data/frame-context-coalescing-all-timing-values-20261009.csv), together with all original JSON, warnings and logs. The previously published full97 comparison remains its separate 75-completed/22-failed capture; this experiment does not replace its chart or aggregate.

## Confirmed next failure: unreachable cycles

A separate, untimed CP-first reproduction creates a reachable `Node` self-cycle and an abandoned `Node` self-cycle. No weakrefs or finalizers seed the collector; only the abandoned node's integer ID is retained. After collection, both runtimes preserve the reachable cycle. CPython 3.14.7 returns 1 and the abandoned node is absent from `gc.get_objects()`. The accepted XLang3 returns 0 and still lists that node, then fails the identical assertion. [Actual receipt](data/gc-unreachable-instance-cycle-untimed-20261009.json).

Current `gc.collect` delegates to `weakref_collect_cycles`. That collector starts from selected weakref/native/local-class candidates, excludes module-rooted classes, and does not seed the general tracked instance/list heap. The generic tracked-object snapshot is used by object inspection but not collection. The minimal result corroborates the retained source diagnosis and explains why the official `gc_collect` requirement cannot be dismissed as harness noise. It is not a performance score, and repairing this discovery gap must still preserve external roots, native references, finalizers and reentry. No collection-count workaround or GC engine change is included here.

The full97 report already withholds `gc_traversal` ratios because its zero-count assertion cannot establish equivalent collection work while discovery is incomplete. That exclusion remains; the next repair must validate the original collection and traversal benchmarks.

## Earlier unpickle call counts

The separately preserved accepted-build profile confirms 20200 `pickle._Unframer.read` Python calls in one original body, matching the September30 diagnosis; the retained BytesIO fast-call adapter is already present and was not reimplemented. The original body performs 60 loads with official `inner_loops=20` normalization. Profiled Python counts are 33385 for CPython and 33427 for X; profiling disables optimizations and does not attribute unprofiled CPU time. X native profile arguments are often name strings, which this collector did not decode, so its unsupported native-name bucket is not comparable with CPython's callable-object native counts. [Diagnostic receipt](data/original-unpickle-call-counts-20261009.json).

## Provenance and limits

The accepted checkpoint was copied once before edits; source140/Release178 of the rejected trial were preserved before restoring source137/Release178. Both complete Release maps, the fixed baseline177 and protected unrelated working files match their receipts. The restoration copied exact preserved bytes to the original directory; it was not a new build. Trial object files remain in the existing Ninja directory; restored source mtimes require rebuilding before a future candidate is timed. The selected source inventory is partial worktree provenance, not proof of a clean-checkout reproduction.

Every timing phase has pre/post idle checks, one-second compiler/tool observations, raw observations, no recorded overlap/scanner errors and stable input hashes. Sub-second processes can escape sampling; this does not claim an otherwise unused computer. Official rigorous and broad screens pin the original benchmark/Python source inputs. The initial fast unpickle screen lacks those extra source pins, so it is retained only as an exploratory screen, not acceptance evidence. A brief untimed pyperf comparison of the completed rigorous results was also issued after the broad manager launched; it is not another workload measurement or proof of fully exclusive CPU use.

Exact rejected owned source files are archived under `data/frame-context-coalescing-trial-sources-20261009`; controllers, the held proposal/history and unchanged diagnostic source are under `data/frame-context-coalescing-evidence-20261009`. Historical scratch paths inside controllers refer to the original workspace; source/binary hashes and raw receipts remain authoritative. A future rerun requires new output names and current provenance, not blindly replaying a historical controller.

The preceding goal turn was a status clarification and is classified as no progress. This turn applied and measured a distinct held trial, rejected it based on official evidence, restored the accepted checkpoint, and reproduced a concrete remaining GC failure. The overall goal of materially reducing full-suite slowdowns/failures versus CPython remains active and unfinished.
