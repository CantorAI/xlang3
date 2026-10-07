# Full-suite failure reporting — 2026-10-07

The comparison generator now excludes all timing values belonging to failed
benchmark definitions from charts, speed ratios and aggregate statistics.
A definition that emits one subtest and then loses a later worker remains a
failure. Its raw JSON/log evidence is preserved, but its partial results are
not scored. This applies independently to XLang3 and CPython.

For a fresh CPython run, pass `--cpython-log` to
`benchmarks/diagnostics/summarize_pyperformance_comparison.py`. The generator
requires all 97 definitions in that log, uses its failures instead of the
historical index's statuses, and rejects missing CPython outcomes. The previous
CSV remains an index of definition/subtest names. It does not supply fresh
CPython outcomes. Successful runs no longer receive a hard-coded exit-1 claim.

Four reporting tests use synthetic all-97 data, including partial values from
failed workers, an incomplete fresh CPython log, a missing fresh outcome and
an all-successful run. They verify that only the 95 jointly successful cases
in the two-failure example contribute to its chart and geometric mean.

Validation command (CPython 3.14.7):

```powershell
& 'C:\Python\Python314\python.exe' -m unittest discover -s benchmarks/diagnostics/tests -p test_summarize_pyperformance_comparison.py -v
```

This change affects reporting only. It does not change the executable currently
under measurement, the fixed regression gate, or frozen historical reports.
