# Context.quantize VM inline trial (2026-10-03)

## Result

Rejected. A guarded `Context.quantize` shortcut bypassed bound-method
allocation and generic native-call argument setup for the exact base Context,
two positional arguments, and an unmodified method. It did not improve
pyperformance `telco` consistently, so the shortcut and its code comments were
removed.

| Pair | Control | Candidate | `pyperf compare_to` |
| --- | ---: | ---: | --- |
| 1, control first | 209 ms ± 4 ms | 214 ms ± 3 ms | Candidate 1.03× slower; not significant |
| 2, candidate first | 211 ms ± 5 ms | 213 ms ± 5 ms | Not significant |

Both runs warned that the `--fast` samples were unstable. The second pair was
run in reverse order to reduce order bias. Neither pair establishes a gain;
the central estimates slightly favor the existing path. No source change from
this experiment remains.

## Correctness and scope

The candidate called the existing native Decimal quantize implementation with
the Context instance as its explicit context. It required the exact owner
class and installed native callback, and required two positional arguments.
Instance attributes, subclasses, edited class methods, keyword arguments,
argument expansion, `__getattribute__` hooks, and active monitoring retained
ordinary dispatch. If the native arithmetic rejected a value or context, the
ordinary call path remained responsible for the result.

The candidate passed the Decimal arithmetic, quantize, and string-format
fixtures. The fixed executable path stayed
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`; CPython was
`C:\Python\Python314\python.exe` (3.14.7), and the runner used pyperformance
1.14.0 with the repository's Windows compatibility shim. The baseline exe and
runtime DLL were restored byte-for-byte after the trial.

This test rules out skipping the outer `Context.quantize` method-call setup as
a standalone source of a large Telco gain. It does not explain the remaining
roughly 37× gap: the existing control measured about 210 ms, while the saved
CPython 3.14.7 reference is 5.755 ms. Further work should profile a larger
shared VM/operator cost rather than repeat method lookup and call-wrapper
shortcuts.

## Build identities

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
| --- | --- | --- |
| Restored control | `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA` | `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F` |
| Temporary candidate | `1ED60125F3254E9B82951AB74DF08E5E23D37310B4B998F9612ABD66BF4516C6` | `84567610EFEA2EA42D95E37DDE15CFB72A7423F01669510FEDEB3AA2749C2BA4` |

## Raw pyperf results

- [Control, pair 1](data/telco-context-quantize-control-r1-20261003.json)
- [Candidate, pair 1](data/telco-context-quantize-candidate-r1-20261003.json)
- [Candidate, pair 2](data/telco-context-quantize-candidate-r2-20261003.json)
- [Control, pair 2](data/telco-context-quantize-control-r2-20261003.json)
