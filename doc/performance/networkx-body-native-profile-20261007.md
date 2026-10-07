# NetworkX algorithm-body native profile (2026-10-07)

After removing quadratic set growth, the official shortest-path result is
1.728712 seconds, versus 0.469990 seconds in CPython 3.14.7. This diagnostic
profiles the remaining shared-runtime work without including graph loading.
It is not a timing result or evidence of another speedup.

The unchanged official benchmark body runs 20 times through
`profile_networkx_runtime.py`. After importing the benchmark and loading the
graph, the child creates a fresh ready file. `sample_native_windows.py` waits
for that marker before suspending the busiest thread briefly to collect native
instruction pointers. No Python call-profile hook is enabled, preserving the
normal VM optimization eligibility.

Sampling began **35.706 seconds** after child launch, after graph loading.
The collector retained 4,287 instruction-pointer samples, including 3,223
in `xlang3_runtime.dll`. The profile was symbolized before rebuilding the
candidate. Among runtime DLL samples:

| Function | Samples | Share of runtime DLL samples |
| --- | ---: | ---: |
| mapping_iter_next | 768 | 23.8% |
| dict_find_string_index | 323 | 10.0% |
| runtime_value_contains | 275 | 8.5% |
| Interpreter::run_function | 195 | 6.1% |
| value_is | 194 | 6.0% |
| object_get_attr | 178 | 5.5% |

These are instruction-pointer samples without call stacks. They identify
where execution was observed, not each operation's inclusive cost. Native
sampling perturbs execution, and its printed call durations must not be
substituted for pyperf values. Dictionary iteration is a concrete next
optimization target, to be checked against its ownership/mutation semantics
and measured with the official benchmark before claiming a gain.

The phase gate was checked with a CPython 3.14.7 child that sleeps before
creating its marker and doing CPU work. The ready case collected 13 samples
after the delay. A missing marker produced zero samples and sampler exit 2;
a stale marker was rejected before child launch. This prevents silently
profiling setup or claiming a valid diagnostic with no phase marker.

Evidence:

- [Native samples](data/networkx-set-growing-body-native-samples-20261007.json).
- [Symbolized functions](data/networkx-set-growing-body-native-symbols-20261007.txt).
- [Child output](data/networkx-set-growing-body-native-samples-20261007.log).
- [Ready-marker check](data/native-sampler-marker-present-20261007.json).
- [Missing-marker check](data/native-sampler-marker-missing-20261007.json).
- [Stale-marker rejection](data/native-sampler-marker-stale-20261007.log).

Control executable SHA-256:
`303F2E8BDCA58C4B555D13ADD7BFC41EEF9E0019F4780A0C9729596CE07DF177`;
runtime DLL:
`4AA9D085D74402A8AF62A7F4E4605B45B0840B28D895225778DD74C7F7FD8FD1`.
The executable path stayed `build-repro/main-verify-20261006/Release/xlang3.exe`.
