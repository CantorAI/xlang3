# Windows native file paths

The native `builtins.open` and `os.open` implementations now retry a failed
ordinary long path with a private, fully resolved Windows extended path. The
first native open retains its existing flags and cost. Successful short paths,
device namespaces and trailing-dot/space semantics retain their behavior;
the Python-visible filename is not replaced with the private path.

The Windows contract exercises actual files through both native APIs, including
260, 280, 288 and 516 UTF-16-unit paths, Unicode, relative paths, dot segments,
append, exclusive creation and missing files. Independent Win32 controls supply
the files; they do not emulate the runtime. UNC preparation is checked without
claiming a network filesystem test.

The Release build completed with the CPython bridge and executable discovery
disabled (`XLANG3_BUILD_CPYTHON_BRIDGE=OFF` and
`XLANG3_PYTHON314_EXECUTABLE:FILEPATH=OFF`). The selected correctness run passed
40 of 41 contracts: all 14 audited XLang-only semantic/CLI tests and the new
file-path contract passed. The existing extensionless native-module import
contract failed under the dotted `.worktrees` parent, and the preserved accepted
runtime reproduced the same failure. The CPython interoperability wrapper was
excluded because this validation executes XLang3 only. This is not a claim that
every SDK test passes.

The complete default performance gate passed all 11 cases against the fixed
accepted `4aea7d8` Release executable and libraries. Defaults remained 21 paired
repeats, five warmups and a 10% threshold; XLang3 executed the orchestrator.
The run took 86.316 seconds. All 5,841 captured source inputs and all 26 preserved
baseline files remained unchanged. [Original measurements](evidence/windows-native-file-longpaths-performance.json)
and [source-bound scope](evidence/windows-native-file-longpaths-provenance.json)
retain the failure and baseline control.

The exact retained 288-unit schema file now reads successfully with its original
24,042 bytes. That file-open repair does not add the existing xMind SQLite
transaction prerequisite or repair SQLite's separate Win32 database-path
boundary. The unpatched SQLite module still rejected its isolation keyword and
failed to open the 280-unit database. Consumer migration, rollback and installed
xMind acceptance remain separate work.
