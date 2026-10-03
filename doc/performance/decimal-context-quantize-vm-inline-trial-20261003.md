# Context.quantize VM inline trial (2026-10-03)

## Result

Rejected. A guarded `Context.quantize` shortcut bypassed bound-method
allocation and generic native-call argument setup for the exact base Context,
two positional arguments, and an unmodified method. A first comparison used
different inline compiler settings and is invalid; a matched `/Ob2` rerun did
not show a significant gain. The shortcut was removed.

The initial control measured 209 ms ± 4 ms and the candidate 214 ms ± 3 ms,
but the candidate's rebuilt objects used `/Ob3` while the fixed control uses
`/Ob2`. Those samples are retained below for traceability and excluded from
the conclusion.

The corrected, matched `/Ob2` run measured:

| Pair | Control | Candidate | `pyperf compare_to` |
| --- | ---: | ---: | --- |
| 1, control first | 222 ms ± 16 ms | 219 ms ± 15 ms | Not significant |
| 2, candidate first | 228 ms ± 27 ms | 226 ms ± 17 ms | Not significant |

All four matched runs warned about instability or high variance. The central
estimates differ by only 1–2%, inside the noise. No performance gain is
established, so no source change from this experiment remains.

## Correctness and scope

The candidate called the existing native Decimal quantize implementation with
the Context instance as its explicit context. It required the exact owner
class and installed native callback, and required two positional arguments.
Instance attributes, subclasses, edited class methods, keyword arguments,
argument expansion, `__getattribute__` hooks, and active monitoring retained
ordinary dispatch. If the native arithmetic rejected a value or context, the
ordinary call path remained responsible for the result.

The matched candidate passed the Decimal arithmetic, quantize, and string-format
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

| Build | Executable SHA-256 | Runtime DLL SHA-256 | Compiler inline setting |
| --- | --- | --- | --- |
| Restored control | `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA` | `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F` | `/Ob2` |
| Matched temporary candidate | `16BA7D833A0AA28D35531C3865E731C67A5F961AC56401A3EEBDCCB58F321A03` | `7E964D772CC2A7C5A5577BB684EEC8362DF2D2E81FB169209039E76D84523D2A` | `/Ob2` |
| Initial, confounded candidate | `1ED60125F3254E9B82951AB74DF08E5E23D37310B4B998F9612ABD66BF4516C6` | `84567610EFEA2EA42D95E37DDE15CFB72A7423F01669510FEDEB3AA2749C2BA4` | `/Ob3` |

## Raw pyperf results

- [Control, pair 1](data/telco-context-quantize-control-r1-20261003.json)
- [Candidate, pair 1](data/telco-context-quantize-candidate-r1-20261003.json)
- [Candidate, pair 2](data/telco-context-quantize-candidate-r2-20261003.json)
- [Control, pair 2](data/telco-context-quantize-control-r2-20261003.json)
- [Matched control, pair 1](data/telco-context-quantize-matched-control-r1-20261003.json)
- [Matched candidate, pair 1](data/telco-context-quantize-matched-candidate-r1-20261003.json)
- [Matched candidate, pair 2](data/telco-context-quantize-matched-candidate-r2-20261003.json)
- [Matched control, pair 2](data/telco-context-quantize-matched-control-r2-20261003.json)
