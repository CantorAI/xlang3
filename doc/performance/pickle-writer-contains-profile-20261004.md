# Pure-Python Pickler membership profile (2026-10-04)

## Finding

An instrumented XLang3 Release build profiled the official pyperformance
1.14.0 `bm_pickle/run_benchmark.py --pure-python --protocol 5 pickle` workload.
After subtracting one warmup workload loop from a six-loop process,
`Contains` accounted for **16.43%** of positive measured self-time, at about
**4,020 calls and 6.82 µs per call**. The source contains repeated
`assert id(obj) not in self.memo` checks in `pickle.py`; `memo` is an exact
dict keyed by integer object ids. `CallMethod` accounted for 15.21%, `Call`
for 10.23%, and VM loop control for 15.88%.

This is diagnostic attribution, not a timing claim: the native timer adds
clock reads and atomic counter updates around every opcode. The important
distinction is that `mapping_contains()` already uses XLang3's integer index
for dict keys. A second early shortcut that skipped its generic mapping/view
setup did not yield a significant benchmark improvement and was removed.
Do not add another special case at the same layer; test the underlying integer
index representation or the VM loop costs with official pyperf.

## A/B result

The candidate was the current Release build plus only the early exact-dict,
`Int64` membership probe. Both runs used CPython **3.14.7**, pyperformance
1.14.0, the same benchmark dependency site, and `--rigorous` mode.

| Benchmark | Control | Candidate | CPython 3.14.7 reference | Result |
|---|---:|---:|---:|---|
| `pickle_pure_python` | 5.52 ms ± 0.46 ms | 5.58 ms ± 0.50 ms | 273.5 µs | No significant change; candidate about 20.4x slower |

Both runs reported unstable sample variation, including one control outlier.
`pyperf compare_to` hid the pair as not significant. The candidate is rejected;
the small apparent slowdown is not evidence of a regression. The broad goal
remains open: pure-Python Pickler is still far slower than CPython 3.14.7.

## Reproduction files

- [One-loop timer baseline](data/pickle-writer-vm-timing-1loop-20261004.txt)
- [Six-loop timer profile](data/pickle-writer-vm-timing-6loops-20261004.txt)
- [Subtracted opcode attribution JSON](data/pickle-writer-vm-timing-delta-20261004.json)
- [Subtracted opcode attribution CSV](data/pickle-writer-vm-timing-delta-20261004.csv)
- [Control pyperf JSON](data/pickle-writer-int-contains-control-rigorous-20261004.json)
- [Candidate pyperf JSON](data/pickle-writer-int-contains-candidate-rigorous-20261004.json)
- [CPython 3.14.7 reference JSON](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)

The experiment used the timer header and summarizer already described in
[`native-vm-opcode-profile-20260930.md`](native-vm-opcode-profile-20260930.md).
The temporary timer include and scopes were removed after collecting data;
`build-repro/Release/` was rebuilt as a normal Release build. The focused dict,
pickle, and stream fixtures passed after the timer removal.
