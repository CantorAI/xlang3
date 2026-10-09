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
