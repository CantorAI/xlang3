# Exact-dict lookup for compiled eval locals — 2026-10-10

## Result

The candidate avoided allocating a temporary `Value` key and entering generic
mapping dispatch when a compiled `eval()` entry frame reads a name from exact
dict locals. Custom mappings continued through the generic path. This is a
runtime optimization; it leaves Genshi and all other pure-Python libraries
unchanged.

An order-following repeat compared the generic lookup parent and candidate
with the official `genshi` pyperformance benchmark. The parent measured
`genshi_text` at **324.8 ± 20.7 ms** and `genshi_xml` at **890.8 ± 49.6 ms**;
the candidate measured **311.8 ± 19.7 ms** and **873.2 ± 34.6 ms**. `pyperf
compare_to` marked both differences **not significant**. This is not evidence
of a speedup, so the specialization was removed. The candidate's CPython
3.14.7 ratios were 16.1× slower for text and 20.5× slower for XML; the trial
does not explain the remaining Genshi gap.

An earlier parent attempt measured 525 ms and 1.67 s with high variance,
while a candidate attempt was near 312/873 ms. That comparison is retained as
noisy evidence and is not used for the decision. The repeated parent removed
that apparent gap.

## Validation

The `eval_live_namespaces` fixture passed with the candidate. The full fixture
run initially exposed a separate mid-frame monitoring bug: after code enabled
line events, XLang3 reported the current line retroactively. A focused probe
showed XLang3 lines `[14, 15, 16, 17]` while CPython 3.14.7 reported
`[15, 16, 17]`. The VM now marks the already-entered call-site line when line
monitoring becomes active mid-frame. The focused probe, the
`sys_monitoring_all_events` fixture, and the complete fixture suite pass with
that correction. The fix is recorded in `src/executor/xlang_vm/xlang_vm_loop.cpp`;
it does not contribute to the Genshi performance result above.

- [Focused callback-line probe](../../benchmarks/diagnostics/probe_sys_monitoring_midframe_lines.py)
- [Full fixture run](data/eval-locals-final-fixtures-20261010.log)

Both benchmark JSON files use Python 3.14.7 and pyperformance 1.14.0:

- [Generic lookup parent, repeat](data/pyperf-genshi-eval-locals-generic-parent-repeat-xlang3-20261010.json)
- [Exact-dict lookup candidate](data/pyperf-genshi-eval-locals-string-lookup-candidate-xlang3-20261010.json)
- [Noisy initial parent attempt](data/pyperf-genshi-eval-locals-generic-parent-xlang3-20261010.json)
- [First candidate attempt](data/pyperf-genshi-eval-locals-string-lookup-r1-xlang3-20261010.json)
- [Candidate comparison against CPython 3.14.7](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json)

The executable path remained
`build-repro/main-verify-20261006/Release/xlang3.exe`. The final generic-lookup
Release pair has executable SHA-256
`019b03c2c239e21e31902054b41b5c3b129007d3dbeefd472954478edf75358` and runtime
DLL SHA-256 `9db2f2eaee375bf94a2918aa937ce7beeaea0abeb16109b617e05ba991785559`.
