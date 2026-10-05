# Runtime fixes for the CantorAI packages

WorldSense runs directly on XLang3. The Mac package needed native POSIX APIs,
ctypes calls, proxy discovery, correct platform module availability, enough
worker stack for nested VM imports, and standard-library discovery when an
embedded host loads the runtime from `bin/lib`.

Correctness evidence: all seven new platform/thread fixtures passed on Mac and
Windows. Eleven additional Mac JSON, ctypes and FastAPI/Pydantic checks passed.
The Intel Mac WorldSense package returned HTTP 200 under a direct XLang3 child;
duplicate start reused the supervisor, stop killed the Cantor it started, and
stop preserved an independently started Cantor. The ARM64 cross-build completed
and its package audit checked/signed 301 Mach-O files. Native ARM execution and
final official GitHub builds remain outstanding.

## Performance results and commit authorization

The complete default Windows Release gate passed all eleven cases (21 repeats,
five warmups, unchanged 10% tolerance). Candidate runtime hashes were checked.

The preserved September Mac runtime produced incorrect `json_dumps` output:
`2 75 330 332000`, versus `2 75 312 314000` for current XLang3 and CPython. Its
invalid report is retained here; the old runtime remains preserved on the Mac.
The user explicitly approved a separate immutable baseline built from pre-fix
revision `532cf9842c02d39d96dc1864130461a208896750`. Both builds used Release,
Chromium Clang 23, SDK 15.5, x86_64/minimum 13.3 and effective `-O2`.

Two full idle Mac comparisons against that accepted baseline passed nine cases
and remained **inconclusive**, not passing, for scalar arithmetic and range
loops near the 10% limit. The VM object hashes were identical between baseline
and candidate. No threshold, benchmark cases, workloads or expected outputs
were weakened. Earlier overlapping runs were discarded.

After these results were explained, the user directed that performance
investigation was outside the current task, then explicitly requested committing
and pushing the fixes so another Codex worker could sync and merge them. This
commit follows that user direction overriding the repository's normal passing
gate prerequisite. It does not claim the Mac performance gate passed or August
performance was restored. The reports are preserved for later investigation.
