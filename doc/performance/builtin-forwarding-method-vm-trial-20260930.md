# Python method-forwarding VM trial (2026-09-30)

This trial evaluated a generic XLang3 VM shortcut for a small Python method
that forwards its arguments to a native built-in method. It did not add or
replace a standard-library module. Pure-Python modules such as `re._parser`,
`re._compiler`, and `argparse` remain Python; native XLang3 modules are only
appropriate where CPython itself supplies a native module, with the same
Python-facing import name and compatible API/ABI.

## Result

The shortcut was removed. It did not produce a confirmed improvement in either
official target benchmark. The initial broad form also changed subclass method
dispatch and omitted the forwarding Python method from exception tracebacks.
The semantic regression fixture remains so future VM work checks both cases.

The final trial build added an explicit native-method registry opt-in and
restricted the fast path to forwarding one argument to `list.append` on an
exact built-in list with spare vector capacity. That guard avoided list
subclass overrides and append-growth errors. It passed the fixture suite and
the complete fixed-baseline Release gate, but the official pyperformance
results below did not justify retaining the added VM path.

## Official pyperformance results

All measurements used pyperformance 1.14.0 with rigorous pyperf settings on
Windows x64. The same repository shim disabled unsupported Windows priority and
host-metadata hooks for both runtimes. The XLang3 control is the preserved
pre-trial Release executable at `533b4b8`; CPython is 3.14.7.

| Benchmark | CPython 3.14.7 | XLang3 control | XLang3 trial | Trial vs. CPython |
| --- | ---: | ---: | ---: | ---: |
| `regex_compile` | 96.8 ± 5.2 ms | 1.34 ± 0.09 s | 1.32 ± 0.04 s | 13.61× slower |
| `argparse_subparsers` | 7.60 ± 0.18 ms | 180 ± 6 ms | 182 ± 3 ms | 23.89× slower |

Pyperf warned that the regex samples and the subparser control/CPython samples
were unstable. `compare_to` reported about 1.02× faster for regex compilation
from the sample means, but the small difference is not a reliable gain. The
subparser trial was about 1.01× slower than its control. These measurements
show that this shortcut did not materially move either full benchmark toward
CPython.

The pyperformance `argparse_subparsers` benchmark constructs its two argument
lists outside the timed parser call. The local `benchmarks/cases/subparsers.py`
reproduction builds those lists inside its timed `main()`. That local 11-case
gate comparison measured 0.959× candidate/control time (95% interval
0.9563–0.9765), but it includes extra argument-list construction and does not
confirm a parser-body improvement. The fixed-baseline gate passed all 11 cases;
the current-control and fixed-baseline reports are preserved below.

## Correctness findings and retained guardrails

The first broad form treated any registered native method as safe to call
without its Python forwarding frame. A wrapper around `list.pop` then lost its
frame from an `IndexError` traceback. It also treated a list subclass as a
plain list based on its sequence storage, bypassing an overridden `append`.
Both behaviors differed from CPython.

The trial was narrowed to an exact built-in list append with spare capacity,
and all other cases fell back to the original Python function. The new
`method_forwarding_semantics` fixture verifies a list-subclass override and
checks the wrapper frame in a failing `pop` traceback. Future forwarding
optimizations must preserve those semantics as well as show a gain in the
official target benchmark; the durable instruction is in [`AGENTS.md`](../../AGENTS.md).

The user-facing implementation boundary did not change: optimization belongs
in the compiler, IR, VM, or generic runtime for pure-Python code. The trial did
not implement any CPython pure-Python library module in C++.

## Validation reports and raw samples

- [Fixed-baseline 11-case Release gate](data/release-regression-builtin-forwarding-fixed-20260930.json)
- [Nine-pair current-control Release gate](data/release-regression-builtin-forwarding-vs-control-20260930.json)
- `regex_compile`: [CPython 3.14.7](data/pyperformance-regex-compile-forwarder-cpython314-rigorous-20260930.json), [XLang3 control](data/pyperformance-regex-compile-forwarder-control-xlang3-rigorous-20260930.json), [XLang3 trial](data/pyperformance-regex-compile-forwarder-candidate-xlang3-rigorous-20260930.json)
- `argparse_subparsers`: [CPython 3.14.7](data/pyperformance-argparse-subparsers-forwarder-cpython314-rigorous-20260930.json), [XLang3 control](data/pyperformance-argparse-subparsers-forwarder-control-xlang3-rigorous-20260930.json), [XLang3 trial](data/pyperformance-argparse-subparsers-forwarder-candidate-xlang3-rigorous-20260930.json)
