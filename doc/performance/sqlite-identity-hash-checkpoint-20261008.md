# SQLite aggregate, identity ownership and String hash checkpoint — 2026-10-08

This checkpoint passed correctness and the unchanged performance gate. The [authoritative terminal receipt](data/sqlite-identity-hash-loop-proof-validation-20261008.json) records `validated`, unchanged source/binary hashes and two complete original official cases. **XLang3 still trails CPython 3.14.7:** SQLite throughput is 0.264x (3.788x slower), and SQLGlot parse is 0.0534x (18.731x slower). This accepts the bounded implementation checkpoint; it does not achieve the overall CPython speed goal.

This checkpoint implements our own native `_sqlite3.Connection.create_aggregate`, corrects generic native cursor progression, repairs generic identity-expression ownership, and preserves exceptions from Python `__hash__`. Python library wrappers and the benchmark's Python aggregate/SQL parsing algorithms remain Python. XLang3 does not load CPython's native module.

The native aggregate bridge gives each SQLite group its own Python instance and dispatches its `step` and `finalize` methods. Registration replacement, empty groups, callback failures, conversion errors and native/SDK lifetimes are checked. Appended host exception take/restore operations preserve the existing ABI prefix and use struct-size negotiation. The native C++ test includes heap-owned counters and a pending-exception/failure-unwind case.

Cursor execution now primes the current row. Fetch copies that row and advances before returning; DONE detaches and finalizes the native statement, while cached column names preserve description. This makes replacement after a one-row fetch possible without forcing a second fetch. SQL/script text is owned before callback-capable cleanup, and every direct same-cursor statement writer is guarded. Error reporting consumes the live statement DB before finalization.

Generic identity cleanup releases only proven last-use expression temporaries. R2 appends an explicit RHS-literal-None branch opcode, preserving old opcode IDs and the existing generic `is` layout; IR codec version 64 rejects older cached IR lacking that provenance. Source expressions, unary-not chains, future reads, aliases, monitoring phases, reentrant finalizers and resurrection retain their required behavior. The subsequent module-load loop proof fixes a separate owner retained after `del item`: a load snapshot can be considered recreated only when its producer precedes all reads and incoming branches cannot bypass it. Read-before-load, bypass edges, narrow/nested loops and SDK-owned register cases retain their owners. Extra proof storage is limited to functions that actually contain module loads and ordinary backedges. The unchanged seven-group Python identity fixture was kept throughout that correction.

The generic `hash` failure path now transfers an existing pending Python exception instead of overwriting it with TypeError. The fixture checks exact exception identity, traceback/cause, handled context and BaseException subclasses; C++ tests exercise ordinary and fast native dispatch with repeated ownership transfers. A separate exact String branch reads the existing immutable hash cache before guaranteed failed numeric-payload attribute lookups. Instances, subclasses and Python `__hash__` callbacks retain ordinary dispatch.

Two private C++ header includes were corrected to `runtime/modules/thread/runtime_lock.h`, allowing the existing interpreter test target to compile using its existing src-root include path. No new CMake target or build directory was introduced. Performance/lifetime comments in the implementation record the assumptions and fallback requirements for future changes.

## Validation evidence

| Required evidence | Prepared status / source |
|---|---|
| Six focused paths | Fresh aggregate 7 groups, cursor 8, unchanged identity 7, hash exception 3, exact String 5 and writer guards 7 passed in the selected receipt. |
| Registered C++ tests | Exactly 9 discovered and passed; the existing interpreter target contains the new ownership/ABI cases. |
| Native SQLite API scripts | Both manually invoked scripts passed their exact expected output; they are not extra registered CTest counts. |
| Full correctness | Selected receipt reports 380 core cases, 11 compatibility sections and 3 expected failures, with the full fixture command exit 0. These are fixture counts, not pyperformance completion counts. |
| Fixed local gate | PASS: unchanged 11 cases, 21 repeats, 5 warmups and 0.1 threshold against the fixed preserved baseline. |
| Original official sqlite_synth | Complete, exit 0: all 20 fresh official values retained. |
| Original official sqlglot_v2_parse | Complete, exit 0: all 20 fresh official values retained. |
| Source/binary provenance | All 31 declared source/test targets pinned; terminal before/after hashes match. |

Official throughput uses **CPython 3.14.7 = 1x** and speed = CPython mean time / XLang3 mean time. Higher is faster. Each cell below comes from 20 measured values; all are retained.

| Original official case | Unit | CPython mean ± sample SD (1x) | XLang3 mean ± sample SD | XLang3 CV | XLang3 speed vs CP | Slower than CP |
|---|---|---:|---:|---:|---:|---:|
| sqlite_synth | us | 1.832987 ± 0.010908 | 6.942670 ± 0.382317 | 5.51% | 0.2640x | 3.7876x |
| sqlglot_v2_parse | ms | 0.988288 ± 0.010773 | 18.511784 ± 1.245476 | 6.73% | 0.05339x | 18.7312x |

Both fresh logs warn that the benchmark result may be unstable because the fast run has insufficient samples for the stated confidence target. XLang3 maxima were 8.258856 us for SQLite and 22.296700 ms for SQLGlot; neither was removed. The saved CPython reference and fresh candidate are sequential historical/fresh unpaired series, so this table does not establish a paired improvement over the previous XLang3 checkpoint.

Raw official values: [SQLite](data/sqlite-identity-hash-loop-proof-validation-20261008-official-sqlite-synth-fast.json), [SQLGlot](data/sqlite-identity-hash-loop-proof-validation-20261008-official-sqlglot-v2-parse-fast.json), [saved exact CPython reference](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json). The [SQLite checkpoint report](sqlite-aggregate-combined-checkpoint-20261008.md) provides its horizontal chart and complete sample CSV.

No diagnostic loop or earlier standalone unaccepted SQLite run supplies these accepted checkpoint values. No full 97-case suite was run for this checkpoint, so its overall completion count or whole-suite win is not claimed.

The already exported [String component report](exact-string-hash-dispatch-20261008.md) remains distinct: cached-string `hash()` improved **1.617x** across seven paired processes, while the fresh original SQLGlot component comparison was **neutral** at nominal 1.0159x (candidate CV 5.69%, warnings and the outlier retained). That component build preceded the final combined ownership proof and is not this checkpoint's official score.

## Preserved failures and limits

The original aggregate setup failure on CPython is preserved: SQLITE_NOMEM rolled back uncommitted setup rows. The only repair was a setup `con.commit()`; the corrected exact CP3147 baseline passed all seven groups. R5 then failed native replacement after a first fetch. The retained-cursor CP/X observation isolated that progression gap. R6 fixed replacement, then exposed a generic weakref-result owner; initial combined identity validation subsequently failed its unchanged loop ownership group. All failures, their raw output and intermediate proposal provenance remain archived. R6 v1/v2 were review revisions, not accepted measurements; v3 included SQL ownership and writer guards.

The original combined CPython seven-API guard probe crashed with `0xC0000005`. Separate exact CP3147 children preserved execute/executemany crashes and five passing operations. Candidate guard assertions pass, but these references do not establish unrestricted CP reentrancy parity. XLang3's lookahead lock remains intentionally stricter while its cursor owns raw native statement storage.

Native SQLite still does not publish registration factory edges through the existing SDK GC-reference facility; factory-to-connection cycles need explicit close. The earlier scalar registration failure double-destroy is separate. Broader DB-API conformance and a CPython speed win are not asserted. Repeated native preparation is a source-backed next candidate: it requires a connection-level cache with safe active leases, binding cleanup, close/replacement semantics and fresh unchanged official measurements.

Evidence publication links the selected terminal receipt, exact final-source inventory, both raw streams per phase, the fixed gate, official JSON/CSV and frozen export manifests. Archive bytes remain exact. The manifests define the report assets to check in.
