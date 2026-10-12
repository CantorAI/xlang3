# SQLGlot parse runtime-call diagnosis (2026-10-06)

## Finding

The unchanged official `sqlglot_v2_parse` body generated the same **7,556
Python call events** under CPython 3.14.7 and the active XLang3 Release runtime.
The largest call counts were `tokens._advance` (1,328), `parser._match`
(1,046), `enum.__hash__` (670), `parser._match_set` (438), `helper.list_get`
(420), and `parser._get_token` (416). This rules out a different SQLGlot
algorithm or a large excess in Python-level call count as the explanation for
the roughly **20x** official parse gap. The remaining cost is in how XLang3
executes these shared Python operations and their object/runtime work.

The Python `sys.setprofile` hook makes the run far too slow for performance
measurement: one instrumented parse reported 0.743 s in XLang3 and 11.5 ms in
CPython. Those values are intentionally excluded from any speed claim. The
call-count comparison is diagnostic only. Raw outputs are retained in
[`the XLang3 profile`](data/sqlglot-parse-call-profile-xlang3-20261006.txt)
and [`the CPython 3.14.7 profile`](data/sqlglot-parse-call-profile-cpython314-20261006.txt).

## Native sample

The repository's user-mode Windows sampler collected 500 instruction-pointer
samples while the same official parse body ran 100 times through the active
Release executable. **381/500 samples (76.2%)** landed in
`xlang3_runtime.dll`, compared with 78 in `ntdll.dll` and 26 in `ucrtbase.dll`.
The active Release build has no matching PDB, so its sampled runtime addresses
cannot yet be attributed to individual C++ functions. The complete sample is
in [`the active Release sample JSON`](data/sqlglot-parse-native-samples-active-release-20261006.json).

Windows Performance Recorder could not start CPU tracing because this session
lacks the system profiling privilege; no privilege or security setting was
changed. The user-mode sampler supplied module-level evidence without moving
or replacing the executable.

## Consequence for the next optimization

The profile directs effort to shared VM/runtime execution cost, not to reducing
SQLGlot's Python calls or editing its pure-Python library. Prior one-off
`CallMethod` lookup, instance-shadow, and call-frame cache trials did not
demonstrate an end-to-end win, so this evidence does not justify repeating
them. The next candidate needs function-attributed Release samples or a broad
runtime change with an ordinary pyperformance A/B result; any retained fast
path must keep the normal Python semantics fallback and document its guard
beside the code.

The workload used pyperformance **1.14.0**, CPython **3.14.7** at
`C:\Python\Python314`, and the shared Python 3.14 benchmark dependency site.
The fixed XLang3 run path remained
`D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe`
(SHA-256 `FF66E309BED7F3226F52F59E06842F448992F31805D54685EA755365CCD2D389`);
its runtime DLL hash was
`395BF96C94508A8E9E326D2C79C427489B729AC84CD244BACD4EDC037F331B1E`.
