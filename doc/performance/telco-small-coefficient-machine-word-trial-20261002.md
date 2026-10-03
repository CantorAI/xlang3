# `telco`: bounded machine-word Decimal arithmetic trial (2026-10-02)

**Result: rejected; the focused workload showed no repeatable speedup.** This
trial tested whether Decimal coefficients that fit in 64 bits could bypass
the current string-digit add/multiply loops. It added bounded coefficient
parsing, checked integer arithmetic, and direct integer-RHS multiplication.
Larger coefficients and overflows continued through the existing exact path
or `_pydecimal` fallback. No pure-Python standard-library code was replaced.

The semantic fixture passed on the candidate, including signed zero, exact
addition, large-coefficient fallback, traps, patched operators, and active
ContextVar changes. The Release build compiled with the Visual Studio
developer environment loaded; the initial bare CMake invocation failed when
`cl.exe` could not find MSVC's `<atomic>` header, confirming that the configured
Ninja tree needs that environment when a source file is actually rebuilt.

## Measurement

The order-balanced comparison ran 21 AB/BA pairs, with three warmups, on the
same `telco_decimal_context.py` workload under both builds. It is a focused
synthetic telco-shaped case, not the official pyperformance benchmark.

| Build | Median per workload | Candidate / control |
|---|---:|---:|
| Control | 256.141 ms | 1.000× |
| Machine-word candidate | 254.881 ms | 1.0024× (95% CI 0.9867–1.0139) |

The interval crosses parity, so the 0.5% median difference is timing noise;
the optimization was removed. The machine-word representation reduced work
inside coefficient arithmetic, but that stage is too small a share of this
case for the added parsing and guards to produce a measurable total gain.

Raw paired measurements and executable/runtime identities are in
[`telco-small-i64-fastpath-ab-20261002.json`](data/telco-small-i64-fastpath-ab-20261002.json).
The experiment's temporary source and binaries remain under
`scratch/performance-trials/telco-small-i64-fastpath-20261002/`.

## Consequence

This was not enough to address the official `telco` gap, which the latest
full-suite comparison measured at about 47.9× slower than CPython 3.14.7. The
current native `_decimal` shim already avoids nearly all arithmetic fallback;
future work must remove a larger cost in the per-operation object/call path or
use a fuller XLang3-native `_decimal` representation compatible with the
module boundary CPython exposes. The fixed 11-case Release regression gate
was not needed for this rejected, fully rolled-back trial; the rebuilt
runtime source was restored byte-for-byte to its control snapshot.
