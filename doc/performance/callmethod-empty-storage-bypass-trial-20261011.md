# CallMethod empty-storage shadow-scan bypass — 2026-10-11

## Result

Rejected. A warmed `InlineSelfSlotMethod` site can prove there is no instance
method override with a small set of receiver guards when the instance has no
dynamic attribute storage. I tested an early return before the existing
receiver-shadow scan, limited to instances with empty compact attributes,
invalid mapping storage, no native/custom attribute hooks, and no slot named
after the method. The existing class, function-code, observer, and call-arity
guards remained required.

Ten order-alternated process pairs ran the same diagnostic script on
source-matched Release builds. The getter loop measured a **33.180 ms** control
median and **33.977 ms** candidate median per 100,000 calls; the median of
paired candidate/control ratios was **1.0341×**. Nine pairs were slower and
one was effectively tied. Direct-call and attribute-only controls did not
show a corresponding improvement. The shortcut therefore adds branch/guard
cost without reducing enough work; it was removed.

This agrees with the earlier method-shadow-cache trial on Richards, which was
also slower, and with the prior negative-shadow caching trial on subparsers.
The new diagnostic confirms the cost of `obj.get()` itself remains large, but
does not justify skipping the scan with this guard design. Do not retry the
same scan-bypass hypothesis without a new mechanism or profile evidence.

## Evidence and validation

- [Ten-pair raw diagnostic log](data/callmethod-empty-storage-bypass-ab-20261011.log)
- Control Release runtime DLL SHA-256: `B2B24CEEBB5E2A6AE85136E14B4E9734E359323E2E257B244F474B5954F21415`
- Candidate Release runtime DLL SHA-256: `CCFFD38ABD79DE76D6C0DDA130287045DD917D4F965D051FCC80B8F751007776`
- The `xlang3.exe` launcher hash was identical for both builds; the runtime DLL
  distinguishes the executed implementation.
- The candidate code and its temporary shadowing fixture were removed. The
  fixed Release build directory was rebuilt from the restored source, and all
  **55/55 CTest checks passed**. No speedup is claimed, and the full
  pyperformance gate was not run for this rejected candidate.
