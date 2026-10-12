# Pickle bound-instance writer-call cache trial (2026-10-02)

## Result

Rejected. Caching `CallMethod` calls whose instance attribute is already a
`BoundMethod` regressed the focused Pickler-shaped benchmark. In 21
order-balanced pairs, candidate/baseline was **1.0211x** (95% interval
**1.0070–1.0546**): median time increased from **47.906 ms** to **49.387 ms**.
The Release candidate also passed the existing
`callmethod_instance_shadow_cache` fixture, but correctness alone does not
justify retaining a slower path.

## Hypothesis and implementation tested

The pure-Python `pickle._Pickler` stores `self.write = framer.write`; repeated
`self.write(...)` calls therefore resolve an instance-held bound method. The
trial added a guarded call-site cache for that case, validating the receiver,
class/version, instance attribute slot/name, and bound method operands before
dispatch. It left `pickle.py` in Python and changed only VM dispatch.

The experiment indicates that adding this guard/cache work to the general
`CallMethod` path costs more than it saves for this workload. Do not repeat this
same generic positive-shadow cache without a narrower profile-backed design.

## Reproduction

The synthetic workload is [`pickle_pure_python_writer.py`](../../benchmarks/cases/pickle_pure_python_writer.py).
It performs 60 dumps of a representative nested payload through fresh
`pickle._Pickler` instances writing to `BytesIO`. Raw paired measurements are
in [`pickle-instance-bound-method-call-ab-20261002.json`](data/pickle-instance-bound-method-call-ab-20261002.json).

The candidate was built in `build-repro/Release` with the configured Visual
Studio 18 environment. The baseline executable was
`scratch/performance-trials/pgo-training/control/xlang3.exe`. The benchmark
used 3 warmups and 21 alternating AB/BA pairs. This focused result is not an
official pyperformance suite result.
