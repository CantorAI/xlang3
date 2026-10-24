# Function defaults must beat dictionary shadows (2026-10-06)

Checkpoint `07749377` fixed live keyword defaults and executable code
replacement, but the official NetworkX cases still failed in an inner
`parse_adjlist` wrapper. A separate function metadata lookup defect explains
how decorator copying can leave that wrapper bound to the wrong default.

NetworkX's `_dispatchable` instance stores `__defaults__` and `__kwdefaults__`
in its instance dictionary. Its argmap decorator copies that dictionary into
a new Python function, then updates the function's keyword default to point
to itself. XLang3 looked in `function.__dict__` before its native defaults
storage, so the final update could mutate a copied shadow dictionary instead
of the dictionary used by argument binding.

CPython's function defaults are data attributes and take precedence over
same-named dictionary entries. The
[small reproduction](../../benchmarks/diagnostics/function_kwdefaults_shadow_probe.py)
copies a shadow entry and mutates the getter's result. The
[checkpoint output](data/function-kwdefaults-shadow-probe-20261006.txt)
reports both checks false under XLang3 and true under CPython 3.14.7.

The candidate moves the `__defaults__` and `__kwdefaults__` getters ahead of
dictionary lookup. It retains the shadow entries in `__dict__`, as CPython
does, while exposing the actual defaults. Argument binding and optimized VM
paths are unchanged. The comment beside these getters explains why decorator
dictionary copying must not shadow native defaults storage.

The expanded live-default fixture covers both positional and keyword shadows,
dictionary identity, subsequent mutation, and preservation of the raw shadow
entry. The [candidate probe](data/function-defaults-precedence-candidate-probe-20261006.txt)
now matches CPython 3.14.7. The complete fixture runner, runtime/interpreter
C++ tests, SDK stream/call tests, and graph producer/consumer test also pass.

The existing VS 18/Ninja Release build and executable path are unchanged:
`D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe`.
CPython remains `C:\Python\Python314\python.exe`, version **3.14.7**.
Candidate executable SHA-256 is
`9AD9C13FF8BFC34068866FDEF2C1BF3875177B342F995A87BE41BC52F6FC6A61`;
runtime DLL SHA-256 is
`3023F4A8CD7A8B5C0F9099A90C85E3C7FD30F68214AF66973B0E5B67DB5C03E2`.

The [complete fixed Release gate](data/function-defaults-precedence-fixed-release-gate-20261006.json)
passed all 11 cases with the unchanged defaults: 21 repeats, 5 warmups, and a
10% threshold. The largest candidate/baseline ratio was 1.033. These are
XLang3 build comparisons, not speedups over CPython.

The three official NetworkX definitions were retried once in fast mode with
a 300-second full-case cap and the same dependency site. Both lazy wrappers
now execute their generated code: the previous `__argmap__` failures are gone.
All three cases then fail during graph loading with
`TypeError: 'GzipFile' object is not iterable`, before any timing is collected.
The [complete attempt log](data/pyperformance-xlang3-defaults-precedence-networkx-fast-20261006.log)
retains that outcome. No NetworkX timing or full-suite completion gain is claimed.

The [small GzipFile probe](../../benchmarks/diagnostics/gzipfile_iteration_probe.py)
and its [captured output](data/gzipfile-iteration-probe-20261006.txt) separate
successful decompression from the remaining iteration failure. The next
investigation is generic/native I/O iteration used by the existing Python
`gzip.GzipFile` implementation; it must not replace that pure-Python library
with C++ code.

The most recent completed full suite remains the
[all-97 run](pyperformance-xlang3-bytearray-iadd-full-fast-20261006.md): 53
completed cases, 44 failures, and about 6.39 times slower than CPython 3.14.7
across 56 matched subtests. This checkpoint fixes decorator semantics; it does
not demonstrate achievement of the performance goal. The local candidate
includes existing worktree edits outside this small checkpoint.
