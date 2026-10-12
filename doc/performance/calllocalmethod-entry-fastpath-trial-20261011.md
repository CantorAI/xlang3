# `CallLocalMethod` entry fast path for slot getters — 2026-10-11

## Result

Rejected. `CallLocalMethod` is a thin wrapper around `CallMethod`: it copies
the receiver local to a register and then enters the general dispatcher. A
warm `InlineSelfSlotMethod` site was given a guarded early return in
`call_local_method` to skip both operations for an instance with no dynamic
attribute storage, no same-name slot, and no native/custom attribute hooks.
Class/code versions, Python monitoring hooks, and the no-argument shape stayed
guarded. A temporary fixture verified that the same call site still honors an
instance override assigned after warm-up and resumes the class method after
deletion; XLang3 and CPython 3.14.7 both printed `7, 99, 7`.

Ten alternating process pairs ran the 100,000-call method diagnostic on
source-matched Release builds. The control median was **33.545 ms** and the
candidate median was **33.813 ms**; the median paired candidate/control ratio
was **1.0143×**. The early exit did not recover enough work to offset its
guards, so the source change and temporary fixture were removed. This closes
the `CallLocalMethod`-entry variant of the shadow-scan experiment; it is not
evidence that the broad VM method-call path is efficient.

## Evidence

- [Ten-pair raw diagnostic log](data/calllocalmethod-entry-fastpath-ab-20261011.log)
- Control runtime DLL SHA-256: `B2B24CEEBB5E2A6AE85136E14B4E9734E359323E2E257B244F474B5954F21415`
- Candidate runtime DLL SHA-256: `AC60A54178352C1A026861B32E2FB9E476600E8C85CD4718AA6B6A6BC1587F74`
- The full CTest suite passed after restoring the fixed Release source. No
  performance gate or official pyperformance win is claimed for this rejected
  candidate.
