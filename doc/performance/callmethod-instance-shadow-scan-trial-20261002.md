# Instance method shadow-scan trial (2026-10-02)

I tested caching the negative per-instance method-shadow check for repeated
`CallMethod` sites. The guard used an exact receiver identity, class version,
and instance-attribute mutation tag, and was disabled for slots, custom native
attribute hooks, and materialized `__dict__` storage. A fixture covered warm
calls followed by instance assignment and deletion.

The fixed Release `subparsers` gate passed against `baseline-0336992` at
0.874x candidate/baseline. That gate is not a comparison with the immediate
parent: the prior accepted gate was already 0.864x on this case. The new
official pyperformance `--fast` sample measured 251 ms +/- 9 ms, while the
latest pre-trial full run measured about 247 ms; `pyperf compare_to` hid the
difference as statistically insignificant. The trial therefore did not show
a speedup and its runtime changes were removed.

This result says the repeated shadow scan is not the dominant cost in
`argparse_subparsers`; the remaining roughly 31x gap versus CPython 3.14 is
still open. The trial JSON files preserve the data:

- [fixed Release gate](data/callmethod-instance-attr-guard-subparsers-gate-20261002.json)
- [pyperformance candidate](data/pyperformance-xlang3-subparsers-instance-attr-guard-fast-20261002.json)
- [pre-trial full run](data/pyperformance-xlang3-exact-dispatch-full-fast-bounded-20261002.json)

## Follow-up wide-layout guard experiment

A second version guarded only instances with at least eight dynamic
attributes and assigned an instance version lazily, avoiding a global atomic
for instances that had not entered the call-site cache. Its rigorous candidate
sample measured `subparsers` at **313 ms +/- 55 ms**, versus **270 ms +/- 10
ms** for the preserved rigorous control. The candidate sample was highly
variable and its mean was slower, so it is not evidence of a speedup. I
removed the cache and its mutation bookkeeping; the original method-shadow
scan and the existing semantic fixture remain. Raw samples:

- [rigorous control](data/argparse-instance-attr-cache-control-rigorous-20261002.json)
- [wide-layout candidate](data/argparse-instance-attr-cache-candidate-r2-rigorous-20261002.json)
