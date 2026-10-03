# MSVC Release inline-policy trial (2026-10-03)

I tested whether changing the Release compiler's inline expansion setting from
`/Ob2` to `/Ob3` provides a broad speedup. The same current source tree,
MSVC 19.51.36256, CMake multi-config generator, `/O2`, and Python 3.14.7 path
were used for both builds; only `/Ob2` versus `/Ob3` changed. The source tree
contained unrelated uncommitted edits during both builds, so these hashes
identify this matched experiment rather than a clean commit build.

The experimental executables and runtime DLLs stayed under `scratch`; the
fixed `D:\CantorAI\xlang3\build-repro\Release\xlang3.exe` path was not
overwritten. Its hashes remained the preserved `B70A6A...52DA` executable and
`330BA0...F23F` runtime DLL. The scratch builds compiled the `xlang3` target,
then used the same native-package DLL directory copied from the fixed Release
build so both runs loaded identical native packages.

## Results

The full 11-case Release regression gate used 21 order-balanced pairs and five
warmups. It passed with no confirmed regression. Candidate/control elapsed
ratios ranged from **0.984× to 1.011×**; no case showed a meaningful general
speedup. The per-case data and runtime identities are in
[`release-inline-policy-ob3-vs-ob2-20261003.json`](data/release-inline-policy-ob3-vs-ob2-20261003.json).

I then ran pyperformance 1.14.0 `--fast` under CPython 3.14.7 on selected
workloads. All four XLang3 samples emitted pyperf instability warnings, so
these are screening measurements. The first independent samples appeared to
show `pickle_pure_python` improving from 6.51 ms to 5.59 ms. To check that
result, I repeated the two principal cases in reverse runtime order:

| Case | `/Ob2` first | `/Ob3` first | `/Ob2` reverse | `/Ob3` reverse | Reverse-pair comparison |
| --- | ---: | ---: | ---: | ---: | --- |
| `pickle_pure_python` | 6.51 ± 0.63 ms | 5.59 ± 0.29 ms | 5.71 ± 0.16 ms | 5.71 ± 0.17 ms | hidden; not significant |
| `telco` | 218 ± 10 ms | 219 ± 14 ms | 220 ± 10 ms | 221 ± 12 ms | hidden; not significant |
| `async_tree_eager` | 3.24 ± 0.23 s | 3.16 ± 0.21 s | — | — | hidden; not significant in first pair |
| `many_optionals` (`argparse`) | — | 8.82 ± 0.23 ms | — | — | not paired |

The raw pyperf files are preserved:

- [`/Ob2` initial selected cases](data/inline-policy-ob2-pyperformance-20261003.json)
- [`/Ob3` initial selected cases](data/inline-policy-ob3-pyperformance-20261003.json)
- [`/Ob3` reverse-order repeat](data/inline-policy-ob3-pyperformance-r2-20261003.json)
- [`/Ob2` reverse-order repeat](data/inline-policy-ob2-pyperformance-r2-20261003.json)

`pyperf compare_to` hides both `pickle_pure_python` and `telco` for the
reverse-order pair. The initial pickle difference was noise, not a repeatable
compiler-flag gain. `/Ob3` therefore does not address the large library-workload
gap and is rejected; the project default remains `/O2 /Ob2 /DNDEBUG`.

For scale, the same CPython 3.14.7 pyperformance reference measured
`pickle_pure_python` at 273.5 µs, `telco` at 5.755 ms, and `async_tree_eager`
at 86.62 ms. The repeated `/Ob3` results are still about **21×**, **38×**, and
**36×** slower on those cases, respectively. More compiler inlining does not
replace the VM/runtime optimizations needed here.

## Validation and limits

Both configurations compiled and completed the 11-case benchmark gate. The
focused pyperformance workers completed and produced output for each measured
case. The full fixture runner stopped at `ctypes_pointer_return` because these
scratch CMake configurations did not build the project's libffi-backed
ctypes package (`NotImplementedError: libffi is unavailable for ctypes`). This
is a scratch-build completeness issue, not a benchmark result; it means these
two trial builds do not have a full-fixture-suite pass. The fixed Release
installation still imports `ssl` through its existing native package, and its
executable and runtime DLL hashes were verified unchanged after the trial.

The rule against C++ replacements for pure-Python standard-library modules
was not involved: this was a compiler-flag-only experiment with the existing
runtime and standard library.
