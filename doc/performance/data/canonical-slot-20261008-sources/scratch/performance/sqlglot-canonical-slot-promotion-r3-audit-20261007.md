# Canonical slot proposal R3 audit — unvalidated draft

Use `sqlglot-canonical-slot-promotion-proposal-r3-20261007.patch`, not R1/R2.
R3 is a real two-file unified diff and `git apply --check` returned 0. It was
not applied, compiled or run. Exact current source copies, candidate copies,
check log and SHA inventory are retained. Both source hashes before/after are
identical. R1/R2 and their provenance remain untouched.

Independent review correctly identified two R2 blockers:

1. `InstanceObject::native_get_attr` belongs to an instance. The native hook
   setter in object_model.cpp around 6186 attaches/removes it without changing
   a class/version tag. A promotion installation guard alone does not protect
   a warmed cache used on a newly hooked instance or a different same-class
   instance. R3 checks the pointer in BOTH warm InstanceSlot read branches in
   ops_attr.h (original lines 421 and 542). Checking only the early branch
   leaves the later branch able to bypass the hook.
2. The duplicate-declaration proof based on MRO descriptor values is incomplete.
   Value::class_object copies inherited flattened names, collect_slot_names
   deduplicates new names, and descriptors are created only for indices beyond
   inherited_slot_count (object_model.cpp around 2043/2061/2110). Dynamic
   `type('Dup', (Base,), {'__slots__': ('x',)})` can therefore retain only Base.x
   despite declaring a duplicate slot. R3 requires descriptor.owner_class to
   equal the receiver class and rejects the name if ANY ancestor flattened
   layout already contains it. Unique inherited slots stay generic too. R3
   does not claim duplicate-slot CPython compatibility or inherited eligibility.

Read-path audit against current source:

- LoadAttr reaches the two guarded ops_attr InstanceSlot reads.
- LoadLocalAttr delegates to LoadAttr (ops_attr.h around 1030), so both checks
  cover fused local receiver reads.
- LoadModuleAttr delegates to LoadAttr (ops_fused.h around 47), so a global
  receiver load does not introduce a separate cache-hit path.
- Outlined xlang_vm_load_attr_cached already checks native_get_attr and clears
  the cache kind before attribute_get (attr.cpp around 58), before every cached
  read. R3 preserves that entry ordering.
- getattr with two or three arguments, runtime_getattr, and default/missing
  handling call builtin_getattr. That primitive checks the live native hook
  before descriptor/storage resolution (functional_builtins.cpp around 3428).
  It has no InstanceSlot site-cache path to modify. Keep these calls in tests
  to prove a warm VM cache does not change their native semantics.
- InstanceSlot in xlang_frame.h around 545 is cache lifetime retention, not a
  read; no hook decision occurs there.
- LoadInstanceSlot/LoadLocalInstanceSlot and property/arithmetic slot executors
  are distinct pre-existing IR/inline paths. This proposal neither rewrites IR
  into those opcodes nor uses their caches. Any existing native-hook behavior
  there needs an independent audit; R3 does not claim global hook conformance.
- InstanceAttr/InstanceDict warm native-hook guards are outside this trial's
  newly promoted InstanceSlot records. The historical branches are not made
  safer by this patch; do not generalize its scope to all attribute caches.

Concrete C++ coverage draft:
`canonical-slot-native-hook-cases-draft-20261007.h` is scratch only. It proposes
two complementary checks rather than a test mirroring the guard expression:

- Call outlined cache loading on an exact initialized slotted instance, require
  real nonowning InstanceSlot installation, attach a functioning native hook
  through instance_set_native_attr_hooks while proving class version unchanged,
  require the hook result/call count and Empty cache kind, then detach and prove
  original storage value and slot eligibility return.
- Run ordinary Python source through the real Interpreter using registered
  native hook attachment/detachment callbacks. A free function loops on item.x
  to exercise dynamic LoadLocalAttr. Attach a hook AFTER warming the same
  receiver; mix hooked and ordinary instances with the SAME class through the
  same read instruction; detach and read again. Assert hook values differ from
  actual slot values, callbacks actually ran, class versions stayed unchanged,
  the lowered source includes LoadLocalAttr, and all assertions reach the final
  output. Include global receiver reads and getattr with/without defaults.

The C++ draft has not been compiled and may need integration repairs; root must
review its API usage and register it only after accepted Release preservation.
The existing Python scratch fixture's inherited and alias rows remain useful
fallback checks. Diagnostic timings must retain exact-owner and inherited
rows separately so the conservative guard's missed benefit is quantified.

Acceptance still requires full correctness, unchanged fixed default gate, and
the official unchanged SQLGlot parse comparison. A passing fixture or mechanical
patch check supplies no speed claim and does not justify retaining R3 alone.
