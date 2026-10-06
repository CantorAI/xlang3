# Smaller inline VM frames on recursive coroutines (2026-10-06)

This workload-specific follow-up tests whether smaller per-frame inline buffers
improve the recursive `coroutines` benchmark. The earlier small-frame screen
covered `deepcopy` and unpickle, but not coroutine frames. The candidate reduced
the inline local/cell capacity from 64 to 8 Values and register capacity from
128 to 32 Values, shrinking the fixed VMFrame storage by about 3.3 KiB.

## Result

The source change was rejected. One fast pyperf pair measured:

| Runtime | Mean |
|---|---:|
| Fixed Release control | 135 ± 10 ms |
| Smaller-frame candidate | 134 ± 2 ms |

`pyperf compare_to` hid the difference as statistically insignificant. This
does not support a coroutine speedup, so the original 64/64/128 capacities
were restored. The fixed Release executable and runtime DLL were restored and
verified by SHA-256; no runtime change is retained.

## Reproduction and build identity

The comparison used the pyperformance 1.14.0 `coroutines` benchmark through
the Windows compatibility shim, with CPython **3.14.7** as the dependency
runtime. Both runs used `--mode fast` and the same host. Raw results:

- [Control pyperf JSON](data/vm-frame-small-inline-coroutines-control-fast-20261006.json)
- [Candidate pyperf JSON](data/vm-frame-small-inline-coroutines-candidate-fast-20261006.json)

Candidate executable SHA-256:
`D7CA3AE539B4BB8BE4A8F2F12581FB98CB5EACF014DE1E1A7E6054F5ADF97411`

Candidate runtime DLL SHA-256:
`227143291B6A79A4E02C8319927E6A04D680681E9F4A14E2380675FA428E4F58`

The capacities can still affect memory footprint, but this test provides no
evidence that reducing them speeds recursive coroutine execution. Revisit only
with a new mechanism or profile showing frame-cache locality is a material
cost.
