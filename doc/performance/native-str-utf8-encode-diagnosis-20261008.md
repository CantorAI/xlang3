# Native UTF-8 entry-point diagnosis

Selected fast Python operations do not establish that XLang3 executes every
Python workload faster than CPython. A full workload combines local operations
with function dispatch, attributes, allocations and native library operations.
The [latest all-97 attempt](pyperformance-xlang3-dict-scalar-append-vs-cpython3147-full-fast-20261008.md)
completed 73 definitions and failed 24. Its pure-Python pickle subtest took
4.087231 ms versus the saved CPython 3.14.7 reference's 0.257498 ms: 15.872874
times the elapsed time, or 0.063001 times CPython's speed. This is an unpaired
fast-mode comparison, not a statistically established whole-suite score.

The dictionary-index repair already retained on main improved the unchanged
original pickle-body diagnostic by a median 1.380379 times against its preserved
XLang3 parent (seven pairs, 95% bootstrap interval 1.345227 to 1.398242).
That incremental gain does not establish a win against CPython.

## Concrete difference

In the preserved parent used for this diagnosis, `string_encode_method` in
`src/runtime/methods/string_methods.cpp` takes common UTF-8 requests through
`__import__("_codecs")`, registry lookup, `_is_text_encoding` attribute access,
then `_codecs.encode`. The native `codecs_encode` repeats lookup and accesses
the codec object's `encode` attribute before calling XLang3's existing UTF-8
kernel in `src/runtime/modules/system/codecs_module.cpp`.

CPython 3.14.7's
[PyUnicode_AsEncodedString](https://github.com/python/cpython/blob/v3.14.7/Objects/unicodeobject.c#L3680)
directly selects native UTF-8 encoding for the default and common normalized
UTF-8 names. Other encoding names retain the registry route.

The fresh post-dictionary-repair native sample contains class attribute lookup
stacks reached from `string_encode_method`. These are location observations,
not percentages of CPU time, and are not all `getattr` calls. This motivated
the separate entry-point diagnostic below.

## Entry-point diagnostic

[Raw diagnostic receipt](data/native-utf8-encoding-current-vs-cpython3147-20261008.json)
SHA-256: `edcefb28ec3dee4cfed88d6b69c73ad6f2a5973276a24afd370dc5edf57f06c8`.

One CPython 3.14.7 process and one unchanged XLang3 Release process each executed
ten cases with five samples of 20,000 operations per case. Untimed checks
verified literal expected bytes, checksums, hashes, consumed characters and
strict surrogate error positions/types. Process observation and before/after
hash checks passed. No profiler or codec monkeypatch was used in these timings.

The table compares `text.encode('utf-8', 'surrogatepass')` with
`_codecs.utf_8_encode(text, 'surrogatepass')[0]` within each runtime. Each column
uses the same input and API spelling between runtimes.

| Input | XLang3 method time / direct codec time | CPython method time / direct codec time |
|---|---:|---:|
| ASCII | 3.102553 | 0.709292 |
| Multibyte Unicode | 3.587438 | 0.764809 |
| Surrogates with `surrogatepass` | 3.700013 | 0.796842 |

XLang3's ordinary method route was slower than its direct native codec route;
CPython's method route was faster. The direct XLang3 codec route itself was
still slower than CPython, so eliminating registry overhead alone cannot be
claimed to close the entire gap.

These are unscored diagnostics: five samples within one process per runtime,
CPython first, no confidence interval, no original-case speedup and no candidate
acceptance. The whole-pickle effect must be measured after implementation.

## Trial constraints

Reuse XLang3's native UTF-8 kernel; keep CPython's pure-Python pickle library in
Python. Validate receiver, argument types and supported error names before a
shortcut. Keep subclass overrides, other encodings and custom handlers on the
existing paths where the shortcut cannot prove equivalence. Preserve keyword
validation, strict error ranges, input/result lifetimes and the observed native
method boundary. In particular, canonical aliases such as `u8` and `cp65001`
must not accidentally expand a guard intended for CPython's direct UTF-8 names.

The existing native UTF-8 custom-error-handler limitation on surrogate failures
is separate; this trial must not claim to repair it. A retained shortcut needs
permanent semantic fixtures, useful improvement on seven preserved-parent /
candidate pairs of the original pickle body, full correctness, the unchanged
fixed 11-case regression gate (21 repeats, five warmups, 10% tolerance), and the
original official pyperformance case. Keep all failed or inconclusive evidence.

## Candidate screen and correctness checkpoint

The frozen candidate adds a guarded `runtime_encode_utf8` call after argument
validation and before encoding/error normalization. The helper owns the input,
uses the unchanged native codec kernel and publishes a local result. Existing
keyword validation and the outer native method observer remain in place. The
new fixture is registered in both fixture runners.

[Applied source receipt](data/native-str-utf8-applied-source-20261008.json):
`96ba3820206e00029a26231b15accb79ff94e1ba33098d769e586d7a28b12f83`.
Recorded sources: 119; this is a partial source inventory, not every compiled
file or transitive header. The same existing Release directory is used; the
preserved parent and accepted fixed baseline remain unchanged.

The proposed eight-group semantic fixture passed first on CPython 3.14.7 and
then on XLang3. [Focused checks](data/native-str-utf8-focused-20261008.json)
also passed the C++ interpreter executable and six related Python fixtures.
These check UTF-8 spellings, strict/surrogate modes, keywords, module-facade
bypass, custom codec/error fallback, subclasses, pickle and observer events.

The [seven-pair original-body screen](data/native-str-utf8-original-pickle-paired-20261008.json)
completed all 14 children with matching original pickle byte/roundtrip
signatures, valid process observation and unchanged pinned hashes. No values
were cut or replaced. The median parent time divided by candidate time was
**1.106001**; the fixed-seed 50,000-resample 95% bootstrap interval was
**[1.101779, 1.114201]**. This is an incremental improvement against the XLang3
parent, not a CPython win or an official pyperformance score.

The [first full validation attempt](data/native-str-utf8-validation-20261008.json)
retains its failed-controller status. The 399 core fixtures, 11 compatibility
sections, three expected failures and nine selected CTests actually passed.
The controller stopped after CTest because the process watcher classified the
owned CTest process (PID 35388) as external activity. Raw CTest exit code was
zero, its transcript reports all nine passes, and its stderr was empty. The
watcher recorded only that owned PID and no scanner errors. This untimed
correctness bookkeeping issue does not establish a timing failure or an engine
test failure. The original receipt and false watcher flag must remain intact.

At that first checkpoint, the remaining two SQLite API checks, fixed performance
gate and fresh official comparison were pending.

## Completed validation and fresh official comparison

The [continuation receipt](data/native-str-utf8-validation-resume-r2-20261008.json)
authenticates and reuses only the three already executed, untimed correctness
phases. It leaves the original failed receipt and CTest watcher flags unchanged,
records a separate semantic pass for the exact owned CTest process, and does not
change the live timing watcher. Both SQLite API checks then passed freshly.

The [fixed performance gate](data/native-str-utf8-validation-resume-r2-20261008-fixed-gate.json)
passed all 11 default cases with 21 repeats, five warmups and the unchanged 10%
tolerance against the accepted baseline. The continuation finished with status
`trial_validated`, `full_validated: true` and unchanged hashes. Its SHA-256 is
`d40024a60374dc2ccfa70a411f5f26f0a4177d4f870f8eb2e70ff0216bf6cba2`.

Fresh official `pickle_pure_python` results use the original benchmark,
pure-Python `pickle`, protocol 5 and `inner_loops=20`. Each runtime retained 20
measurement values; pyperf calibrated outer loop counts independently and
normalized elapsed times in the usual way.

| Runtime | Mean time | Speed relative to CPython 3.14.7 |
|---|---:|---:|
| CPython 3.14.7 | 0.257790 ms | 1.000000x |
| XLang3 UTF-8 candidate | 3.570568 ms | 0.072199x |

Speed is CPython time divided by runtime time; larger is faster. XLang3 still
takes **13.850687 times CPython's time** on this case. The fresh runs are
unpaired and in fast mode, and their stability warnings remain in the raw logs.
They validate the affected official benchmark, not a new all-97 attempt or a
whole-suite win. The earlier all-97 report remains a report of its own older
XLang3 binary and saved CPython reference.

- [XLang3 official values](data/native-str-utf8-validation-resume-r2-20261008-official-xlang3-pickle-fast.json)
- [CPython 3.14.7 official values](data/native-str-utf8-validation-resume-r2-20261008-official-cpython3147-pickle-fast.json)

The seven-pair original-body result establishes the incremental gain against
the preserved XLang3 parent; it must not be replaced by an unpaired percentage
computed from different official runs. This checkpoint uses the current
workspace build with existing unrelated changes held fixed. The source119
inventory is partial; it does not certify that every compiled source/header
matches a clean Git checkout. The overall CPython performance goal remains
unfinished.

## Retained numerical data

- [All 14 original-body observations](data/native-str-utf8-checkpoint-20261008/original-body-all14.csv)
- [All 40 fresh official values](data/native-str-utf8-checkpoint-20261008/official-pickle-all40.csv)
- [Fixed 11-case gate summary](data/native-str-utf8-checkpoint-20261008/fixed11-gate-summary.csv)
- [All recorded gate arrays](data/native-str-utf8-checkpoint-20261008/fixed11-gate-all-values.csv)

Gate ratios below are candidate time divided by accepted baseline time; lower is faster. Every case passed the unchanged gate. These are gate checks, not whole-suite scores.

| Case | Time ratio | 95% interval |
|---|---:|---:|
| local_slots | 0.987004 | [0.975436, 1.000354] |
| scalar_arithmetic | 1.008947 | [0.998312, 1.013271] |
| range_for | 1.007123 | [0.999356, 1.022523] |
| function_calls | 0.984473 | [0.974386, 0.996685] |
| class_construct | 1.013920 | [1.002213, 1.028785] |
| list_append | 1.010770 | [0.964868, 1.031087] |
| property_access | 0.992806 | [0.974478, 1.004596] |
| deepcopy_memo | 0.904772 | [0.869770, 0.909114] |
| json_dumps | 1.014862 | [1.003520, 1.029555] |
| gc_traversal | 0.763662 | [0.734679, 0.782407] |
| subparsers | 0.931010 | [0.920259, 0.957750] |
