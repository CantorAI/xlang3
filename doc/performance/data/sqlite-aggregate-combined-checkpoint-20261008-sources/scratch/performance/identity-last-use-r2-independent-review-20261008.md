# Identity last-use R2 independent static review

Reviewed frozen patch `70cff5cc3dda8d2ed92fe0b5dfe28b868e9e7432071f831eb5de2bd4a7cd58e7` and provenance `82fc09bf044be8b5da015d97806d883a2fdb66efde69136f3a59c38e3e416279`.

Verdict: ready for controlled application, compiler checks and candidate validation. No remaining concrete compiler, ownership or integration blocker found in the bounded review. This is static readiness, not runtime acceptance. No engine/test changes, build, fixture or benchmark execution were performed by this reviewer.

- The appended `IsNoneJumpIfFalse` opcode preserves old opcode IDs and every legacy nonzero `IsJumpIfFalse.c` meaning. Codec version 64 rejects version 63 cached IR, whose missing source provenance cannot implement the new policy. Dump, opcode rows, operand reads and incoming-edge metadata recognize the appended opcode.
- Only an exact source RHS None literal marks identity-result provenance. Reversed operands and a dynamically loaded None remain generic. Source-proven direct/unary/multiline fusion retains comparison position and leaves the existing local-constant comparison fast path intact.
- Retirement requires owning Object storage, exact last use and the per-register control-flow proof. Borrowed values, live aliases, local/cell/native argument roots and output aliases are protected. Incoming edges that bypass a recreated producer block retirement; unsupported backward regions conservatively disable it.
- Both eligible operand owners leave published registers before any destructor. The new bool output is already published. Destruction preserves replaced output first, then right operand, then left; refreshed monitoring is observed after callbacks. C++ native audit storage outlives frame cleanup, including failed-test paths.
- The literal-None taken branch snapshots RIGHT eligibility at dispatch entry. Other identity branches and fallthrough use refreshed state. This matches the recorded CPython 3.14.7 strict observations; an entry-off finalizer cannot retroactively enable the current specialized taken branch. Runtime None value alone is insufficient provenance.

Parent reported the frozen `fcad09adfe9cdf69d43539860cca1d39f5deb8224e16c71462d07ee3bbd3f40d` Python fixture passed all seven groups on exact CPython 3.14.7, including 48 strict monitoring rows and the value-return case. The earlier proposal and failed monitoring baseline remain historical evidence; they are not replaced or accepted by this review.

Candidate C++ tests, XLang fixtures, the unchanged full correctness and fixed regression gates, and affected official benchmarks remain required before accepting this change. Performance improvement is not asserted by this static review.
