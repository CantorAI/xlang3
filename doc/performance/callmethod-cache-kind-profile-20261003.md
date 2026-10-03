# `argparse_subparsers`: warmed `CallMethod` cache kinds (2026-10-03)

Two instrumented executions of pyperformance 1.14.0's unchanged
`bm_argparse/run_benchmark.py subparsers` produced identical method-cache hit
counts. Of 41,305 Python-level calls in the body, 30,215 reached the
class-version-guarded instance-method cache: 27,704 cached interpreted
`UserFunction` calls and 2,511 cached `NativeFunction` calls. No other cache
kind reached that guarded dispatch block. This is 59% of the 51,062
`CallMethod` opcodes measured by the separate native profile.

The counter sits after the direct-instance-attribute shadow check and after
the receiver class/version guard. It counts visits to the corresponding
dispatch block, not arbitrary cache contents. The `UserFunction` case enters
the ordinary frame-push path; `NativeFunction` enters native dispatch. The
matching runs make this distribution reproducible, but the profiling hook
changes execution and these counts do not measure speed.

This rules out treating `CallMethod` as mostly a cold lookup or another
attribute-cache problem. Most guarded hits already resolve directly to a
Python function, so subsequent optimization work should focus on the shared
interpreted-call/frame/opcode costs and be accepted only on ordinary Release
measurements.

## Reproduction and limits

The diagnostic counter was enabled only when
`XLANG3_CALL_METHOD_CACHE_PROFILE=1`. It was built as a separate MSVC Release
`/O2 /Ob3` executable and runtime; neither fixed Release binary was changed.
The benchmark and imports used `C:\Python\Python314` (CPython 3.14.7 standard
library and pyperformance dependencies). The Python `sys.setprofile` runner
and native counter both perturb execution; output is call-shape evidence only.

| Diagnostic binary | SHA-256 |
|---|---|
| `xlang3.exe` | `A5135B484AF7F78BDC3439822F9CD166E33B49B85C76C187CA10B2885CF0E9DD` |
| `xlang3_runtime.dll` | `C0AB11927F27C929E75044EF4A9F1C64A8C15D3E681DEA7A6248FDC83E6EE51F` |

Raw output: [run 1](data/callmethod-cache-kinds-subparsers-20261003.log),
[run 2](data/callmethod-cache-kinds-subparsers-r2-20261003.log).
