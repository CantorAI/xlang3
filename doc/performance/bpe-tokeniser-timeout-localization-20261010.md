# BPE pyperformance timeout localization — 2026-10-10

Status: the current fixed-binary full run also timed out `bpe_tokeniser` at
300 seconds. No BPE partial JSON was retained, so there is no current-run
sample to score.

## Existing comparison

The previous all-97 XLang3 run records `bpe_tokeniser` as a 300-second timeout,
while the saved CPython 3.14.7 full run completed with a 3.586-second benchmark
mean. The values are from separate runs and the timeout is the whole XLang3
definition, including pyperf workers; do not divide them as if they were
matched per-iteration timings. The all-97 status row is in
[`pyperformance-xlang3-current-vs-cpython3147-full-fast-20261010-r2-all-97-status.csv`](data/pyperformance-xlang3-current-vs-cpython3147-full-fast-20261010-r2-all-97-status.csv).

## Workload shape

The checked-in pyperformance `bm_bpe_tokeniser` code deliberately implements a
simple uncached quadratic BPE. Each training iteration repeatedly scans all
adjacent byte-token pairs, concatenates candidate byte strings for dictionary
lookups and counts pairs, then rebuilds every word after each merge until the
vocabulary reaches 1,024 entries. It then encodes the complete training text.
This is benchmark workload code; keep its algorithm unchanged for a fair
runtime comparison.

The dominant cost is not established by the timeout alone. The likely runtime
areas to measure after the full run are Python loop and generator/zip overhead,
short-bytes creation and concatenation/hash cost, dictionary/Counter updates,
and repeated list slicing/copying. Optimize shared XLang3 runtime or IR paths;
do not replace the Python BPE with a native implementation.

## Current run and next measurement

The active full run uses the fixed Release executable and a 300-second cap per
definition. Its BPE case timed out and the runner advanced to case 26. The full
log is
[`pyperformance-xlang3-current-candidate-full-fast-20261010-r1.log`](data/pyperformance-xlang3-current-candidate-full-fast-20261010-r1.log).
No BPE file appears in the run's partial-output directory. After the suite
ends, run the unchanged benchmark on the fixed Release build and CPython
3.14.7 with a profiler-free paired diagnostic, then profile only the operations
that account for the measured gap. Any runtime change must keep the benchmark
source identical and pass the full fixture/regression gate.
