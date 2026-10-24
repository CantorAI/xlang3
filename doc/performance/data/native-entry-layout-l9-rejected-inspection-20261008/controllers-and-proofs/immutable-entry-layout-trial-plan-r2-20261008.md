# Isolated immutable entry layout trial

Held, root-executable proposal. No agent execution, AST check, build, application
or live edit occurred. Candidate is the reviewed setup-only patch
`native-python-entry-immutable-layout-proposed-20261008.patch`, SHA
`f5a1cf0588369abe0ed15e007dbf34fb101eafabca6d50ac08b4df4a83950e83`,
with provenance `bde7e4fa9b891e60eef8886107675df34c0e6905b94259c59a6c2574b7e2e797`.
The rejected empty Interpreter wrapper is absent.

Parent: S8 source110 receipt `f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556`;
full correctness/gate validation `d4b028b1255f23b11e664bd9e6778b808a7d160d3628e17f39710faa1900d022`.
Immutable S8 control is
`build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008`,
manifest `67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56`,
with 178 Release files and 110 raw source snapshots. Its historical label says
pyflate pending; the exact preserved bytes remain the S8 comparator. Do not
replace it with a later trial or rewrite that manifest.

## Execution order and declared decision

1. Finish S8 publication. Root checks the five actual source inputs, copies only
   six reviewed candidates, and records actual source111/build log. Root runs
   the existing fixed-path Release build; no reconfigure, worktree or run-path
   change. Build must actually finish successfully before the focused command.
2. Run the registered public DLL CPP proof first, then sixteen existing Python
   boundary fixtures. The helper proves actual metadata admission and
   A→uncached→B→A same-depth restore in 32 fresh callback entries, code mutation,
   copied-code owner rejection and absence of retained argument/function roots.
   Existing sparse storage checks retain dense monitoring behavior. Relevant
   generator/debug/monitoring tests are freshly run. Stop on any failure.
3. Run the unchanged callback boundary diagnostic in seven alternating S8/trial
   pairs: five cases, three 50000-operation samples per case per child, 210 raw
   timing values. The native sorted Python-key case is the target; direct branch,
   loop, tiny-call and no-key sort cases remain controls. No CP timed child.
4. Run one unchanged original pprint body under CPython 3.14.7 for fresh parity,
   then seven alternating S8/trial pairs (14 XLang children). Each invokes the
   original 100000-item aliased `_safe_repr` body once; signature remains exactly
   4200000 characters/SHA15c2695. All 15 children have cap300, no profiler, no
   repeats or workload substitutions. The paired body is an affected-workload
   diagnostic, not an official score or CPython speed claim.
5. Run the unchanged original pure-pickle CP/trial parity body as a control using
   Suite's separately frozen runner. Original 41 loops/2460 protocol5 dumps stay
   fixed. A neutral pickle time does not reject a callback/pprint improvement.

The affected pprint decision is predeclared: median of seven within-pair
S8/trial elapsed ratios **strictly above1.02**, with lower bootstrap95 endpoint
**strictly above1.0**. Bootstrap resamples50000, seed20261008; all paired ratios
retained. Callback sort-key signal is supporting evidence with the same
threshold; it cannot alone establish a full-library win. Callback negative
controls must have median ratios at least1/1.05 (no more than5% slower).
Pure-pickle parity and all focused outputs must match. No trimming, retries,
replacement values, expanded run counts, automatic acceptance or rollback.

Only a useful affected-workload result and passing controls justify fresh full
correctness/CTest/API validation and the unchanged default gate11/21/5/.1.
Then run the unchanged official pprint case with the existing cap300 and20
values. A timeout/inconclusive gate does not count as acceptance. Root decides
whether the original official result warrants retaining the runtime change.

## Controller pins and exact commands

Focused controller:
`check-immutable-entry-layout-focused-r2-20261008.py`, SHA
`4b5d28ec1fd6ec59ebfbc9b7baa40f2c3b879e5f332da0cacf7ef0806fb42517`.
Measurement controller:
`measure-immutable-entry-layout-paired-r2-20261008.py`, SHA
`640725fd35a8f062333332ca6f2dabbac76735f5412dc798d65af1fe245c0b7a`.
Both require exact actual future inventory/focus hashes, complete current/control
Release trees, successful recorded public eligibility and source identity.
Their root-only CP manager rejects optimized or non3.14.7 execution; binary
stdout/stderr, partial/timeout evidence and final drift status are retained.
The measurement reuses the frozen external-build timing watcher50007db5.

Set actual root-produced paths/hashes below after the fixed build. None is a
predicted candidate binary or future receipt hash:

```powershell
$layoutInventory = 'D:/CantorAI/xlang3/doc/performance/data/ACTUAL-layout-compiled-source.json'
$layoutInventorySha = (Get-FileHash -Algorithm SHA256 -LiteralPath $layoutInventory).Hash.ToLowerInvariant()
$layoutBuildLog = 'D:/CantorAI/xlang3/doc/performance/data/ACTUAL-layout-Release-build.log'
$layoutBuildLogSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $layoutBuildLog).Hash.ToLowerInvariant()
& 'C:/Python/Python314/python.exe' -I -u 'D:/CantorAI/xlang3/scratch/performance/check-immutable-entry-layout-focused-r2-20261008.py' --source-inventory $layoutInventory --source-inventory-sha256 $layoutInventorySha --build-log $layoutBuildLog --build-log-sha256 $layoutBuildLogSha --prefix immutable-entry-layout-focused-r2-20261008
$layoutFocus = 'D:/CantorAI/xlang3/doc/performance/data/immutable-entry-layout-focused-r2-20261008.json'
$layoutFocusSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $layoutFocus).Hash.ToLowerInvariant()
& 'C:/Python/Python314/python.exe' -I -u 'D:/CantorAI/xlang3/scratch/performance/measure-immutable-entry-layout-paired-r2-20261008.py' --mode callback-pairs --source-inventory $layoutInventory --source-inventory-sha256 $layoutInventorySha --focused-receipt $layoutFocus --focused-receipt-sha256 $layoutFocusSha --prefix immutable-entry-layout-callback-paired-r2-20261008
& 'C:/Python/Python314/python.exe' -I -u 'D:/CantorAI/xlang3/scratch/performance/measure-immutable-entry-layout-paired-r2-20261008.py' --mode pprint-pairs --source-inventory $layoutInventory --source-inventory-sha256 $layoutInventorySha --focused-receipt $layoutFocus --focused-receipt-sha256 $layoutFocusSha --prefix immutable-entry-layout-pprint-paired-r2-20261008
```

Direct public eligibility command, executed by the first focused phase:
`D:/CantorAI/xlang3/build-repro/main-verify-20261006/Release/xlang3_interpreter_tests.exe`.
Python phases use the fixed absolute XLang executable and unchanged core fixture
paths. Controllers retain fresh source/expected pins and exact transcript checks.
Build log is pinned provenance; its text is not misrepresented as a parsed build
result. Public CPP plus changed runtime/CPP hashes guard against stale S8 tools.

Frozen diagnostic sources remain unchanged: callback boundary `f7613659`,
original pprint child `5d57809b`, benchmark `07ef5720`, stdlib pprint `c29eb77a`;
pure pickle child `5295c899`, benchmark `31c0e30b`, stdlib pickle `144fdf59`.

Correctness-only R2 preserves the phases-empty eb61 preflight refusal. It logs
permitted MSBuild workers while retaining other build/runtime name guards and
source/Release identity. All seventeen tests remain fresh. Timing name/activity
guards and watcher remain byte-identical; an active/unresolved worker can still
block measurements. Do not kill or suspend Visual Studio.
