# Direct cached read for `LoadLocalAttr` — 2026-10-04

## Result

Rejected and removed. The candidate tried to read a warmed instance
slot/attribute directly from the local-variable array, skipping the receiver
copy into a scratch register and the subsequent general `LoadAttr` call. The
focused Pickler-shaped workload did not show a reliable improvement:

| Workload | Parent median | Candidate median | Candidate / parent | 95% interval |
|---|---:|---:|---:|---:|
| 60 pure-Python Pickler dumps per invocation, 21 order-balanced pairs | 47.676 ms | 47.688 ms | 0.993x | 0.981–1.020 |

The interval crosses parity, and the median difference is effectively zero.
The additional guard path was therefore removed instead of retained as an
unproven VM branch.

An official `pickle_pure_python --rigorous` run of the parent was noisy at
5.52 ±0.47 ms, including an 8.53 ms outlier. The candidate worker failed in
pyperf manager code (`AttributeError: object does not support attribute
assignment`), so that attempt is not a valid candidate comparison and is not
used to claim either a win or a regression.

## Implementation boundary and regression check

The trial changed only XLang3 VM dispatch. It did not modify `pickle.py` or
replace a CPython pure-Python library with C++. The existing `LoadAttr` path
already performs the guarded cached instance read; duplicating those guards in
`LoadLocalAttr` saved too little on the measured writer workload.

The `load_attr_cache_precedence` fixture now exercises the local-load form
through repeated instance reads, a changed instance value, class replacement
with a data descriptor, and a custom `__getattribute__`. CPython 3.14 and the
Release interpreter produced matching output, and
`xlang3_interpreter_tests.exe` passed. The full fixture runner remains
unverified because an earlier unrelated `subprocess_inherits_chdir` case
fails under Python 3.14 before this fixture is reached.

Raw paired data:
[`load-local-attr-direct-pickle-writer-ab-20261004.json`](data/load-local-attr-direct-pickle-writer-ab-20261004.json).
The parent official pyperf run is preserved at
[`load-local-attr-direct-pickle-pure-parent-rigorous-20261004.json`](data/load-local-attr-direct-pickle-pure-parent-rigorous-20261004.json).
