# Generic exact-positional call trial (2026-10-06)

## Result

I tested caching the proven positional signature at warmed ordinary and bound
Python `CALL` sites, then entering the normal VM frame without repeating the
signature checks in the argument binder. The candidate did not significantly
improve either of the two call-heavy pyperformance cases:

| Benchmark | Fixed Release control | Candidate | Relative speed |
|---|---:|---:|---:|
| `pickle_pure_python` | 5.16 ms ± 0.06 ms | 5.16 ms ± 0.08 ms | 1.000× (not significant) |
| `richards` | 349 ms ± 19 ms | 346 ms ± 13 ms | 1.009× (not significant) |

`pyperf compare_to` hid both comparisons as statistically insignificant. The
candidate reported fast-mode stability warnings. This rejects repeated
signature validation on generic `CALL` sites as a useful standalone target for
these workloads; reducing function-call setup alone did not move their total
runtime measurably. The candidate was reverted.

The experiment validated exact code identity through the function's module and
function id, and fell back to ordinary argument binding when the code changed
or the call shape used keywords or expansions. Exact full-arity calls may
ignore mutable defaults because every parameter is already supplied. The fast
frame path used the normal frame push, retaining recursion checks, monitoring,
tracebacks, and the standard execution loop.

## Validation and build identity

The Release candidate passed `xlang3_runtime_value_tests`,
`xlang3_interpreter_tests`, and the complete fixture runner using
`C:\Python\Python314\python.exe` (CPython 3.14.7). The fixed Release pair has
been restored:

- Executable SHA-256: `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548`
- Runtime DLL SHA-256: `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D`
- Candidate executable SHA-256: `857E32F9678E882B352C6A30D1291DE43ECFE55987DAEAE89A850AC3732211D1`
- Candidate runtime DLL SHA-256: `8B52348225D7B5E3E7391696721ED62A4C7094C593D143D920CDAEB329163773`

Both comparisons used pyperformance 1.14.0, Python 3.14.7, the same Windows
compatibility shim and dependency site, and `--fast` mode. The retained
candidate JSON covers `pickle_pure_python` and `richards`; the matching
control JSON covers those same cases. Each case used 20 measured values across
10 worker processes.

## Raw evidence

- [Control pyperf JSON](data/generic-exact-call-control-fast-20261006.json)
- [Candidate pyperf JSON](data/generic-exact-call-candidate-fast-20261006.json)
