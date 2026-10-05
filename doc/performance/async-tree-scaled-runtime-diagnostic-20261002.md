# Scaled asyncio tree diagnostic on rebuilt Release

This is a diagnostic only, not a pyperformance result or a CPython 3.14
comparison. The available CPython runtime in this session is 3.13.7, so both
programs used its standard library for this probe. The full benchmark remains
the authoritative comparison against CPython 3.14.

The repository's `async_tree_scaled.py` was run at four levels, three branches,
and 30 repetitions. The first CPython sample was a warm-up outlier; medians
across all three runs were 0.07464 s for CPython 3.13.7 and 0.4869 s for
XLang3, or 0.153x CPython/XLang3 (about 6.5x slower). The sample count is too
small for a stable score; the raw runs are in
[`async-tree-scaled-cpython313-diagnostic-20261002.csv`](data/async-tree-scaled-cpython313-diagnostic-20261002.csv).

## Empty-star callback fast path

VM counters showed that `Context.run` used its generic callback 291 times and
its register-backed fast callback zero times for one four-level tree. Most
`asyncio.Handle._run` calls use `Context.run(callback, *args)` with an empty
built-in tuple/list, which unnecessarily disabled the fast callback. The VM
now skips expansion only for exact empty list/tuple objects when the native
call has no keyword expansion; custom iterables and subclasses keep the normal
iterator path. The regression fixture
[`context_run_empty_star.py`](../../tests/fixtures/core/context_run_empty_star.py)
checks both empty expansions and a user-defined iterator side effect.

After the change, the same counter probe reported 126 fast and 165 generic
`Context.run` calls. Eight 30-iteration timings had a 0.4727 s median versus
0.4869 s for the three-run control, a **3% directional gain**. A follow-up that
also routed non-empty expanded calls through the fast callback had a 0.5010 s
median across five runs, slower than the narrower candidate, so that broader
path was removed. The empty-only path is a measured but small result, not
evidence of a material suite-wide improvement. Measure the official 3.14
asyncio cases and fixed Release gate when the 3.14 stdlib is readable.

The pre-change XLang3 Release build produced 23,080 `LoadConst`, 22,321 `StoreLocal`,
20,938 `LoadLocal`, 18,915 `JumpIfFalse`, 17,983 `LoadLocalAttr`, 13,439
`Call`, and 9,152 `CallMethod` dispatches for a single four-level tree. The
same run recorded 18,636 native calls. These counts locate substantial work in
the VM instruction stream, but they do not identify which operation accounts
for the elapsed-time gap. The complete counter dump is
[`async-tree-rebuilt-release-counters-20261002.txt`](data/async-tree-rebuilt-release-counters-20261002.txt).

A separate three-level profiling run counted 3,488 Python asyncio call events
under CPython and 3,521 under XLang3. XLang3 invoked
`asyncio.futures._get_loop` 90 times versus CPython's 51. The close total call
counts alongside the much slower XLang3 elapsed time suggest that per-opcode
and call execution cost matters more than entering substantially more Python
frames. This is a working inference from a small scaled workload, not a
profiled attribution. The extra `_get_loop` calls are a measurable difference
to investigate, but their count alone does not explain the overall gap.

The XLang3 process in this session could not read the configured CPython 3.14
standard library. Its environment variable `XLANG3_PYTHON_LIB` points to a
nonexistent per-user Python 3.14 path; the actual `C:\Python\Python314\Lib`
directory is inaccessible to this process. No result here should be used to
refresh the existing 3.14 chart until that runtime path works again.
