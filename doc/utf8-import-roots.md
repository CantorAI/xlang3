# UTF-8 embedding import paths

The C API's filesystem paths use UTF-8 on Windows. `x3_runtime_add_import_root`
and the parent root introduced by `x3_runtime_eval_file` now decode them with
`std::filesystem::u8path`, rather than the Windows ANSI conversion. Import-root
normalization, pure-source lookup/cache paths, native package diagnostics and
module/package-host path metadata retain UTF-8 when crossing string interfaces.

This fixes a Unicode runtime-directory failure in xMind: its native SQLite
package was missed, causing lookup to reach the pure SQLite wrapper and fail on
`_sqlite3`. The correction retains the existing native package and ABI; it does
not execute CPython or substitute a Python implementation.

The added C ABI regression uses a unique pure module and a copied actual JSON
native package under an owned Unicode directory. Its unique native package name
cannot resolve through an implicit SDK module directory. It checks native path
metadata and a separate source-file sibling import through `EvalFile`.

The complete Release build passed with the CPython bridge disabled. The focused
Unicode/native-package/Windows-file checks passed. Of 58 registered checks, five
explicit CPython peer checks were excluded by the xMind user's instruction.
The remaining 53 ran: 51 passed, and native-library-path plus the Visual Studio
launch configuration checks failed. Both failures were reproduced using the
unchanged baseline and are retained; no full 53-check success is claimed.

The complete default performance gate ran under xlang3 against the preserved,
fixed accepted Release baseline. All 11 cases passed with 21 paired samples,
five warmups and the unchanged 10% per-case threshold. Seven changed-source
hashes remained frozen through build, correctness and performance validation.
The report and exact local receipt hashes are in the accompanying evidence.

The follow-up working-directory fix also retains UTF-8 in initial `sys.prefix`,
standard-library prefix paths, CLI `sys.path` publication and module-source
metadata. A VS Code launch with an actual Unicode working directory exposed
ANSI conversion during runtime initialization even when the package itself
used ASCII paths. The C ABI regression now creates its runtime inside the
owned Unicode directory and checks native/source imports and prefix metadata.
The CLI probe checks Unicode `os.getcwd()` and its correct empty first path
entry for `-c`. The embedding C ABI does not automatically publish `sys.path`;
that remains a separate compatibility limitation rather than a claim made by
this regression.

The follow-up Release build and all three focused checks passed. Its allowed
SDK suite again ran 53 checks with 51 passes and the same two failures,
independently reproduced with the preserved unchanged `5e86144` Release.
All 11 default performance cases passed against the same fixed accepted
baseline, with 21 paired samples, five warmups and the unchanged threshold.
Four mapped source files remained unchanged through validation. Failed test
assumptions and the performance guard metadata error remain recorded in
[working-directory validation](utf8-working-directory-local.json).
