# Full-suite checkpoint against fresh CPython 3.14.7

The goal has not been reached. The fresh comparison confirms substantial
interpreter slowdowns and compatibility failures; the local Chameleon gain
does not establish a broad win over CPython.

## Complete run population

| Runtime | Definitions attempted | Workers completed | Worker failures/timeouts | Recorded subtests |
| --- | ---: | ---: | ---: | ---: |
| CPython 3.14.7 | 97 | 97 | 0 | 124 |
| XLang3 engine `168c79a0377dc259c0429930b156edc38883f990` | 97 | 68 | 29 | 77 |

The XLang3 failures comprise 14 timeouts and 15 worker deaths. Each definition
has a 300-second cap, with 600 seconds for NetworkX. Both suites used fast mode,
the same benchmark dependency directory and compatibility hook, and Python
3.14's standard library. The executable path remains
`build-repro/main-verify-20261006/Release/xlang3.exe`.

XLang3 ran from 17:04:32 to 19:28:41 UTC on October 7. The fresh CPython run
followed from 19:29:46 to 20:01:52 UTC and exited 0. CPython's executable was
`C:\Python\Python314\python.exe`, version 3.14.7. Both runs recorded unchanged
executable and runtime-library hashes between start and end. XLang3's original
provenance still identifies its originally planned older CPython reference;
the [joint comparison provenance](data/pyperformance-xlang3-super-method-call-vs-cpython3147-full-fast-20261007-comparison-provenance.json)
explicitly identifies the fresh reference without rewriting that record.

## Ratios and chart

The [full report and horizontal chart](pyperformance-xlang3-super-method-call-vs-cpython3147-full-fast-20261007.md)
score **76 subtests**, after excluding failed definitions and the demonstrated
incorrect Genshi XML workload. Geometric mean CPython time / XLang3 time is
**0.12665×**, equivalent to approximately **7.9× slower** for XLang3 on this
scored set. This aggregate does not assign times to failed cases or imply a
score across all 97 definitions.

Three nominal timing ratios exceed 1×: GC traversal (3.152×), Fannkuch (1.119×),
and pickle dictionary (1.012×). Fast-mode warnings and the small pickle margin
prevent treating these ratios as statistically established wins. GC traversal
does not prove correct cycle collection: the separate `gc_collect` definition
failed its collection assertion on XLang3.

| Selected subtest | CPython 3.14.7 | XLang3 | CP time / X time |
| --- | ---: | ---: | ---: |
| JSON dumps | 8.32 ms | 38.4 ms | approximately 0.217× |
| Python-only pickle | 279 µs | 5.69 ms | approximately 0.049× |
| Runtime typing protocols | 131 µs | 3.17 ms | approximately 0.041× |
| Startup without site | 16.0 ms | 19.9 ms | approximately 0.804× |

These displayed times are rounded; the [subtest CSV](data/pyperformance-xlang3-super-method-call-vs-cpython3147-full-fast-20261007-subtests.csv)
uses arithmetic means of raw measurement values, excluding calibration and
warmups. The [all-97 CSV](data/pyperformance-xlang3-super-method-call-vs-cpython3147-full-fast-20261007-all-97-status.csv)
records every definition, both runtime outcomes, and correctness exclusions.

## The apparent 50.9× XML gain was invalid

The official Genshi workload constructs 1,000 rows containing ten cells each.
The benchmark itself does not check its rendered output. The
[correctness probe](../../benchmarks/diagnostics/genshi_render_correctness.py)
uses the same templates and input, outside timing.

| Variant and runtime | Output characters | Rows | Cells | UTF-8 SHA-256 |
| --- | ---: | ---: | ---: | --- |
| XML, CPython | 112,017 | 1,000 | 10,000 | `61096eb9fee3ea72a4615a57e653bca545efacbad8aa8d522bb954d66d9421bc` |
| XML, XLang3 | 273 | 0 | 0 | `c0ed718ec5421dabf38b6fa566883bc4ad790bb45b6e8da8aa92b5f676068c8d` |
| Text, both runtimes | 112,018 | 1,000 | 10,000 | `c2e6154871cc315eadf1536e6b5f4679e3eee7aa6ed6ac3b9ea2e927ef2de810` |

XLang3's XML result still contains the unexpanded template and repeats it.
Its 915.7 µs timing versus CPython's 46.59 ms is preserved as raw evidence,
but receives no speed score, chart bar, win or aggregate contribution. The
[probe evidence](data/genshi-render-correctness-20261007.json) records both
outputs, loaded Genshi module paths, executable hashes and the probe hash.
CPython exited 0; XLang3 failed the row-count assertion.

Source review identifies two native `pyexpat` gaps to isolate next:
`namespace_separator` is stored but never applied to element/attribute names,
and each `Parse()` reparses the accumulated buffer from the beginning. These
are source-backed hypotheses for the observed unexpanded/duplicated output,
not a claim that a fix has been implemented. Genshi remains implemented in
Python; CPython's native Expat counterpart may be implemented in XLang3.

## Before/after scope

The [common-subtest data](data/super-method-full-common-cpython3147-20261007.json)
and [all common measurements](data/super-method-full-common-cpython3147-20261007.csv)
compare 74 subtests completed by both the frozen subscription checkpoint and
this checkpoint, using the same fresh CPython reference. The geometric mean
old-XLang3 / new-XLang3 time is **0.98551×**: the new run is nominally about
1.47% slower on this common set. Fast runs are not a paired significance test.
Chameleon and Genshi text are excluded from that common population because
their earlier benchmark definitions failed. Genshi XML is excluded for
incorrect output. SciMark completed in both full runs.

The older engine contained a native translation of Python typing Protocol
logic that was subsequently removed to comply with the user's rule. This
comparison spans several correctness and engine changes; it cannot attribute
the aggregate difference to the guarded super-call optimization alone. The
[isolated Chameleon checkpoint](super-method-call-checkpoint-20261007.md)
records its approximately 6% reduction against the preceding engine, together
with matching rendered output and the unchanged fixed regression gate.

## Reporting validation and next engine work

Nine reporting tests passed on CPython 3.14.7. They cover failed partial results,
fresh subtest names absent from a historical failed index, missing outcomes,
chart bounds, and evidence-backed exclusion of incorrect workloads. The chart
was rendered to PNG and visually inspected. Reporting changes do not change
the measured executable.

Checkpoint-specific Git attributes preserve hashed raw evidence byte for byte
instead of converting Windows line endings during check-in. Historical records
are outside those patterns.

The next measurements will isolate native factorial and generic bigint
multiplication, rather than repeat rejected method-cache trials. The source
currently clones bigint operand limbs before multiplication and allocates
limbs for scalar operands. A guarded immutable operand view may remove these
copies; it must preserve aliases, input lifetimes and wrapper callbacks, and
show a measured benefit before acceptance. Preserve this Release checkpoint
before changing the engine, then require correctness fixtures, C++ tests,
the complete fixed-baseline regression gate and an affected official benchmark.

Raw XLang3 failed Base64 partials remain in
`data/pyperformance-xlang3-super-method-call-full-fast-20261007-partial/`;
they do not contribute scores. No failed case has been removed, no historical
benchmark record overwritten, and no accepted baseline changed.
