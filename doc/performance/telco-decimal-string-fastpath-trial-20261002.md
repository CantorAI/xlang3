# `telco` Decimal string-conversion fast path (2026-10-02)

The unmodified `Decimal.__str__` path in `_pydecimal.py` spends Python calls
formatting the value from four immutable slots. The benchmark calls `print(t)`
for every record, so `builtin_str_from_value` now formats those slots directly
for the exact, unmodified stdlib `decimal.Decimal` class. Subclasses,
monkey-patched methods, unusual layouts, and extreme exponents retain the
regular descriptor/Python path. The shortcut changes only conversion to text;
arithmetic still uses `_pydecimal`.

The new fixture checks signed zero, fixed and scientific notation boundaries,
trailing zeros, NaN payloads, infinities, and a subclass override against
CPython 3.14.7. The fixture passes on the candidate.

Five alternating single-loop runs of the unchanged official `bm_telco`
function produced these median wall times:

| Runtime | Median | Relative speed |
| --- | ---: | ---: |
| Fixed XLang3 Release control | 3.5324 s | 1.000x |
| Candidate with slot formatter | 3.4101 s | 1.036x |
| CPython 3.14.7 | 5.820 ms | 606.6x faster than control |

This direct runner is a diagnostic, not a pyperf result; its logs record five
single-loop samples per runtime in
[`data/telco-decimal-str-direct-repeats-20261002.log`](data/telco-decimal-str-direct-repeats-20261002.log)
and [`data/telco-decimal-str-cpython-direct-20261002.log`](data/telco-decimal-str-cpython-direct-20261002.log).
The candidate log is in
[`data/telco-decimal-str-candidate-direct-20261002.log`](data/telco-decimal-str-candidate-direct-20261002.log),
and the fixed Release control's initial sample is in
[`data/telco-decimal-str-control-direct-20261002.log`](data/telco-decimal-str-control-direct-20261002.log).

An official pyperformance rigorous attempt hit the 300-second case cap before
producing a result; it is preserved in
[`data/telco-decimal-str-control-rigorous-20261002.log`](data/telco-decimal-str-control-rigorous-20261002.log).
The direct measurements show a repeatable small improvement, but they do not
address the dominant gap: Decimal multiplication, addition, and quantize still
execute the pure-Python fallback. The next work must target those operations
through an XLang3 implementation of CPython's native `_decimal` boundary; the
stdlib `decimal.py` and `_pydecimal.py` remain Python.
