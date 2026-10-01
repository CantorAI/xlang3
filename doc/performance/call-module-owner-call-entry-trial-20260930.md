# Callee-module owner selection at Python call entry (2026-09-30)

## Question

CPython 3.14's `CALL_PY_EXACT_ARGS` enters a frame using the callee's function
and code object. XLang3's `call_user_function` copied the caller's
`shared_ptr<Module>` before checking whether the `FunctionObject` already owned
its module. For the common module-backed function, assigning the callee owner
then released that temporary caller-owner copy. I tested selecting the owner
first to avoid that extra shared-reference increment/decrement on Python calls.

The experiment changed no argument binding, frame layout, or call guards, and
left CPython's pure-Python benchmark code unchanged. The exact candidate source
edit is preserved in
[`call-module-owner-trial-20260930.patch`](data/call-module-owner-trial-20260930.patch).

## Official pyperformance result

The parent and candidate were built from the same source revision and measured
serially with pyperformance 1.14.0 in rigorous mode. Each run used the same
pyperf worker setup for `deltablue` and `unpickle_pure_python`.

| Benchmark | Parent | Candidate | Candidate vs parent |
|---|---:|---:|---:|
| `deltablue` | 47.1 ± 4.7 ms | 46.8 ± 5.2 ms | 1.01× faster, not significant |
| `unpickle_pure_python` | 3.30 ± 0.27 ms | 3.35 ± 0.23 ms | 1.01× slower, not significant |

Both runs reported substantial variation. `pyperf compare_to -v` marked both
comparisons not significant, with a 1.00× slower geometric mean. The
shared-owner cleanup is therefore rejected and removed from the engine.

| Build | `xlang3.exe` SHA-256 | `xlang3_runtime.dll` SHA-256 |
|---|---|---|
| Parent | `D56726FF584DE2717EFD006BD92C0AF8FA4B5CC73373565CB70C3F25B2059CCD` | `72EBEB7D5E8847782AEDB6EC544BA7E3A643EAEC2E8B06BBCF891DCE1349F766` |
| Candidate | `D56726FF584DE2717EFD006BD92C0AF8FA4B5CC73373565CB70C3F25B2059CCD` | `5B071F2CC807D7B821D37D85FA498FB30C934BCAEBB49808E2CBE38561971D63` |

The raw [parent](data/call-module-owner-parent-rigorous-20260930.json) and
[candidate](data/call-module-owner-candidate-rigorous-20260930.json) pyperf
files and their runner logs are retained. The candidate also passed all 11
cases in the complete fixed Release gate; its report is
[here](data/call-module-owner-candidate-fixed-baseline-20260930.json).
The full Python fixture runner completed without reported failures, and the
interpreter, runtime-value, and IR-codec C++ suites passed. The IR-codec test
binary was rebuilt from current source after an old executable still contained
an assertion from a discarded fusion trial.

No runtime change from this trial is retained. The fixed-baseline Release
executable and runtime were restored to the source-matched parent build.
