# Faster exact Context reads in native `_decimal` arithmetic — 2026-10-04

## Change

The XLang3 native `_decimal` arithmetic fast path reads `prec`, `Emin`,
`Emax`, and `clamp` from the active `Context` on every add and multiply. For
the exact, unmodified `_pydecimal.Context` class, those values live in
XLang3's instance-attribute vector unless the program materializes
`Context.__dict__`. The generic attribute path also checks class descriptors
and lookup hooks four times per operation.

The native callback now reads those live fields directly only when the active
context is exactly the cached base `Context`, its class version is unchanged,
and it has no custom attribute lookup hook. When `__dict__` exists, its values
are checked first, matching normal attribute precedence; otherwise the
instance-attribute vector is scanned. Subclasses, class mutations, custom
hooks, nonstandard instance access, and incomplete field storage use normal
Python attribute lookup. The guard version is refreshed after `_decimal`
installs its own `Context.quantize` callback.

The first dictionary-only attempt did not match the runtime's common storage:
profiling showed zero direct reads and 45,019 fallbacks, and the matched body
was 6% slower. I changed it to handle the inline instance-attribute vector and
kept dictionary precedence as a second case. The final diagnostic recorded
45,019 direct reads and zero fallbacks. The profiling counters were removed
from the production path; their captured output is retained as raw evidence.

This changes only XLang3's own native `_decimal` module and its dispatch into
the `_pydecimal`-provided Python `Context`. It does not replace Python
standard-library implementation code.

## Measurements

| Workload | Control | Candidate | Candidate / control |
|---|---:|---:|---:|
| Unchanged pyperformance `telco` body, 21 order-balanced pairs | 198.174 ms | 190.204 ms | 0.9641× (95% interval 0.9578–0.9649) |
| Official pyperformance 1.14.0 `telco`, rigorous mode, fixed Release | 193 ms ± 2 ms | 187 ms ± 2 ms | 1.04× faster, as reported by `pyperf compare_to` |

The isolated native Context lookup is a measurable improvement. The candidate
still takes **32.43×** CPython 3.14.7's saved **5.75 ms** result for `telco`;
the overall performance goal remains open.

## Validation and provenance

- The full Python fixture suite and `xlang3_interpreter_tests` passed on the
  final candidate. The Decimal fixture now tests both inline Context fields
  and the dictionary-precedence case. XLang3 exposes `Context.__dict__` because
  its `_decimal` shim currently uses `_pydecimal`; CPython's native Context
  does not expose that attribute.
- All 11 fixed-Release regression cases passed; `subparsers` was slowest at
  1.078×, under the 1.10 threshold.
- Python 3.14.7, pyperformance 1.14.0, and the shared dependency site were
  used for the benchmark runs.
- The fixed Release executable SHA-256 remains
  `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`.
- Candidate executable SHA-256:
  `57511DD72B472DB10C9A9D2ED3E5AE6F445F3433A04C045416BCCDC8E7567429`.
- Candidate runtime DLL SHA-256:
  `8A35F064A2C6300BB9E46E0FEC0D4219D596B0D11B29D901ABF43685CEDDD599`.
- Balanced body data:
  [`telco-context-inline-fields-candidate-rigorous-20261004.json`](data/telco-context-inline-fields-candidate-rigorous-20261004.json).
- Official pyperformance candidate and fixed-baseline data:
  [`candidate`](data/pyperformance-xlang3-telco-context-inline-fields-candidate-rigorous-20261004.json),
  [`fixed Release`](data/pyperformance-xlang3-telco-fixed-release-rigorous-20261004.json).
- Fixed-Release regression gate:
  [`release-regression-decimal-context-inline-fields-20261004.json`](data/release-regression-decimal-context-inline-fields-20261004.json).
- Direct-path counts:
  [`telco-context-inline-fields-profile-20261004.txt`](data/telco-context-inline-fields-profile-20261004.txt).
- Rejected initial dictionary-only attempt:
  [`source-matched measurements`](data/telco-context-fields-single-loop-candidate-fast-20261004.json),
  [`official fast-mode result`](data/pyperformance-xlang3-telco-context-fields-candidate-20261004.json).
