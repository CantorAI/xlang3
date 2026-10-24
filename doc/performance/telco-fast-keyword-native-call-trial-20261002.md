# `telco`: stack-backed native keyword callback trial (2026-10-02)

The official workload calls `print(value, file=outfil)` 5,000 times per loop.
I added an opt-in VM callback that passes native keyword arguments by borrowed
view and leaves positional arguments in VM registers, then enabled it only for
the built-in `print`. The redirected-print semantics fixture passed, and
runtime counters confirmed the fixture used the fast callback. Calls with
`*args` or `**kwargs` continued through the generic materialization path.

The official `pyperformance` 1.14.0 `--fast` samples were **284 ms +/- 15 ms**
for the saved control and **290 ms +/- 23 ms** for the candidate.
`pyperf compare_to` hid the difference as statistically insignificant. The
trial therefore does not demonstrate a gain and its callback/API changes were
removed. The existing guarded direct `StringIO.write` path remains. The clean
Release rebuild after rollback passed the complete 11-case fixed-baseline gate
with 41 paired samples per case; see the [gate report](data/fast-keyword-trial-rollback-fixed-baseline-r41-20261002.json).

Raw samples:

- [control](data/telco-fast-keyword-control-fast-20261002.json)
- [candidate](data/telco-fast-keyword-candidate-fast-20261002.json)
