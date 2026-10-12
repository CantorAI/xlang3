# Native `_decimal` metadata avoids importing `_pydecimal` at startup

XLang3's native `_decimal` module used its module-level `__getattr__` to lazily
initialize the pure-Python `_pydecimal` fallback for every missing attribute.
During normal startup, Python 3.14's `site.abs_paths()` visits loaded modules
and probes `__file__` and `__cached__`. Those probes accidentally initialized
the fallback, even though startup had not imported or used `decimal`.

The native module now defines `__file__` and `__cached__` as `None` when it is
registered. This matches a built-in module with no standalone source/cache
file, lets `site.abs_paths()` handle it without invoking `__getattr__`, and
keeps the Python fallback lazy until a Decimal API is actually requested. The
code comment at registration records this startup invariant. No CPython
pure-Python library implementation was moved into C++.

## Measurement

Official pyperformance 1.14.0 `python_startup` (`--fast`) was run against the
preserved Release executable and then the candidate on the same Windows host,
with CPython **3.14.7** at `C:\Python\Python314` and the same benchmark
dependency path. The executable hashes match; the runtime DLL changed only
because of this fix.

| Runtime | Mean | Relative to pre-fix XLang3 |
|---|---:|---:|
| XLang3 before fix | 73.7 ms ± 1.8 ms | 1.00× |
| XLang3 after fix | 31.4 ms ± 1.8 ms | **2.35× faster** |
| XLang3 after-fix rerun | 30.4 ms ± 1.1 ms | **2.43× faster** |
| CPython 3.14.7 reference | 24.9 ms | XLang3 remains 1.26× slower |

The first post-fix run reduced startup time by 57.4%; the repeat measured a
58.8% reduction. The benchmark emitted pyperf's fast-mode stability warning,
so treat these as directional results; the large change reproduced on the
second run. XLang3 is still slower than CPython on this case.

The before/after samples use the same Release executable SHA-256
`94F65647D7E3116A81CC7D1E7783D5E951E7A260101667177257F9266502E033`. The
pre-fix runtime DLL SHA-256 is
`E57C8698FEC7C8097FBDA6B297EBF06680BE83ABBAC28A85A755C1E3CF4EAED1`; the
fixed DLL SHA-256 is
`C60087265E4A97FEF73EC4F9CDA28BCDE02A4291FA2E4ED760E4390DB9E9BDE5`.

The exact trigger was confirmed by manually reproducing `site.abs_paths()`:
reading `_decimal.__file__` invoked the lazy module getter and registered
`_pydecimal`. After the change, probing `'_pydecimal' in sys.modules` prints
`False` both with normal startup and with `-S`.

## Correctness and regression checks

- `xlang3_runtime_value_tests.exe`: passed. It verifies that reading native
  `_decimal.__file__` and `__cached__` returns `None` and does not register
  `_pydecimal`.
- Full Release regression gate: **11/11 cases passed**, 21 paired samples per
  case, five warmups, 10% threshold. Raw results are preserved below.
- Full CTest run: **54/55 passed**. The sole failure is the existing Visual
  Studio debug-launch smoke test, whose tracked profile requires `build/Release`
  while this checkout intentionally builds in `build-repro/Release`.

## Raw evidence

- [Pre-fix pyperformance JSON](data/pyperformance-xlang3-python-startup-decimal-fallback-baseline-20261005.json) and [log](data/pyperformance-xlang3-python-startup-decimal-fallback-baseline-20261005.log)
- [Post-fix pyperformance JSON](data/pyperformance-xlang3-python-startup-decimal-fallback-fixed-20261005.json), [repeat JSON](data/pyperformance-xlang3-python-startup-decimal-fallback-fixed-rerun-20261005.json), and [repeat log](data/pyperformance-xlang3-python-startup-decimal-fallback-fixed-20261005.log)
- [CPython 3.14.7 reference JSON](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)
- [Full Release regression-gate JSON](data/release-regression-site-decimal-fallback-fix-20261005.json)
- [Full 97-case pyperformance report before this fix](pyperformance-xlang3-fixed-release-all-97-vs-cpython314-fast-shim-corrected-20261005.md)
