# JSON ASCII run-append trial (2026-10-01)

## Finding

The native `_json` module is already registered and handles `json.dumps` in
XLang3. To test whether its per-byte string output was material, I changed the
ASCII encoder to append contiguous printable runs in one `std::string::append`
and added a fixture covering long runs around quotes, slashes, and controls.
The fixture and native JSON tests passed.

The official pyperformance `json_dumps` case did not show a repeatable gain in
two opposite-order rigorous comparisons. The first order was insignificant;
the reverse order favored the control. The source and fixture change are
removed, since the result does not justify extra scanning logic.

| Order | Runtime | Mean ± standard deviation | Comparison |
| --- | --- | ---: | --- |
| Control, then candidate | Control | 36.4 ± 3.1 ms | — |
| Control, then candidate | ASCII-run candidate | 35.9 ± 3.8 ms | Not significant |
| Candidate, then control | ASCII-run candidate | 36.9 ± 4.8 ms | 1.06× slower |
| Candidate, then control | Control | 34.8 ± 0.6 ms | — |

Raw pyperf results are preserved in
[`json-ascii-control-rigorous-20261001.json`](data/json-ascii-control-rigorous-20261001.json),
[`json-ascii-candidate-rigorous-20261001.json`](data/json-ascii-candidate-rigorous-20261001.json),
[`json-ascii-candidate-repeat-rigorous-20261001.json`](data/json-ascii-candidate-repeat-rigorous-20261001.json),
and
[`json-ascii-control-repeat-rigorous-20261001.json`](data/json-ascii-control-repeat-rigorous-20261001.json).

This trial does not explain the suite-level gap. The next work stays focused on
the shared VM costs supported by the CPython 3.14.7 source comparison and
instrumented profiles: dispatch, generic calls, and frame handoff.
