# SQLite repeated preparation diagnosis — 2026-10-08

The next bounded native performance candidate is a **connection-level prepared-statement cache**. Current own `_sqlite3` prepares the identical INSERT for every `Connection.execute` in the original `sqlite_synth` loop. A cache on Cursor alone would miss: each connection shortcut creates a new cursor. No implementation, compilation, fixture, timing probe or benchmark was executed for this diagnosis.

The supplemental original official case completed 20 values at about 6.69 us/loop versus the saved CPython 3.14.7 reference at about 1.833 us/loop, approximately 3.65x slower. Its terminal receipt explicitly says unaccepted while full identity correctness and the unchanged gate are pending. These results establish the remaining gap, **not the amount caused by preparation**. Raw inputs: `doc/performance/data/sqlite-r6-identity-r2-official-sqlite-synth-20261008{.json,.log,-receipt.json}`. No fresh full-suite completion count follows from this one case.

| Current source fact | Location |
|---|---|
| Connection stores db, refcount and closed state, with no prepared SQL cache. | `modules/sqlite/sqlite_handles.h:30` |
| Connection.execute allocates a fresh cursor before delegating to cursor_execute. | `modules/sqlite/sqlite_package.cpp:1225` |
| cursor_execute owns/copies SQL, derives operation metadata, and prepares anew. | `modules/sqlite/sqlite_package.cpp:1149`, `:1160`, `:1171` |
| DONE releases the statement through detached sqlite3_finalize. | `modules/sqlite/sqlite_package.cpp:1067`, `:1217` |
| Every executemany parameter row also delegates to cursor_execute, thus prepares anew. | `modules/sqlite/sqlite_package.cpp:1383` |
| Parameters go through SDK len/getitem per item; strings/blob values copy into SQLite with SQLITE_TRANSIENT. | `modules/sqlite/sqlite_values.cpp:59`, `:66`, `:74` |

The installed unchanged original benchmark (`C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_sqlite_synth/run_benchmark.py`, SHA256 `dcba7a6889e66f08480f4332860208d624678fc42a03be5c8f3b6f1f894231fd`) executes one CREATE, N identical INSERTs, one scalar SELECT, one aggregate SELECT and one DELETE on one connection. The supplemental JSON records N=16384. In a successful invocation the current explicit cursor path therefore prepares N+4 statements, including 16384 INSERT prepares. This source-derived count excludes implicit transaction SQL and SQLite internal reprepare; it is not an observed profiler count.

Exact tagged CPython 3.14.7 comparison:

- A connection creates an LRU statement cache, default `cached_statements=128`; explicit close clears the cache before closing its DB. [connection.c:144–162, 208, 262–288, 667–669](https://raw.githubusercontent.com/python/cpython/v3.14.7/Modules/_sqlite/connection.c)
- Cursor obtains SQL from the connection cache. A busy cached statement gets a fresh instance; a reusable statement is reset. After DONE the cursor drops its reference while the cache can still own it. Consequently the sequential identical INSERT can reuse one cached prepare across fresh shortcut cursors. [cursor.c:489–495, 792–816, 888–923](https://raw.githubusercontent.com/python/cpython/v3.14.7/Modules/_sqlite/cursor.c)
- Creation prepares SQL once, stores DML classification with the native statement, and finalizes when that Statement object's ownership ends. [statement.c:30–105](https://raw.githubusercontent.com/python/cpython/v3.14.7/Modules/_sqlite/statement.c)

This is a source-backed reason to prioritize prepared-statement reuse. It does not prove the remaining Python aggregate callback, call-frame or parameter-conversion costs are small, and does not justify native replacement of the Python `AvgLength` algorithm.

## Bounded candidate contract after current validation freezes

Use owned SQL bytes as the exact cache key on Connection, with a bounded idle cache and an explicit checked-out lease. Keep the existing raw statement pinned by its active cursor. A statement leased to another cursor, currently binding parameters, or inside a callback is unavailable even before sqlite3_stmt_busy becomes true; prepare a separate statement in that case. Detach a cursor's statement before any callback-capable reset/finalize, just as R6 already requires.

Return only successfully reset, nonbusy, usable statements to the open originating connection. Clear bindings before reuse so zero/partial/new bindings cannot read stale previous values; the current uncached implementation's parameter-count behavior must not silently change through cache reuse. An error, reset failure, closed connection or invalidated lease goes through finalization. Preserve cached description, lookahead error behavior, rowcount and lastrowid, and all existing writer guards. SQL NUL/tail validation and broader DB-API parameter compatibility are separate existing gaps, not permission to extend this trial indiscriminately.

On connection close, atomically detach the idle cache and publish closed/db-null state before callback-capable native teardown. Finalize every detached idle statement; active leases keep their statement/SQLite DB alive until normal completion and must never reenter the closed cache. Otherwise a reset-only cache would reintroduce close-v2 zombie DB retention and prevent the original seven-group aggregate fixture's immediate factory cleanup.

Function/aggregate replacement and registration failure must preserve the existing original fixture's exact factory lifetime, replacement semantics and exceptions. Schema changes must still work through SQLite's v2 reprepare. Keep all cache container/key/eviction iterators out of callback-capable reset/finalize regions: nested SQL or a factory destructor can mutate or close the connection. A minimal first trial may cache only safe zero-column completed statements while retaining finalization for callback/row-producing statements, but it must be described as that limited policy and measured against the unchanged original benchmark.

## Meaningful verification plan, not executed

Add native C++ assertions on actual prepare/reuse identity or a test-only prepare count: repeated identical INSERT across different connection shortcut cursors should prepare once, varying SQL should prepare separately, and active same-SQL leases should not alias. These are eligibility/resource assertions, not timing mirrors.

Preserve the unchanged original seven aggregate and eight cursor groups, real-host C++ ownership/unwind cases, and seven guarded writer operations. Add bounded cases for two overlapping same-SQL cursors; nested same-SQL execution during parameter binding; repeated bindings including omitted/failed parameters; close while an active cursor remains; factory replacement and close releasing callback references; schema change/reprepare; and eviction/finalizer reentry. Keep exact CP3147 outputs for new observable cases, with no repeated unchanged reference runs.

After correctness passes, run the unchanged default 11-case / 21-repeat / 5-warmup / 0.1 gate and fresh original official sqlite_synth. A prepare-only diagnostic can distinguish cache benefit from Python callbacks, but cannot substitute for that official case. No promised speed multiplier before those measurements.

## Source pinning

| Own source reviewed | SHA256 |
|---|---|
| modules/sqlite/sqlite_package.cpp | e4349e80b5c519b290853c1ac80443a5351f4d4f9def41449af71c3d1178457d |
| modules/sqlite/sqlite_handles.h | 14bac0cb256d3fd86fd9422ba7b423cb16ae6a5185c714d5f71fa84a34e8c620 |
| modules/sqlite/sqlite_handles.cpp | 115afd05d291841df78d0accfb033ebb53c46bffa9344bcadb8f49c4939180ca |
| modules/sqlite/sqlite_values.cpp | c691c90ba6b7a43c12bce47dfef75101b22b76732303a196ac30b9d6ad3533dc |

No local exact CPython checkout was found in the initially searched workspace. The comparison above uses the official `python/cpython` v3.14.7 native sources directly; no CPython native module was loaded into XLang3.
