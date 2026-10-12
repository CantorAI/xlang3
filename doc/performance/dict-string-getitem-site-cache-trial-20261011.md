# Exact-dict string GetItem site-cache trial — 2026-10-11

## Isolated result

Retained a guarded, per-instruction cache for repeated exact built-in dict and
exact built-in string reads. The warm path validates the live dictionary owner,
entry bounds, and current string key before reading. A changed or deleted key
falls through to the normal string-index lookup; custom non-string key equality
continues through the generic mapping path. The returned value is pinned before
the destination register is replaced because releasing its former value may run
a finalizer that mutates the dictionary.

To isolate this change, two Release builds were made from the same working-tree
source: one without only this cache and one with it. The rich-comparison proof
cache and all other source changes were therefore present in both binaries.
The 500,000-iteration exact-string dict read/write microbenchmark medians were
64.5 ms without the cache and 62.1 ms with it (1.04x faster). CPython 3.14.7
measured 23.7 ms on the same diagnostic, leaving XLang3 2.62x slower on this
narrow loop.

The paired official `richards` result was 346 ms ± 6 ms without the cache and
337 ms ± 3 ms with it. `pyperf compare_to` reports the candidate 1.03x faster,
but both fast-mode samples carry instability warnings and the gain is small.
`typing_runtime_protocols` was 2.85 ms ± 0.06 ms versus 2.86 ms ± 0.09 ms;
pyperf hides it as insignificant. This is not evidence of a suite-wide gain.

The first attempted comparison (339 ms versus 378 ms) used an older binary that
did not contain the same pending source changes. It was confounded and must not
be attributed to this cache; its raw files are retained only as superseded
historical evidence.

## Correctness and guard rationale

`tests/fixtures/core/dict_custom_hash_equality.py` warms a string-key read,
mutates and deletes entries, changes keys at the same GetItem instruction, and
checks a string-equal custom key. The focused fixture passed on both isolated
control and candidate binaries. The cache holds only a non-owning `Object*`
identity and an entry index; it does not keep a dictionary or value alive. Each
hit compares the live key and entry after validating the index, so erase/reinsert
and address reuse cannot return a stale value.

## Paired pyperformance results

Both runs used Python 3.14.7, pyperformance 1.14.0 fast mode, the fixed
`build-repro/main-verify-20261006/Release/xlang3.exe` path, the same dependency
site, and the same 300-second cap. The only source difference between binaries
was the dict string-key cache.

| Case | Control | Candidate | `pyperf compare_to` |
|---|---:|---:|---:|
| `richards` | 346 ms ± 6 ms | 337 ms ± 3 ms | Candidate 1.03x faster |
| `typing_runtime_protocols` | 2.85 ms ± 0.06 ms | 2.86 ms ± 0.09 ms | Hidden as insignificant |

The candidate's `pprint` case timed out at 300 seconds in an earlier attempt and
is excluded. Template cases `chameleon`, `django_template`, and `mako` could not
start because their isolated benchmark environments lacked those dependencies.

## Build identities and evidence

Isolated candidate executable SHA-256:
`7D9FB86BB78EA8357107C92E39253E16C9BAE60BBB2235AE358E83C35580DF39`.
Candidate runtime DLL SHA-256:
`F366FA18ABD77D98DE6FD739C2F17850FBEFC2C7178CE4086F996F578901BC4E`.

Isolated control executable SHA-256:
`8566D90CC54A2E3CC8B1C98B15D8F06D6922546ABC76A22613903B2931960A20`.
Control runtime DLL SHA-256:
`F90407D521CE15C1877481279626E532F42664E01BC87D9472594A8122311B55`.

- Candidate pyperf JSON: [`dict-string-getitem-isolated-candidate-20261011.pyperf.json`](data/dict-string-getitem-isolated-candidate-20261011.pyperf.json)
- Control pyperf JSON: [`dict-string-getitem-isolated-control-20261011.pyperf.json`](data/dict-string-getitem-isolated-control-20261011.pyperf.json)
- Paired comparison: [`dict-string-getitem-isolated-compare-20261011.txt`](data/dict-string-getitem-isolated-compare-20261011.txt)
- Repeated dict-loop timings: [`dict-string-getitem-microbench-20261011.txt`](data/dict-string-getitem-microbench-20261011.txt)

The build used the fixed Release command:

```powershell
cmd.exe /c doc\performance\data\vm-active-code-view-reproduction-build-command-20261009.cmd
```

The official paired run used `--benchmarks richards,typing_runtime_protocols`
with `run_pyperformance_xlang3_shimmed.py`, mode `fast`, and
`--case-timeout 300`. The completed current-source full-suite comparison is in
the [r4 report](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147.md);
it does not show an overall suite speedup, and this small cache trial does not
validate the full performance goal.
