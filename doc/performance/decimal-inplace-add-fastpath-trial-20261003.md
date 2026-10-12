# Decimal augmented-add VM fast-path trial (2026-10-03)

## Result

Rejected. A guarded shortcut for `Decimal += value` did not improve the
official `telco` benchmark. `InplaceAddLocalLocal` currently enters the
runtime's generic in-place-add builtin after numeric fast paths fail. The
candidate bypassed that call only for Decimal when `__iadd__` was absent from
the live class hierarchy, then reused the existing guarded native `__add__`
callback. Patched methods and subclasses continued through ordinary dispatch.

| Pair | Control | Candidate | Candidate change |
| --- | ---: | ---: | --- |
| 1, control first | 225 ms ± 20 ms | 223 ms ± 21 ms | Not significant |
| 2, candidate first | 212 ms ± 8 ms | 218 ms ± 19 ms | Not significant |

Both `pyperf compare_to` comparisons hid the result as not significant. The
directions disagree, and the candidate did not establish a repeatable gain.
The shortcut was removed. The full comparison still identifies `telco` as a
large gap, so the next work should target a larger shared VM cost than this
single augmented-add route.

## Correctness

The candidate passed the Decimal arithmetic and quantize fixtures and the C++
runtime-value and interpreter test binaries. A temporary semantic probe also
added `Decimal.__iadd__` at runtime and confirmed both candidate and control
returned the replacement result. The experiment left the fixed Release
executable and runtime DLL restored to their control hashes.

## Build identities

Control: executable
`B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`, runtime
DLL `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`.

Candidate: executable
`68409EB9EE7EF25FB99B18FFBB7268617772F8EA93E5523F09ED120080AAAD38`, runtime
DLL `DE544E2B147739D61F0C117C1B171D31DF6AFD7CE806D6557E49BD4D23E113B2`.

The executable path remained `D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`;
XLang3 loaded the CPython 3.14.7 standard library from
`C:\Python\Python314\Lib`.

## Raw pyperf results

- Control run 1: [JSON](data/decimal-inplace-add-fastpath-control-r1-20261003.json)
- Candidate run 1: [JSON](data/decimal-inplace-add-fastpath-candidate-r1-20261003.json)
- Candidate run 2: [JSON](data/decimal-inplace-add-fastpath-candidate-r2-20261003.json)
- Control run 2: [JSON](data/decimal-inplace-add-fastpath-control-r2-20261003.json)
