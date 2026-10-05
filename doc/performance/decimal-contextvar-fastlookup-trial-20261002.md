# Exact Decimal context lookup trial (2026-10-02)

## Finding and change

The current `telco` profile counted 32,512 native Decimal add/multiply fast
paths. Each path still dispatched the cached `_current_context_var.get(None)`
bound method before reading `prec`, `Emin`, `Emax`, and `clamp`. For the exact,
unmodified XLang3 `ContextVar.get` callback, the active binding can be read
directly from the same thread-local ContextVar table.

The Decimal module now validates that bound-method identity once while it
installs the native `_decimal` operations. Only that exact callback receives a
cached ContextVar key. The hot path then performs one lookup in the currently
active table. A missing binding, a customized getter, or an unsupported object
keeps using the original Python-visible call and fallback behavior. This
optimizes XLang3's native `_contextvars`/`_decimal` boundary; `decimal.py` and
`_pydecimal.py` remain Python.

## Focused measurement

The reproducible [Telco-shaped case](../../benchmarks/cases/telco_decimal_context.py)
performs the same Decimal multiply, `Context.quantize`, add, and
`print(file=StringIO)` pattern for 5,000 records, using deterministic generated
amounts so the test has no pyperformance data-file dependency. The
order-balanced comparison used 21 AB/BA pairs and three warmups against the
preserved source-matched Release control:

| Build | Median per case | Candidate / control |
|---|---:|---:|
| Control | 258.557 ms | 1.000× |
| Candidate | 253.577 ms | 0.9846× (1.0156× faster) |

The paired 95% interval for candidate/control was **0.9755–0.9929**, entirely
below parity. This establishes a small gain on the repeated Decimal pattern,
not a new official pyperformance score. Raw samples, output digest, source hash,
and runtime identities are in
[`decimal-contextvar-fastlookup-ab-20261002.json`](data/decimal-contextvar-fastlookup-ab-20261002.json).

## Correctness and regression checks

`decimal_native_arithmetic`, `decimal_native_quantize`, `context_run_keywords`,
`context_run_empty_star`, and `contextvar_generic_alias` all passed on the
candidate. The arithmetic fixture now also switches to a different Decimal
Context inside `Context.run`, performs a rounded operation there, and confirms
that the caller's context is restored. It passed on both candidate and
source-matched control. Existing cases also exercise rounding flags and traps.
The entire 11-case fixed Release gate passed with 21
order-balanced samples per case against the preserved `baseline-0336992`
runtime; the detailed ratios and hashes are in
[`decimal-contextvar-fastlookup-fixed-baseline-20261002.json`](data/decimal-contextvar-fastlookup-fixed-baseline-20261002.json).

The full fixture runner currently stops at the pre-existing
`runtime_protocol_fastcheck` output mismatch. The preserved source-matched
control produces the same mismatch, so it is not caused by this Decimal
change. The 97-case pyperformance package/data set is not installed in the
available Python environments; no official full-suite result is claimed for
this candidate. The latest full comparison remains the saved 97-definition
report until that run can be repeated.
