# MSVC Release inline-policy trial (2026-10-03)

## Correction

The initial `/Ob2` versus `/Ob3` comparison was invalid. A local CMake edit
unconditionally changed Release `/Ob2` to `/Ob3`, so both initial builds used
`/Ob3`. The original JSON and log files remain in `data/` as historical,
invalid evidence; they must not be used to claim a compiler-flag comparison.

I added a guarded `XLANG3_RELEASE_OB3` CMake option in the current working tree
and rebuilt the same source snapshot twice. The generated flags verify that
the `/Ob2` build used `/O2 /Ob2 /DNDEBUG` and the `/Ob3` build used
`/O2 /Ob3 /DNDEBUG`; this is the corrected matched comparison. The source tree
contained unrelated local changes, so these executable hashes identify the
measured builds more reliably than a commit id.

## Corrected results

The 11-case Release regression gate used 21 order-balanced pairs and five
warmups. `/Ob3` passed with no confirmed regression; the candidate/control
geometric mean was **0.977×** (about 2.3% faster). Per-case data is in
[`release-inline-policy-ob3-vs-ob2-corrected-20261003.json`](data/release-inline-policy-ob3-vs-ob2-corrected-20261003.json).

The repeated pyperformance `--fast` comparison favored `/Ob3` on two of four
selected cases: `many_optionals` was **1.11×** faster and `async_tree_eager`
was **1.07×** faster. `pickle_pure_python` and `telco` were not significantly
different. These screening samples warned about instability and do not
establish a broad gain. Both run orders and raw JSON are preserved:

- [`/Ob2`, first pair](data/inline-policy-ob2-pyperformance-corrected-20261003.json)
- [`/Ob3`, first pair](data/inline-policy-ob3-pyperformance-corrected-20261003.json)
- [`/Ob3`, reverse pair](data/inline-policy-ob3-pyperformance-r2-corrected-20261003.json)
- [`/Ob2`, reverse pair](data/inline-policy-ob2-pyperformance-r2-corrected-20261003.json)

A subsequent all-definition `/Ob3` run against CPython 3.14.7 completed
47/97 definitions and recorded 50 worker deaths or timeouts. Among 51 matched
subtests, XLang3 won four; the geometric mean CPython/XLang3 speed ratio was
**0.14562×** (about 6.9× slower). This confirms that `/Ob3` alone does not
close the library-workload gap. The complete report, horizontal ratio chart,
all-97 status table, matched subtests, raw JSON, and log are in
[`pyperformance-xlang3-ob3-corrected-full-fast-20261003.md`](pyperformance-xlang3-ob3-corrected-full-fast-20261003.md).

## Build identity and safety

The corrected `/Ob2` executable and runtime DLL hashes were
`C5563D98063C1AF0F6546C1C41DB3ECD58A1A31BCCE57DE8D6EA5629DDEBA1FC` and
`47AA13A2F9EA9E8053ED0929AC0E8A6CEE29EF8837B81447D9A1B255C689EDA6`.
The corrected `/Ob3` executable and runtime DLL hashes were
`0F4541BA603DF07FC031D04A1E9EF118B2D0DBC08EFF78C515A7E94607076B` and
`2A11460D4877EC6B8A60F66A3C58C5F1D3B9959343FF319A5D2A88F9067CAE46`.
The full run used the `/Ob3` scratch executable and Python 3.14.7. The fixed
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe` and its runtime DLL
remained byte-for-byte unchanged at their preserved baseline hashes
`B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA` and
`330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`.

This compiler-only test leaves pure-Python standard-library implementations
in Python. Native module work remains limited to modules that have a native
CPython counterpart, such as XLang3's `_decimal` implementation.
