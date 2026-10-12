# Rejected weakref callback-slot fast path — 2026-10-11

## Result

Reading the callback-free weakref flag directly from the exact builtin
reference's indexed slot made the primitive `weakref.ref(existing_target)`
and `inspect._shadowed_dict` diagnostics about 6% faster in alternating
parent/candidate runs. It did not improve the official pyperformance case:
`typing_runtime_protocols` measured **2.76 ms** before and **2.82 ms** with the
candidate, which pyperf reports as **1.02x slower**. The candidate also emitted
the unstable-result warning. The source fast path was removed and the fixed
Release path rebuilt from restored source. This trial is rejected; the
primitive result does not justify retaining it.

The restored-source rerun measured **2.85 ms ± 0.06 ms** and also warned that
the result may be unstable. That does not establish that reverting the path
regressed the benchmark; it confirms that these fast-mode samples do not
resolve a change of this size.

The paired, same-path diagnostic used the same Python 3.14.7 XLang3 helper,
20,000 warmed operations per primitive, and parent/candidate/candidate/parent
order. Parent versus candidate medians were:

| Primitive | Parent | Candidate | Parent / candidate |
|---|---:|---:|---:|
| Cached `weakref.ref` (`native_weakref_create`) | 18.61 ms | 17.57 ms | 1.059x |
| `inspect._shadowed_dict` | 96.31 ms | 90.75 ms | 1.061x |
| Python identity call control | 1.76 ms | 1.90 ms | 0.925x |

The control variation and the official case result show that this local
callback lookup is not a sufficient lever for the protocol slowdown. XLang3
remains roughly **21.5x slower** than CPython 3.14.7 on the official case.

## Validation and artifacts

- [Paired primitive output](data/typing-runtime-weakref-slot-callback-paired-20261011.txt)
- Candidate pyperf JSON: [typing-runtime-protocols-weakref-slot-candidate-20261011.pyperf.json](data/typing-runtime-protocols-weakref-slot-candidate-20261011.pyperf.json)
- Parent pyperf JSON: [typing-runtime-protocols-single-star-binder-current-20261011.pyperf.json](data/typing-runtime-protocols-single-star-binder-current-20261011.pyperf.json)
- Restored-source pyperf JSON: [typing-runtime-protocols-weakref-slot-restored-20261011.pyperf.json](data/typing-runtime-protocols-weakref-slot-restored-20261011.pyperf.json)
- The restored fixed Release build passes `weakref_registry_index`, `weakref_module`,
  and `weakref_thread_lifetime` fixtures, plus `xlang3_interpreter_tests.exe`.
- All builds used the fixed-path reproduction command documented in
  `data/vm-active-code-view-reproduction-build-command-20261009.cmd` and
  CPython `C:\Python\Python314\python.exe` 3.14.7 for pyperf.
