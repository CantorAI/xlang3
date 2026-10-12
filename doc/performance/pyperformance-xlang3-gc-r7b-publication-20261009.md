# Full pyperformance comparison with CPython 3.14.7

The retained capture completes 77 of 97 definitions; 20 fail and none remain
unfinished. Its complete list covers all 124 expected subtests, with 86 scored
and 38 unscored. All 30 invalid attempts remain excluded from scores.

[Full comparison and horizontal chart](pyperformance-xlang3-gc-r7b-per-definition-ownership-supplement-20261009.md)
includes every definition, every expected subtest, and every recorded attempt.
The ratio is CPython elapsed time divided by XLang3 elapsed time: above 1× favors
XLang3; below 1× favors CPython. For example, 0.05× means XLang3 took 20× as long.

The captured engine is `21e2eadaba7d317932374a63d3c2d70dff4e25c1`.
The publication review ran at `784888a05aa14a22b04942c59e9b96199475bcb5`, which
adds test-harness repairs. Those later changes did not rerun or relabel these
retained timings. Subsequent VM trials require their own measurement evidence.

CPython is exactly 3.14.7. Its October 7 reference and XLang3's independent
per-definition windows are unpaired and use different capture protocols.
The completed-subset geometric mean is about 0.127×; this excludes failures
and is neither a full-suite aggregate nor proof of a causal engine improvement.
The overall goal of materially outperforming CPython remains unfinished.

Published evidence retains both origins, successful and failed raw outputs,
invalid attempts, watcher observations, active controller/parser sources, and
the numerical audit. Runtime binaries and installed dependency/data trees are
represented by recorded hashes, rather than vendored into this report. This is
an evidence closure for the report, not a clean-checkout runtime reproduction.
