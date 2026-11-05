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

Linux qualification also passed all 11 default cases on the idle XPS build
host, using the preserved production Linux Release runtime and the candidate
built with the same Clang 19 `-O3 -DNDEBUG` settings. Only the parser and I/O
translation units changed; the unchanged VM object was reused. Both processes
were verified to map their respective runtime library without libpython.
The gate kept the default 21 paired samples, five warmups and 10% threshold,
with inherited CPU 4 affinity. A systemd unit bounded the run to 16 tasks,
1 GiB, one CPU and 30 minutes. It completed in 108.7 seconds with exit 0.

The initial isolated Linux invocation exited 2 before measuring a case because
it could not locate `site.py`. The successful run supplies the existing
standard-library directory equally to both runtimes. Both reports, the result
receipt and the runtime mapping check are retained alongside the Windows
evidence. No benchmark ran on the production Azure VM for this qualification.

The candidate passed the actual bounded document-parser subprocess on Azure,
including text extraction, encrypted/scanned PDF rejection and the 100-page
limit. Its runtime library was then installed atomically for newly started
processes; the existing Cantor Main, API and database worker were preserved.
This installation does not establish full fleet rollout or throughput
qualification.

After installation, the real public Chat PDF upload, extraction and model-answer
case passed. The encrypted-PDF rejection case also passed. The full Chat
regression finished with 16 passed and zero failed, including image input,
native Garnet speech recognition/synthesis, conversation and web search.
