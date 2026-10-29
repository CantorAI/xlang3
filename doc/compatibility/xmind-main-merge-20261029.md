# xMind runtime changes integrated with current main

All nine xMind-required commits through `6df6520f` are integrated: native C++
LRPC hosting, Pydantic package metadata, Unicode library paths, native package
host guards, runtime lifetime isolation, ctypes pointer/buffer behavior and
module class/callable compatibility. Earlier SQLite/AES-GCM changes were
already ancestors of main. After qualifying that merge against `f2356065`,
main advanced to `b1b6673f`; its IPC cancellation and serialized-frame lifetime
fixes were merged and the combined Release runtime was qualified again.

The final Windows checks pass 16 selected CTest groups, including the CLI core
and compatibility fixtures, native hosting/isolation, capacity, serialized
lifetimes, LRPC smoke/backpressure and timeout recovery. The module-class probe,
four ctypes regressions, offline pip 25.2/26.2.1 installs and native Pydantic
metadata lookup also pass without executing CPython. All 11 default performance
cases pass with 21 order-balanced samples, 5 warmups and the unchanged 10%
tolerance against the preserved fixed Release baseline. Source, pure-library,
baseline and runtime inventories recorded for these checks remain unchanged.

The first broad fixture run exposed launcher-environment mismatches. The
PowerShell launcher now uses the native Windows executable path spelling for
the existing `sys.orig_argv` assertion and selects stdlib distutils to avoid
an ambient setuptools `.pth` bootstrap hook. Assertions were not changed.
The initial failure and diagnostic outputs remain in the local validation
ledger; both the corrected first phase and the final integrated phase have
published receipts. This is focused Windows runtime qualification, not a full
cross-platform or xMind application release claim. Running xMind installations
were not replaced; rebuild xlang3 from main on the destination node.

- [Final qualification](../performance/data/xmind-main-merge-20261029/qualification.json)
- [Final correctness output](../performance/data/xmind-main-merge-20261029/correctness.log)
- [Final performance report](../performance/data/xmind-main-merge-20261029/performance.json)
- [First-phase qualification](../performance/data/xmind-main-merge-phase1-20261029/qualification.json)
