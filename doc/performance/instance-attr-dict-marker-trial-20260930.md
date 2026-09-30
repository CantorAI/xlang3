# Instance attribute dict-marker guard trial (2026-09-30)

## Change tested

The CPython comparison suggested checking whether XLang3's warmed instance
attribute load could avoid resolving `instance_attribute_storage()` just to
decide whether the separate `#__dict__` dictionary is present. The trial used
the synchronized `InstanceObject::has_separate_attribute_storage` bit in the
two cached-load guards in `xlang_vm_ops_attr.h` and in the lower-level cached
attribute helper. The optimization comment described the expected hot-path
benefit and the marker-writer invariant.

This was a generic VM experiment. The CPython `deltablue` comparison and
adaptive-load notes are in the [companion implementation analysis](deltablue-cpython314-implementation-analysis-20260930.md).

## Official result: neutral; change reverted

Both runtimes used the official pyperformance 1.14.0 `deltablue` benchmark in
rigorous mode, with independent bytecode-cache prefixes and alternating run
order:

| Order | Parent | Candidate |
|---|---:|---:|
| Parent, then candidate | 59.1 ± 1.2 ms | 59.6 ± 0.9 ms |
| Candidate, then parent | 59.2 ± 1.0 ms | 58.8 ± 2.2 ms |
| Pooled | 59.2 ± 1.1 ms | 59.2 ± 1.7 ms |

The second candidate run warned that its samples might be unstable. Pooled
`pyperf compare_to` reports **1.00× slower and not significant**. The
fixed-baseline 11-case Release regression gate passed, but that is a safety
check; it does not establish a speedup. The code change was removed.

```text
DeltaBlue elapsed time (shorter is faster)
Parent       59.2 ms |████████████████████
Candidate    59.2 ms |████████████████████
```

The timing result suggests this accessor simplification is not a material
DeltaBlue bottleneck. One plausible reason is that `load_attr()` already has
an instruction-local class/version/index guard ahead of general descriptor
resolution; the trial only shortened one condition in that existing path.
Treat that explanation as an inference, not a separate timing measurement.

## Validation and evidence

The candidate Release build passed the complete Python fixture runner and
52/52 CTest tests outside the known `xlang3_cli_fixtures` wrapper failure. It
also passed every case in the fixed-baseline gate; the full paired data and
binary hashes are in the [gate report](data/instance-attr-dict-marker-candidate-fixed-baseline-20260930.json).
The official benchmark raw samples are [parent, first order](data/instance-attr-marker-parent-first-rigorous-20260930.json),
[candidate, first order](data/instance-attr-marker-candidate-first-rigorous-20260930.json),
[candidate, reverse order](data/instance-attr-marker-candidate-second-rigorous-20260930.json),
[parent, reverse order](data/instance-attr-marker-parent-second-rigorous-20260930.json),
[pooled parent](data/instance-attr-marker-parent-merged-rigorous-20260930.json),
and [pooled candidate](data/instance-attr-marker-candidate-merged-rigorous-20260930.json).

The parent executable and runtime DLL hashes were
`87677A90EF64095E11855208D9DECF108151BE6C0371F4A1DB92E7E7459F6EA7` and
`7C3E23C35F27289428D40754D4CF403106740741C0D02167EFAC8CB678583F7C`. The
tested executable hash was the same; its candidate runtime DLL hash was
`45B178BD82C3002251351AB593059420564AF83AC0C2684877C9853F57DEF5EF`.
The parent executable and runtime DLL were preserved in a local Release
snapshot before changing the source.

The remaining slowdown requires a broader win in shared Python execution:
attribute specialization, call/return handling, and VM dispatch. The stable
offset and shape guards in CPython remain a useful design reference, but this
single marker-bit shortcut should not be repeated as a DeltaBlue optimization.
