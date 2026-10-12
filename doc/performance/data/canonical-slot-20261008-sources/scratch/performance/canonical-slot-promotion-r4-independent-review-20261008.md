# R4 independent static review

Reviewed final patch: `canonical-slot-promotion-r4-v2-20261008.patch`.
SHA256: `b64b88f3bc8dec3ef6991e3e5195b6963cd1f6d682a1314235a556f5179a8700`.
Scope: source, scratch diff and provenance reads only. No source/test edits, build, compiler, fixture or benchmark execution.

## Verdict

Ready for a controlled trial. No remaining static correctness or test-construction blocker found. Compilation, correctness execution and performance acceptance remain pending.

The original patch (`cd5580b22832234737320982ee981da4517a7fc4598d17f605d2c647993ea6bc`) had three incorrect first-read expectations. V2 corrects all three with separate strict cleanup/Retry and subsequent warm negative/promotion assertions. Its engine candidate is byte-for-byte identical to the original reviewed engine candidate (`6592b859f2b92b48343bdb587660c5ee6ed4949d3c822105508674a852727e51`).

The helper intentionally returns `Retry` before shape proof when the old owning `cache.value` differs from the new descriptor. These transitions therefore need two reads: first read releases the unsafe old owner and installs `Descriptor` with index 0; the next warm read may mark `ShapeIneligible` or promote.

- Replacing the `alias` descriptor now checks first-read identity/Retry, then warmed negative sentinel.
- Restoring canonical `x` after a foreign descriptor now checks first-read canonical descriptor/Retry, then warmed promotion.
- Replacing a cached real property with the foreign descriptor now checks first-read accessor reset/constants/Retry, then warmed negative sentinel.

The unsafe-owner guard is preserved. Splitting these assertions retains strict coverage of both cleanup and the eventual result.

## Verified design

- Cold descriptor installation clears getter/setter/deleter flags and weak accessor/property/class pointers before publishing fresh owner/version guards and before replacing the old owning value. Same-descriptor warm property specialization remains intact. Owning accessor constants keep their existing cleanup lifetime.
- Only a guarded `Descriptor` read interprets the sentinel. Other cache kinds retain their index meaning. Cold installation resets index to 0 for `Retry`, so different classes/descriptors and class-version changes do not inherit negative eligibility.
- Missing/deleted/uninitialized/short instance storage, instance native hooks, unsafe old cache owners and invalid MRO remain retryable. Inheritance, aliases and ambiguous flattened layouts stay generic.
- Frame teardown still excludes owning `Descriptor` entries from persistent scalar caches; descriptor and sentinel reset together. The new frame construction uses a real `LoadAttr` metadata site and observes the descriptor refcount before ownership insertion.
- The real property/function ownership probe drops external function/property owners before the retry transition. Its weak accessor pointer can therefore become invalid when cache ownership is released, making the cleanup assertions material.
- Promoted result ownership and the existing output-finalizer/sole-receiver tests remain unchanged. No benchmark fixture, workload, expected-output gate or native-hook guard is weakened.

Performance acceptance remains unproven. The marker addresses repeated generic descriptor eligibility work; the ordinary attribute diagnostic warms `InstanceAttr` and bypasses this helper, so keep its unchanged no-regression gate.

Source hashes matched provenance: engine `158656a116108577893968bc61c7f89dbad50a8795dbb8f153251da7cf601d5e`; C++ header `93f47849e9b26eaceb942e36ed9c8d8a333a22036d5b3f00e715d802d9932d2d`. Author's recorded `git apply --check` exit is 0; this review did not run it again.
