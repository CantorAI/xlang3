# Native `math.log` BigInt compatibility trial (2026-10-03)

The corrected Python 3.14 pyperformance run identified a concrete reason the
`sympy` benchmark died: SymPy's `mpmath` dependency calls `math.log(n, 2)` for
arbitrary-size Python integers, and XLang3 rejected those values. The native
`math.log` implementation now reads the BigInt limbs directly, derives the
logarithm from the leading 53 bits, and avoids converting the full integer to
a decimal string. The source comment records why this path deliberately avoids
that allocation and full-digit scan. Domain errors now raise `ValueError`, and
the native math module also exposes `frexp`, which the imported `mpmath` code
requires.

`tests/fixtures/core/math_module.py` covers `log(2**2048, 2)`, a huge integer
base, domain errors for negative/zero inputs and base one, and `frexp`. The
fixture passes against the built Release executable, and its expected output
is saved in `tests/fixtures/expected/math_module.out`. The code builds with the
existing Visual Studio 2026 Release toolchain at
`build-repro/Release/xlang3.exe`; no build or executable paths were changed.

The targeted `sympy` run now passes the original BigInt/domain failure and the
missing-`frexp` import. Python 3.14 removed `distutils`, which this older SymPy
release still imports. A small Python-only `LooseVersion` bridge was added to
the shared pyperformance compatibility overlay so both CPython and XLang3 can
load that dependency. With the same overlay, CPython 3.14 imports SymPy 1.8
successfully. XLang3 proceeds further but still fails while SymPy initializes
its assumption rules with `RecursionError` in `sympy.core.facts.process_rule`.
The failure reproduces with XLang3's recursion limit raised to 5000; its VM
currently applies a separate 1024-frame host-safety ceiling. The targeted run
therefore produced no valid SymPy timing and this trial makes no speedup claim.
The follow-up full Python 3.14 run still completed 47 of 97 definitions and
failed or timed out on 50. It matched 51 subtests, with four XLang3 wins and a
CPython/XLang3 geometric-mean ratio of 0.14658x. This is slightly worse than
the previous fast-run ratio and confirms that this math compatibility fix has
not materially changed overall performance. See the
[updated full comparison and chart](pyperformance-xlang3-python314-stdlib-full-fast-distutils-shim-20261003.md)
and its [complete status list](data/pyperformance-xlang3-python314-stdlib-full-fast-distutils-shim-20261003-all-97-status.csv).

The Release regression gate against `scratch/performance/baseline-0336992`
passed 10 of 11 cases in a seven-pair run. `gc_traversal` was inconclusive at
that sample count, then passed a separate 21-pair confirmation at 1.030x
candidate/baseline. Raw gate evidence is in
[`math-log-bigint-fixed-release-gate-r7-20261003.json`](data/math-log-bigint-fixed-release-gate-r7-20261003.json)
and
[`math-log-bigint-fixed-release-gc-confirm-r21-20261003.json`](data/math-log-bigint-fixed-release-gc-confirm-r21-20261003.json).
The latest targeted SymPy run and its output are in
[`pyperformance-xlang3-math-log-bigint-sympy-fast-distutils-shim-20261003.log`](data/pyperformance-xlang3-math-log-bigint-sympy-fast-distutils-shim-20261003.log).
