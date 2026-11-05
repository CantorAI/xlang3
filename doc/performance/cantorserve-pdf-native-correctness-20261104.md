CantorServe's real Chat attachment test rejected an ordinary text PDF with
pypdf 6.21.4. Two native runtime errors caused the failure. The keyword typo
precheck rejected pypdf's valid `for im in images` loop before parsing. After
that was corrected, `BytesIO.read(NumberObject(length))` consumed the remainder
of the file, because memory-stream reads recognized only exact integer values.

The parser now distinguishes the loop target and iterable from a misspelled
membership keyword, retaining the diagnostic for `for item im images`.
Memory-stream reads normalize integer subclasses and the `__index__` protocol,
reject invalid and overflowing sizes, and preserve the existing exact-integer
path. The pure Python pypdf library and document resource limits are unchanged.

Validation on a staged Windows Release runtime passed the C++ parser tests,
the complete I/O stream fixture, and the simple-suite, logical-line and statement
syntax fixtures. Native XLang3 extracted the actual PDF text and rejected an
encrypted PDF, a scanned page without text, a 101-page PDF and malformed input.

The full default performance gate passed all 11 cases against a separately
preserved Release executable and runtime, using the unchanged default 21
order-balanced paired samples, five warmups and 10% threshold. The first run
was inconclusive on two cases. The complete rerun inherited CPU affinity for
logical CPU 4 to avoid hybrid-core migration; no workload or threshold changed.
Both raw reports are retained in `data/cantorserve-pdf-native-20261104`.

The full fixture runner also encountered `native_array_shared_buffer`: its
expected BufferError was a TypeError with the unchanged extension assets.
The preserved baseline reproduces exactly the same failure. This work does not
claim that the entire fixture suite or fleet has passed.
