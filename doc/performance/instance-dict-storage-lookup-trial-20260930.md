# Skip ordinary-instance dictionary-marker scans (2026-09-30)

## Change and implementation boundary

`instance_attribute_storage()` is called from attribute, slot, descriptor, and
method lookup paths. It previously scanned every instance's `attrs` vector for
the internal `#__dict__` marker on each call. That marker is present for dict
subclasses, whose mapping entries and Python attributes use separate storage;
ordinary instances do not have it. `InstanceObject` now records whether the
marker is present. Ordinary instances return their existing mapping storage
without scanning the attribute vector. Dict-subclass construction, generic and
VM attribute stores, slot-descriptor stores/deletes, object recycling, and
graph deserialization keep the flag synchronized.

The code comment beside the accessor states the hot-path reason and the writer
invariant so later changes preserve the optimization. This is a generic XLang3
object-model change. It does not port or reimplement `pickle.py` or any other
pure-Python standard-library module in C++. Native module implementations stay
within the CPython-native module boundary; this change is in the VM/runtime.

## Results

These are official `pyperformance` 1.14.0 / `pyperf` 2.10.0 rigorous runs on
the same Windows host and XLang3 executable. The XLang3 parent and candidate
differ only in their runtime DLL. The final pair ran the candidate first and
the control second. Lower elapsed time is better.

| Benchmark | XLang3 parent | XLang3 candidate | XLang3 speedup | CPython 3.14.7 | Candidate vs CPython |
| --- | ---: | ---: | ---: | ---: | ---: |
| `pickle_pure_python` | 6.37 ±0.07 ms | 6.34 ±0.10 ms | 1.01× (`t=3.13`) | 252 ±5 μs | 0.040× throughput (25.13× slower) |
| `unpickle_pure_python` | 4.31 ±0.05 ms | 4.29 ±0.06 ms | 1.00× (`t=2.46`) | 160 ±2 μs | 0.037× throughput (26.90× slower) |

The candidate trims only about 0.5% from `pickle_pure_python`; unpickling is
effectively unchanged. The geometric mean is 1.00× after pyperf's two-decimal
rounding. The result is a small incremental gain, not a fix for the roughly
25–27× pure-Python gap.

```text
pickle_pure_python  (bars scaled within this row; lower is faster)
CPython 3.14.7       252 μs  █
XLang3 parent       6.37 ms  ████████████████████
XLang3 candidate    6.34 ms  ████████████████████

unpickle_pure_python
CPython 3.14.7       160 μs  █
XLang3 parent       4.31 ms  ████████████████████
XLang3 candidate    4.29 ms  ████████████████████
```

The CPython 3.14.7 reference files were collected in the preceding rigorous
pickle investigations. They use the same official benchmark definitions and
host, but are not simultaneous samples with this final XLang3 pair.

## Validation and evidence

The Release build succeeded. The complete Python-driven fixture suite passed.
Nine focused object-model fixtures also passed against both the parent and
candidate runtimes: instance dictionary assignment/dispatch, dict-subclass
visibility and initialization, multiple inheritance, attribute hooks,
descriptor lookup, slots, and arbitrary dict-instance attributes.

The final candidate passed all 11 fixed-baseline Release checks with nine
order-balanced pairs and two warmups. It also passed all 11 checks against its
immediate parent; candidate/parent elapsed ratios ranged from 0.963× to 1.032×,
within the 10% regression allowance. The two raw gate reports preserve every
paired sample and binary hash: [fixed baseline](data/instance-dict-storage-final-fixed-baseline-20260930.json)
and [immediate parent](data/instance-dict-storage-final-vs-parent-20260930.json).

CTest passed 52 of 53 tests. Its Windows PowerShell fixture wrapper reports a
Unicode expectation mismatch in `json_module`: the runtime emits the correct
UTF-8 text, while the wrapper decodes the expected fixture as mojibake. Running
the same filtered CTest with the parent runtime produces the same failure. The
full Python fixture runner passes, so this is a pre-existing wrapper encoding
issue rather than a candidate regression.

The final XLang3 executable SHA-256 is
`1551FD3479BADE99AFCCC047272C4E1934150A35586F49ACB13C1215868C8794`.
The parent runtime DLL is
`FF1BD7C84A24909E417662B6CD5BE7B9E05E6577536FC460842F60141B97D3B2`; the
candidate runtime DLL is
`F38F8A5AA45F1ACF33BBA0D97EA972C804F7CAFF783309DA151F3EA5649D61EF`.

The final raw pyperf files are [parent](data/instance-dict-storage-control-final-rigorous-20260930.json)
and [candidate](data/instance-dict-storage-candidate-final-rigorous-20260930.json),
with the corresponding [parent log](data/instance-dict-storage-control-final-rigorous-20260930.log)
and [candidate log](data/instance-dict-storage-candidate-final-rigorous-20260930.log).
The CPython references are [pickle](data/pure-pickle-generic-bound-cache-cpython314-rigorous-20260929.json)
and [unpickle](data/unpickle-pure-python-bytesio-read-fastcall-rigorous-cpython314-20260929.json).

An earlier [XLang3 call profile](data/pyperformance-pickle-pure-python-xlang3-profile-20260929.txt)
and [CPython profile](data/pyperformance-pickle-pure-python-cpython314-profile-20260929.txt)
record the same Python call counts, including 6,080 `pickle.py:save` calls and
10,420 `pickle.py:write` calls per profiled workload. A separate [VM-counter
run](data/pyperformance-pickle-pure-xlang3-vm-counters-20260929.txt), which
also includes import and setup work, records repeated `LoadLocalAttr`,
`CallMethod`, and `Call` dispatches. That points to XLang3's generic Python
execution cost as the remaining target; it does not indicate fallback to
CPython. The profiles are instrumentation diagnostics, not timing results.
