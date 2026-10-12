# BytesIO exact-append trial for pure-Python pickle (2026-10-05)

## Question

Can _io.BytesIO.write avoid one temporary allocation and copy when the pure-Python Pickler appends exact immutable ytes at the end of a native BytesIO buffer?

## Comparison

All XLang3 runs used the same uild-repro\\Release path and CPython **3.14.7** at C:\\Python\\Python314. The benchmark was the official pyperformance 1.14 pickle_pure_python benchmark through enchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py, in ast mode.

| Runtime / revision | Result |
| --- | ---: |
| XLang3 control | 5.29 ms ± 0.08 ms |
| XLang3 candidate | 5.42 ms ± 0.29 ms |
| CPython 3.14.7 | 265 µs ± 22 µs |

pyperf compare_to --verbose hid the control/candidate comparison as not significant. The harness also emitted its fast-mode stability warning, so the 2.5% nominal candidate regression is not evidence of a real slowdown. This change did not demonstrate a speedup and is rejected.

## Implementation tested

For an exact immutable bytes argument to exact binary _io.BytesIO.write, when the cursor was at the end, the candidate appended directly from the bytes object view instead of materializing a temporary std::string and copying that string into the buffer. Writes that overwrite existing bytes and all other inputs retained the generic path.

## Correctness and restoration

The candidate built successfully. xlang3_runtime_value_tests and xlang3_interpreter_tests passed. The io_module_streams.py fixture output matched control; both emitted the same ResourceWarning on stderr. The source change was reverted and the fixed Release artifacts restored from the saved control copies.

Control SHA-256: xlang3.exe 4E014A32CDB168B1B10965B0BB242824F777DF0E4C9B635FE2435A39B88E6414; xlang3_runtime.dll 539C434DF8EC85F35CBACEFF4402359C8432FB0B5E7E81B3F4B35A80AD6688BF.

Candidate SHA-256: xlang3.exe 13DA46FA6B606FB85E461FAC99E05CAF8CB08DF24148F641261B39A6B870A48C; xlang3_runtime.dll DFDCD378D00F5BC7B610E1FD056033A91308D6DB1F8B7568C34525183C2BA396.

## Decision

Do not retry this copy-elision fast path. The result suggests the measured pickle gap is not dominated by this particular BytesIO copy; focus subsequent work on the much larger call/frame or Python-level dispatch cost, and accept an optimization only with a matched pyperf comparison.
