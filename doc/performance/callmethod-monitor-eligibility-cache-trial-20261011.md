# Cache `sys.monitoring` eligibility for inlined self-slot methods — 2026-10-11

## Result

Rejected. Warm `CallMethod` cache hits for `InlineSelfSlotMethod` called
`sys_monitoring_function_may_dispatch` each time through the common
`inline_python_function_allowed` guard. I tried reusing the call site's
existing scalar payload for the monitoring-configuration generation and
eligibility bit, while retaining per-call debug, trace, and profile checks.
Class identity/version and function code-version checks stayed independent.

Ten order-alternated process pairs ran the same 100,000-call method diagnostic
on the preserved fixed Release control and candidate builds. The control
median was **33.792 ms**, the candidate median was **34.364 ms**, and the
median paired candidate/control ratio was **1.0137×**. The candidate was
slower in six pairs and faster in four. This did not demonstrate a win; the
change was removed. The monitoring query is not a useful standalone target
for this call shape under the current cache design.

This trial is distinct from the earlier trivial direct-call eligibility cache
which showed a 3–5% narrow-call benefit but no improvement for bound-method or
`async_tree_none` pyperformance. Here the target was an already-inlined
slot-getter method, and it also failed to improve.

## Evidence

- [Ten-pair raw diagnostic log](data/callmethod-monitor-eligibility-cache-ab-20261011.log)
- Control Release runtime DLL SHA-256: `B2B24CEEBB5E2A6AE85136E14B4E9734E359323E2E257B244F474B5954F21415`
- Candidate Release runtime DLL SHA-256: `4D5766C8E6C63B058701E8340CA781A7E6438468B2EBF5BEA5E29143F8442ADD`
- The candidate code is removed. The full local Release gate remains required
  for retained engine changes; this rejected trial has no broad-speed claim.
