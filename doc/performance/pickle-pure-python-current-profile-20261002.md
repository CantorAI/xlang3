# Pure-Python pickle profile on the current Release (2026-10-02)

The current full pyperformance 1.14.0 run measured `pickle_pure_python` at
**5.762 ms ± 0.17 ms** against **273.5 µs** for CPython 3.14.7, about **21×
slower**. The benchmark intentionally selects Python's pure-Python Pickler;
XLang3 must keep that library implementation in Python and reduce the cost in
its interpreter/runtime.

A diagnostic run of the benchmark's unmodified `bm_pickle/run_benchmark.py`
with `pickle --pure-python` counted 6,080 `pickle.py:save` calls, 10,420
`pickle.py:write` calls, 3,940 `memoize` calls, 3,700 `save_str` calls, and
6,140 `commit_frame` calls in one outer workload pass. The separate VM-counter
run saw 323,881 native calls (275,736 fast, 142,178 cached-fast) and about 2.5
million value reference-count operations. Profiling alters runtime heavily;
these counts identify repeated Python call and VM traffic, not normal-build
timing shares.

Prior exact-call and polymorphic-call trials for the pure-Python unpickler
were neutral and were not repeated. The current profile is a serializer
workload and should be used to select the next interpreter-level experiment;
no native replacement for `pickle.py` is appropriate.

Raw profiles:

- [Python call profile](data/pickle-pure-python-current-profile-20261002.txt)
- [VM counters and Python call counts](data/pickle-pure-python-current-vm-counters-20261002.txt)
- [Current all-97 pyperformance comparison](pyperformance-xlang3-gc-early-reject-vs-cpython314-full-fast-20261002.md)
