# August-to-current speed evidence — 2026-10-10

The Git history does not contain an August full-pyperformance comparison to
CPython. At the August 31 checkpoint `8beae11d`,
`benchmarks/pyperformance/supported.txt` explicitly says that no official
pyperformance benchmark had yet been marked supported; the repository tracked
only its custom microbenchmark cases. Therefore the statement that the August
version beat CPython cannot be verified against the October 9 all-97 run from
the checked-in evidence. A microbenchmark subset and pyperformance's full
application workloads are different comparisons.

On the current fixed-path Release executable, a fresh diagnostic compared
simple Python bodies to CPython **3.14.7**. It used the same 50,000 loop count
for integer and function-call bodies and 10,000 for the dictionary body; each
row is the median of three samples. Lower elapsed time is faster.

| Body | CPython 3.14.7 | XLang3 | XLang3 elapsed / CPython |
|---|---:|---:|---:|
| Integer loop | 0.894 ms | 1.183 ms | 1.32× |
| Python function call loop | 1.159 ms | 3.716 ms | 3.21× |
| Dict get/set loop | 0.340 ms | 1.179 ms | 3.47× |

A separate 100,000-yield dict probe measured keys at 1.72× CPython elapsed,
values at 1.69×, and items at 5.63×. This localizes a measurable gap in
general Python-call and mapping/runtime work; the simple integer loop is much
closer. It does not prove that any one of these operations explains the full
pyperformance gap.

The latest full report in the checked-in evidence is still the October 9
capture: 75/97 definitions completed, 22 failed, and the scored subset had a
0.12306× CPython-relative geometric mean. It is an unpaired result against
October 7 CPython 3.14.7 and cannot establish an August-to-October regression
or a universal speed ranking. See the [full comparison](pyperformance-xlang3-python-new-r4-vs-cpython3147-full-fast-20261009.md).

These measurements point the next investigation toward common VM/runtime
operations used by many Python bodies, especially calls and dictionary access.
The recent dict-iterator source-cache trial was rejected: it produced no
repeatable gain and its runtime code was removed. Focused results are in
[the iterator trial record](dict-iterator-source-cache-trial-20261010.md).

## Evidence

- [XLang3 microbench output](data/vm-hotpath-microbench-current-20261010.txt)
- [CPython 3.14.7 microbench output](data/vm-hotpath-microbench-cpython3147-20261010.txt)
- [XLang3 dict-iteration timings](data/dict-iterator-primitives-current-20261010.json)
- [CPython dict-iteration timings](data/dict-iterator-primitives-cpython3147-20261010.json)
- August 31 source checkpoint: `8beae11d`; its tracked support list is available with `git show 8beae11d:benchmarks/pyperformance/supported.txt`.

The current fixed-path executable and runtime DLL are SHA-256
`FE99DAAF1100286C569FA49A7104D4D4E694F19FADF0B5AF8D677C3439086374` and
`DB6E062B0692347E0B4C92E5A7389CEBB270684497D0BD720EB7FF9A7FDA895E`.
