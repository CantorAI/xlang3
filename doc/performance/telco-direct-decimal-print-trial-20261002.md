# Direct Decimal formatting into `print`'s output buffer (2026-10-02)

`print_value_text()` converted every printed value into a temporary XLang
string and copied that text into its existing C++ output buffer. For exact
stdlib `Decimal` values, the existing formatter already reads the Decimal
slots directly. This trial exposed that formatting helper so `print` could
write the Decimal text to its output buffer without constructing and copying
the intermediate XLang string. Other values and customized Decimal classes
kept the existing `str()` path.

The candidate compiled, and the focused Decimal formatting, print/StringIO,
and Decimal arithmetic fixtures passed. The new fixture also checked that a
Decimal subclass and a monkey-patched `Decimal.__str__` still used normal
Python dispatch. After collecting the benchmark result, the trial was
discarded because it did not demonstrate a speedup:

| Build | Official pyperformance `telco` |
|---|---:|
| Matched control | 263 ms ± 13 ms |
| Direct-format candidate | 278 ms ± 26 ms |

`pyperf compare_to` reports the candidate at 1.06× the control time. The
candidate run was unstable, including a 419 ms maximum, so this is not evidence
of a real slowdown either. The formatting shortcut is not retained. Raw
results are [`control`](data/telco-print-keyword-control-rigorous-20261002.json)
and [`candidate`](data/telco-direct-decimal-print-candidate-rigorous-20261002.json).
