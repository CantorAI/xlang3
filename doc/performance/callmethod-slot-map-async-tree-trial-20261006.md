# CallMethod class-slot lookup trial (2026-10-06)

## Result

A per-bytecode-site cache for `ClassObject::instance_slot_indices[name]` did
not improve the official pyperformance `async_tree_none` benchmark. The
candidate measured 4.50 s ± 0.07 s versus 4.47 s ± 0.04 s for the control;
`pyperf compare_to` classified the difference as insignificant. The candidate
was reverted.

| Build | `async_tree_none` mean ± standard deviation | Relative speed |
|---|---:|---:|
| Fixed Release control | 4.47 s ± 0.04 s | 1.000× |
| Slot-map cache candidate | 4.50 s ± 0.07 s | 0.993× (not significant) |

The candidate cached only the class-level slot index, guarded by class identity
and version. It did not cache whether a particular instance shadows a method;
instance attributes and dictionary-backed attributes remained dynamically
checked on every call. The experiment therefore rejects this narrow string-key
lookup as a useful async-tree optimization. The profile's hashing samples must
be addressed in a hotter path before adding more call-site cache state.

Against the saved CPython 3.14.7 result of 227.4 ms, the control remains about
0.051× CPython speed (19.7× slower). This is a diagnostic result, not a
performance claim.

## Validation and build identity

The candidate built as Release and passed `xlang3_runtime_value_tests`,
`xlang3_interpreter_tests`, and the complete fixture runner using
`C:\Python\Python314\python.exe` (CPython 3.14.7). The fixed Release pair was
restored after measurement:

- Restored XLang3 executable SHA-256:
  `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548`
- Restored runtime DLL SHA-256:
  `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D`
- Candidate executable SHA-256:
  `4F96DD9406A0C56BBA8E464DFDE959C4BAA036A783D86A71954CD3736D4B8C6C`
- Candidate runtime DLL SHA-256:
  `1281C6842EF0A0BBF5197AA5161DE7594677966BFD42115B4E72C5096A211004`

Both runs used pyperformance 1.14.0, Python 3.14.7, the same Windows
compatibility shim and dependency site, and `--fast` mode (20 measured values
across 10 workers, one warmup per worker). The candidate reported a pyperf
stability warning, so small differences are especially weak evidence.

## Raw evidence

- [Control pyperf JSON](data/callmethod-slot-map-async-tree-control-fast-20261006.json)
- [Candidate pyperf JSON](data/callmethod-slot-map-async-tree-candidate-fast-20261006.json)
