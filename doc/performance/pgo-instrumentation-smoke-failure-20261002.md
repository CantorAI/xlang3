# MSVC PGO instrumentation smoke failure (2026-10-02)

The Release PGO instrumentation configuration built successfully, but its
executable did not reach Python execution. A one-line `xlang3.exe -c
"print('instrumented-smoke-ok')"` invocation produced no output and remained
running beyond 10 seconds. The same executable path with `PGO_STAGE=OFF`
completed the equivalent smoke test immediately. The stalled process was
stopped; no PGO training results were collected and no speedup is claimed.

I reconfigured the existing `build-repro` directory with
`XLANG3_RELEASE_PGO_STAGE=OFF` and rebuilt the Release runtime. The restored
executable printed `release-smoke-ok`. All 11 cases then passed the fixed
Release regression gate against `baseline-0336992`, with 21 order-balanced
pairs per case. This is a build-health/regression result, not a comparison
against CPython and not evidence that the PGO stage is usable.

The restored Release pair is identified by these SHA-256 hashes:

- `xlang3.exe`: `C3D48136EE8E94A89B0DBEF1EC2C433164E86D366C78EA0FA3F07AF9ABE8D938`
- `xlang3_runtime.dll`: `12866A793670746F127346497C2F02D8FD60966F21902EE4A1F263F83CC85278`

The complete gate report preserves per-case paired samples, confidence
intervals, source hashes, and runtime identities:
[`pgo-instrumentation-restore-release-gate-20261002.json`](data/pgo-instrumentation-restore-release-gate-20261002.json).

A separate seven-pair, order-alternated direct run of the unchanged
`pickle_pure_python` body compared the restored runtime DLL with the DLL saved
before the PGO attempt. Each sample ran ten outer benchmark loops under the
same Python 3.13 library. Median candidate/control was 1.0055×, too small to
establish any performance change; this diagnostic is not official pyperf and
is not a CPython comparison. The raw values are in
[`pgo-restore-direct-pickle-ab-20261002.csv`](data/pgo-restore-direct-pickle-ab-20261002.csv).

The cause of the instrumented startup stall remains undiagnosed. Before
attempting another profile, isolate the minimal instrumented runtime startup
and verify its generated link command and profile-runtime dependencies. Do not
train on a partially started or unresponsive executable.
