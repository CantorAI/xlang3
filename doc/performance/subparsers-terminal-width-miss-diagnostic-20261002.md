# `subparsers`: terminal-width miss diagnostic (2026-10-02)

The current pyperformance comparison measures `subparsers` at **235.5 ms**
for XLang3 and **8.151 ms** for CPython 3.14.7. A fresh call profile of the
unchanged parser workload shows 1,003 calls to `os.get_terminal_size()` and
`sys.stdout.fileno()`, plus 2,009 native `KeyError` constructions. Tracing
locates the repeated misses in Python's `os._Environ.__getitem__`: absent
`COLUMNS` and `LINES` each take the underlying-dict miss path, then the
original-key re-raise path, on every parser formatter construction.

This is a shared-workload effect, not a XLang3-only operation: the matching
CPython 3.13 trace also records 1,003 missing `COLUMNS` and `LINES` accesses
and their caught/re-raised exceptions. The official performance comparison
uses CPython 3.14.7, so the CPython 3.13 trace is only a path confirmation.

## Terminal environment A/B

Seven alternating direct measurements ran the same timed
`benchmarks/cases/subparsers.py` body with `COLUMNS` and `LINES` absent, then
with them set to 80 and 24. The geometric/median behavior shows that both
runtimes benefit from skipping terminal probing. XLang3's median fell from
**258.1 ms** to **216.8 ms** (1.19x faster); CPython 3.13.7 fell from **17.08
ms** to **13.68 ms** (1.25x faster). The XLang3/CPython ratio changed from
about **15.1x** to **15.8x slower**, so setting these environment values does
not explain or close the relative interpreter gap. The direct body and CPython
version also differ from the official 3.14 pyperf run; these timings are
diagnostic, not replacements for the official chart.

Raw evidence:

- [XLang3 terminal-environment A/B](data/subparsers-terminal-env-xlang3-ab-20261002.csv)
- [CPython 3.13 terminal-environment A/B](data/subparsers-terminal-env-cpython313-ab-20261002.csv)
- [XLang3 KeyError trace](data/subparsers-keyerror-trace-xlang3-20261002.txt)
- [CPython 3.13 KeyError trace](data/subparsers-keyerror-trace-cpython313-20261002.txt)
- [Uninstrumented pyperformance call and VM profile](data/current-release-argparse-subparsers-profile-20261002.txt)

The next useful engine target is the cost of this common caught-exception path
and traceback/frame maintenance. `os.py`, `shutil.py`, and `argparse.py` remain
Python code; changing their implementation to C++ would violate the project
rule. An interpreter/runtime optimization must preserve `KeyError` identity,
context, traceback, tracing, and `f_locals` behavior, and must win an
order-balanced A/B plus the fixed Release gate before it is retained.
