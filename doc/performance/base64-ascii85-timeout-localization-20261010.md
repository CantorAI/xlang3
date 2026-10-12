# pyperformance base64 timeout localization — 2026-10-10

Status: the full fixed-binary run timed out in the `base64` definition after
300 seconds. The partial pyperf file is valid and retained; this note localizes
the next subtest but does not claim a timing or identify the exact hot opcode.

## Evidence

The active full run uses `--fast` and a 300-second per-definition cap. Its
`base64` output was preserved at
[`base64-f6d4418eaf5484f0.json`](data/pyperformance-xlang3-current-candidate-full-fast-20261010-r1-partial/base64-f6d4418eaf5484f0.json)
with provenance
[`base64-f6d4418eaf5484f0.provenance.json`](data/pyperformance-xlang3-current-candidate-full-fast-20261010-r1-partial/base64-f6d4418eaf5484f0.provenance.json).
The JSON is valid (13,115 bytes; SHA-256
`f6d4418eaf5484f0328fba1dfb0e090c95a1a577989dbe5209a3769ae457b630`) and has
20 values for each of eight subtests: `base64_small`, `base64_large`,
`urlsafe_base64_small`, `base32_small`, `base32_large`, `base16_small`,
`base16_large`, and `ascii85_small`. The runner then hit the definition cap
before recording `ascii85_large`.

The following directional comparison uses the arithmetic mean of each raw
sample set (20 values on both sides). CPython's values are from the saved
2026-10-07 full run, so this is an unpaired comparison; fast-mode warnings
remain relevant.

| Subtest | CPython 3.14.7 | XLang3 fixed Release | CPython / XLang3 |
|---|---:|---:|---:|
| `base64_small` | 0.270 ms | 2.50 ms | 0.11× |
| `base64_large` | 7.90 ms | 14.16 ms | 0.56× |
| `urlsafe_base64_small` | 0.410 ms | 5.22 ms | 0.08× |
| `base32_small` | 7.58 ms | 41.60 ms | 0.18× |
| `base32_large` | 382.89 ms | 1782.03 ms | 0.21× |
| `base16_small` | 0.290 ms | 3.14 ms | 0.09× |
| `base16_large` | 6.20 ms | 39.11 ms | 0.16× |
| `ascii85_small` | 16.04 ms | 94.31 ms | 0.17× |

These rows identify more than the timeout: even completed pure-Python
base32/base16/Ascii85 subtests run roughly 4.7–11× slower by this mean-of-raw-
samples calculation, while `base64_large` is about 1.8× slower. They remain
directional estimates, not a paired optimization result. The saved CPython
reference is
[`pyperformance-cpython3147-live-eval-full-fast-20261007.json`](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json).

The pyperformance benchmark registers `ascii85_large` immediately after
`ascii85_small`. It processes ten 100 KiB inputs plus one 1 MiB input per
iteration, with encode, wrapped encode, and two decode operations for each
input. This makes `ascii85_large` the next focused investigation target; the
timeout does not prove that one operation alone consumed all 300 seconds.

## Runtime path and design boundary

CPython 3.14.7's `base64.py` keeps Ascii85 in Python. `_85encode` unpacks a
large `!NI` record with `struct.Struct.unpack`, then runs a Python comprehension
over every 4-byte word and joins the byte chunks. `a85decode` also loops in
Python over encoded bytes. XLang3 already owns a native-compatible `_struct`
module, but those Python algorithms remain in the Python 3.14 standard
library. Do not translate or replace `base64.py` in C++; measure the runtime
cost of its Python loop, comprehension, integer, tuple, and byte operations.

## Next measurements

After the active full suite ends, run the unchanged `ascii85_large` function
directly on the fixed XLang3 Release executable and CPython 3.14.7, with the
same pyperformance data and `--fast` worker configuration. Capture wall time
and profiler-free samples first, then profile the specific runtime operations
only if the gap confirms the target. Any optimization must preserve the
stdlib algorithm and pass a base64/Ascii85 semantic fixture plus the unchanged
Release regression gate. The partial output belongs to the pre-edit full run;
it is evidence, not a completion result for the whole `base64` definition.
