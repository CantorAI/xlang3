# Held seven-pair C5 versus R6 original pickle diagnostic

Frozen manager SHA256: `b41f74b4efac72cf40e46ec9bccc6cabc2b31d203e9b764341e22a1109d26a0c`.

Root runs this once after independent static review and an AST check, with no other build or runtime activity:

```powershell
& C:/Python/Python314/python.exe -I scratch/performance/measure-pickle-published-frame-c6-original-body-paired-20261008.py --source-inventory doc/performance/data/published-frame-snapshot-r6-applied-source-20261008.json --source-inventory-sha256 e1a655b96ec2931b607ddb4b037784ffcb069f814267ca0191871839d39f5221 --focused-receipt doc/performance/data/published-frame-snapshot-r6-focused-20261008.json --focused-receipt-sha256 b9eb06e0120d5474a0ef69b65a23da026807c81b3ef803487baf02c3a0eba253 --prefix pickle-published-frame-c6-original-body-paired-20261008
```

The initial CP/R6 body receipt `0040315f…`, C5 body `2d6a79f2…`, C5 native sample `e5919bc0…`, C5 terminal `a3be06b1…`, and complete preserved control `85910ee3…` are pinned. C5's 178 Release files and 106-source snapshot are checked separately from current R6's 178 files and 107 sources, before and after measurement. All twelve focused phases remain required.

Exactly seven pairs run in this order: control/candidate, candidate/control, control/candidate, candidate/control, control/candidate, candidate/control, control/candidate. Each fresh child has a unique pycache prefix and runs unchanged child `5295c899…` once: 41 loops, three original objects, 20 dumps per object, protocol 5. All fourteen invocations must reproduce the original pure-Python byte signatures and roundtrips. CPython 3.14.7 is the manager only; no CP body is rerun. Each child retains separate stdout/stderr, both original and enclosing timers, and a 300-second limit. Idle checks and the existing one-second external compiler/CTest watcher invalidate observed overlap. Failures and partials are preserved without retries or replacement samples.

The statistic is the median of seven within-pair **C5 original timer / R6 original timer** ratios. The 95% percentile bootstrap resamples those seven paired ratios 50,000 times with replacement, using seed 20261008, the median statistic, and linear interpolation at the 2.5% and 97.5% endpoints. No values are trimmed or removed.

The predeclared useful-signal rule requires **median > 1.02 and lower CI > 1.0**. Otherwise the receipt says `reject_c6_retain_verified_c5`; the script performs no automatic rollback. A qualifying signal only permits consideration of further validation. It is an unscored original-body diagnostic, not an official result, CPython win, or whole-suite claim.

The existing full validator `04009fa2…` remains held because its prerequisite uses the initial single-body comparison, which showed no gain. A qualifying paired result would need a separately frozen paired-proof prerequisite revision; do not rerun or alter the initial body receipt to make that gate pass.

Provenance SHA256: `8ba07cd0e4d56720dfd8cb0ae15a3ff3b351ba15309a19d63804b046ea35e4fc`. No controller, AST check, runtime, benchmark, build, source edit, or staging operation was executed by this subagent.
