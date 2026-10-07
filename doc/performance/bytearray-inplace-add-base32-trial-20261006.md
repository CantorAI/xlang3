# Native bytearray in-place add fixes the Base32 quadratic slowdown (2026-10-06)

## Finding

The official `base32_large` workload in pyperformance 1.14.0 was spending most
of its time repeatedly copying the growing output buffer. CPython 3.14.7's
`Lib/base64.py` builds its output with `bytearray += bytes`. XLang3 had no native
`bytearray.__iadd__`, so augmented addition fell through to `value_add`, which
allocated a new bytearray and copied the full accumulated prefix on every
iteration. This made the pure-Python Base32 loop quadratic in the output size.

The fix implements the mutable-buffer operation in XLang3's native bytearray
methods and routes bytearray augmented addition directly to it. The standard
library `base64.py` remains Python code. The code comment records why the
in-place path is required and guards against accidentally reintroducing the
copying fallback.

## Comparison

This is the exact official pyperformance `bench_b32_large(loops=1)` function:
ten encode/decode pairs on 100 KiB data, followed by one pair on 1 MiB data.
All one-value measurements used the fixed Release executable path and the same
official benchmark function. CPython was **3.14.7**.

```text
CPython 3.14.7       0.435 s |█                                      | 1.0×
XLang3 candidate     1.90 s  |████                                   | 4.4× slower than CPython
XLang3 control      38.5 s   |███████████████████████████████████████| 88.5× slower than CPython
```

The candidate is **20.3× faster** than the pre-change XLang3 control on this
workload. It remains about **4.4× slower than CPython**, so the Base32 gap is
substantially smaller but not closed.

The repeated fast-mode candidate run measured **1.83 s ± 0.06 s** over three
values; pyperf warned that the sample count was too small to establish stable
sub-1% variation. A same-mode one-value control/candidate pair is therefore
the clearest direct comparison. A three-value control attempt took several
minutes per worker and was stopped; no claim of statistical significance is
made from the one-value comparison.

## Wider Base64 run

The official pyperformance `base64` case was run with a 300-second case cap.
It measured `base32_large` at **1.81 s**, but the complete case exceeded the cap
while progressing through later Base64 subtests and did not produce a complete
benchmark JSON. The per-case output warned that fast-mode samples were
unstable. This is not a completed Base64 suite result.

The remaining work is to investigate the later pure-Python Base64 paths and
the residual 4.4× Base32 gap through interpreter/runtime operations, keeping
the library algorithms in Python.

## Follow-up diagnosis

Isolated official workload components show the remaining Ascii85/Base85 gap
is spread across both directions: XLang3 took **1.27 s** for Ascii85 encoding
versus **253 ms** on CPython, **3.27 s** for Ascii85 decoding versus **668 ms**,
**572 ms** for Base85 encoding versus **123 ms**, and **1.04 s** for Base85
decoding versus **218 ms**. These were one-value diagnostic runs.

The native `struct.Struct.unpack` call over the official 1 MiB payload took
**4.04 ms** in XLang3 and **8.65 ms** in CPython, so `_struct` is not the source
of the Base85 slowdown. A direct byte-iterator optimization was also screened:
three-value Ascii85 decode pyperf runs measured **3.35 s** before and **3.23 s**
after (1.04× nominal). Both runs warned about sample stability, so that change
was rejected and reverted.

## Correctness and files

The new fixture verifies in-place identity for bytes, bytearray, and memoryview
inputs and the `BufferError` when an exported buffer prevents resizing. The
complete Python fixture suite, `xlang3_runtime_value_tests.exe`, and
`xlang3_interpreter_tests.exe` passed.

Raw pyperf output:

- `data/base32-large-bytearray-iadd-control-debug-20261006.json`
- `data/base32-large-bytearray-iadd-candidate-debug-20261006.json`
- `data/base32-large-cpython314-debug-20261006.json`
- `data/base32-large-bytearray-iadd-candidate-3values-20261006.json`
- `data/ascii85-large-xlang3-candidate-debug-20261006.json`
- `data/ascii85-large-cpython314-debug-20261006.json`
- `data/base85-large-xlang3-candidate-debug-20261006.json`
- `data/base85-large-cpython314-debug-20261006.json`
- `data/a85_encode-xlang3-debug-20261006.json`
- `data/a85_encode-cpython314-debug-20261006.json`
- `data/a85_decode-xlang3-debug-20261006.json`
- `data/a85_decode-cpython314-debug-20261006.json`
- `data/b85_encode-xlang3-debug-20261006.json`
- `data/b85_encode-cpython314-debug-20261006.json`
- `data/b85_decode-xlang3-debug-20261006.json`
- `data/b85_decode-cpython314-debug-20261006.json`
- `data/base64-struct-unpack-xlang3-candidate-debug-20261006.json`
- `data/base64-struct-unpack-cpython314-debug-20261006.json`
- `data/a85-decode-byteiter-control-20261006.json`
- `data/a85-decode-byteiter-candidate-20261006.json`

The fixed run directory was not changed. Its current runtime DLL SHA-256 is
`828F125B1C6BEE78519F54A3EAA4D5E4F8356C25530ED038FD26A8EC9B06CA4A`.
