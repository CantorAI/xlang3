# Windows native SQLite paths

This follows the [native file-open checkpoint](windows-native-file-paths.md).
The SQLite package now opens ordinary long Windows database paths through the
locking `win32-longpath` VFS. Four native syscall adapters retain the original
call first and retry only a failed ordinary long path using the established
Windows file-path helper. Initialization validates the pinned SQLite 3.48 VFS
and restores original hooks if installation fails partway through.

The syscall table is shared by the Win32 VFS implementations inside this
package DLL. Default VFS registration, locking callbacks, public canonical
filenames, explicit namespaces and URI policy remain intact. Python connections
still have URI parsing disabled; the native C contract explicitly enables URI
parsing for its read-only control. The full-path wrapper fails closed at an
ambiguous buffer boundary instead of opening a truncated filename.

The existing xMind transaction/text prerequisite is applied separately and
bound by its original patch hash. The actual package contract checks
`isolation_level`, rollback, embedded-NUL/Unicode text, the legacy Database API,
relative and extended paths, and memory databases. Independent native C checks
exercise a 280-unit database, Unicode paths exceeding 1,040 UTF-8 bytes,
two-connection write locking, journal/WAL/SHM access, checkpoint/deletion and
read-only URI behavior. Empty-filename temporary databases and network UNC
databases are outside this fixture's scope.

The Release build used Visual Studio 18 x64, OB3 enabled and PGO disabled, with
both CPython executable discovery and the bridge explicitly disabled. The
complete selected correctness run reached **41/42** in **39.17 seconds**:
all 28 direct native executable contracts and 14 audited XLang-only CLI tests
ran, with no skips. The new SQLite contract passed in 0.28 seconds. The remaining
extensionless-import failure under the dotted worktree parent was independently
reproduced with the fixed accepted runtime. Ten registered orchestration wrappers
were outside this selection, including the forbidden CPython pickle wrapper.

The unchanged closed consumer copy passed at the original **288-unit schema
path** and **280-unit database path**: all 24,042 schema bytes and 44 exact type/SQL
definitions matched in query-only mode. Original and copied database, WAL, SHM
and schema bytes remained unchanged.

Both initial 40/42 fixture failures remain recorded: the first required sidecar
deletion after a read-only WAL close; the second left SELECT cursors live across
a reopen and produced the captured `SQLITE_BUSY` return. The repairs retain the
original assertions and explicitly perform a writable checkpoint and close each
fixture-owned SELECT cursor. The initial complete performance run returned
inconclusive and is retained unchanged. A fresh complete **11/11** run passed in
**122.148 seconds**, using the same 26-file accepted baseline, 21 paired repeats,
five warmups and 10% threshold. All 5,849 captured inputs were unchanged during
the accepted stages.

[Provenance](evidence/windows-native-sqlite-paths-provenance.json),
[correctness](evidence/windows-native-sqlite-paths-correctness.log),
[original LastTest](evidence/windows-native-sqlite-paths-last-test.log),
[first failure](evidence/windows-native-sqlite-paths-initial-correctness.log),
[second failure](evidence/windows-native-sqlite-paths-second-correctness.log),
[inconclusive performance](evidence/windows-native-sqlite-paths-performance-inconclusive.json),
[passing performance](evidence/windows-native-sqlite-paths-performance.json) and
[closed-copy result](evidence/windows-native-sqlite-paths-retained-query.log)
preserve the distinct outcomes. This local SDK evidence does not establish a
paired xMind release or an installed-preview upgrade; the primary SDK and
managed preview were unchanged.
