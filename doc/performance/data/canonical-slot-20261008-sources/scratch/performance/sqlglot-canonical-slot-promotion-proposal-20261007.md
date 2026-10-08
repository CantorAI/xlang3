# SQLGlot canonical slot promotion proposal — read-only review, 2026-10-07

This is a proposal, not an applied or measured optimization. No source,
benchmark library, build, or runtime was changed during this review.

Use the mechanically checked revision 2 patch:
`sqlglot-canonical-slot-promotion-proposal-r2-20261007.patch`.
The first draft and bare-hunk schematic remain preserved. Revision 2 also
clears scalar property accessor flags and weak pointers before publishing the
new slot guards; it leaves owning accessor constants to ordinary cleanup.
Both `git apply --check` calls returned 0 without applying anything, and exact
source/candidate copies plus SHA provenance are retained alongside the patches.
The nine-group focused fixture draft and expected output are in
`sqlglot-canonical-slot-promotion-fixture-draft-20261007.py/.out`; they have
not been registered, parsed with a runtime, or executed.

The latest full comparison records sqlglot_v2_parse at 21.95 ms versus CPython
3.14.7 at 988.3 us (about 22.2 times slower). Historical Python profiling found
the same 7,556 Python call events in both runtimes; profiling-hook timings are
not performance measurements. The active Release sample has 381/500 runtime
DLL samples, 78 ntdll, 26 ucrtbase and 15 other library samples. It has no
function attribution. The earlier optimized RelWithDebInfo symbol review is
supporting evidence only: interpreter dispatch, load_attr, Value release/copy,
string map lookup and FNV hashing all appeared repeatedly.

Ranked costs, with evidence and limits:

1. Slot/attribute descriptor execution and its ownership traffic. The historical
   ten-parse counter process recorded 124,359 LoadAttr, 216,292 LoadLocalAttr,
   100,800 StoreAttr and 915,618 SlotDescriptor increfs AND decrefs. These counts
   include import/setup, so they do not prove an exact fraction of parse time.
   Current source still routes canonical initialized slots through Descriptor
   cache values and the descriptor dispatch path rather than InstanceSlot.
   SQLGlot Token, Tokenizer and Parser explicitly declare slots: tokens.py
   lines 232 and 545; parser.py line 381. Tokenizer._advance and Parser._match
   read several such fields on every hot Python call.
2. Ordinary Python execution and call boundaries: 7,556 identical Python calls,
   including _advance 1,328, _match 1,046, Enum.__hash__ 670, _match_set 438,
   list_get 420 and _get_token 416. Current indexed names, argument transfers,
   trivial callbacks and captured-item shortcuts address particular shapes;
   they do not remove these general parser/tokenizer bodies. Frame-layout,
   generator storage and timer trials should not be repeated without new proof.
3. Wrapper/lifetime churn: 72,238 BoundMethod allocations and 72,052 releases
   in the old process (free-list lifecycle counts, not system heap allocations),
   plus substantial Instance/Module/Function traffic. Existing cached native
   calls cover 52,542/54,908 native calls in that process; raw generic CallMethod
   lookup counts are only 1,960 versus 19,135 cached Python-method hits. The
   rejected method-slot/shadow-cache and direct Enum hash-call trials make
   another narrow wrapper cache an unattractive first choice.

The concrete candidate is to promote a resolved, canonical, initialized member
descriptor into the existing class/version/index InstanceSlot read cache. This
would eliminate repeated owning SlotDescriptor values and repeated slot-name
linear scans (slot_descriptor_get currently asks effective_slot_index twice),
not merely rearrange LoadLocalAttr guards or cache one method-name map lookup.
No cache record or frame storage is added. The attached scratch patch is only
a proposed implementation of the read side; stores/deletes remain unchanged.

Promotion proof:

- A resolved descriptor must be exact SlotDescriptor with descriptor.name equal
  to the requested attribute name. Aliases deliberately retain generic dispatch.
- The receiver must be an ordinary Instance with no native attribute callback;
  its current class must have no custom __getattribute__ hook.
- Resolve the current effective class slot index by name, check bounds and name,
  and require an initialized slot value. Missing/deleted slots retain the
  original descriptor, __getattr__, materialized-dict and tuple-backed fallbacks.
- The descriptor owner must occur in the receiver MRO and still declare that
  same descriptor under that name. Require exactly one owning slot declaration
  of that name throughout the MRO. Duplicate/shadowed slots are not promoted:
  the current layout construction deduplicates slot names, so its CPython
  distinct-storage behavior is a separate existing correctness risk.
- Do not release an arbitrary old owning cache value during proof installation:
  its finalizer could mutate the class. Promote only an empty/nonobject old
  cache value or the same proven descriptor; otherwise keep existing dispatch.
- Own the hit result before overwriting the output, publish cache guards before
  assignment, and do not access the borrowed receiver/class/slot afterward.
- Process-wide class version tags prevent allocator-address reuse mistakes;
  class/base descriptor replacement invalidates subclasses before old values
  are released (object_model.cpp around 556 and 5367). __class__ replacement
  also changes the identity guard. No persistent Value or descriptor root is
  added to the cache.

Correctness plan, before any timing:

1. Exact slots and an inherited unique slot with a shifted effective index;
   assert ordinary repeated reads, live writes and output-object identity.
2. Base.x saved descriptor aliased to y: y must stay generic and read x;
   wrong-owner descriptor must retain existing error behavior. Record any
   pre-existing disagreement with CPython instead of treating it as new gain.
3. Base/Child both declare x: compare Base.x.__get__ and Child.x.__get__ with
   CPython. Keep this shape unpromoted; a current failure blocks claiming full
   slot semantic coverage and is a separate runtime-layout bug to fix.
4. Delete the initialized slot; check AttributeError and inherited __getattr__
   lazy refill, plus a slotted class with __dict__ whose dict has the same name.
5. Warm the same read site, replace its descriptor with a property/custom data
   descriptor, restore it, change a base descriptor and introduce/replace
   __getattribute__. Verify all substitutions remain visible.
6. Last output finalizer changes the descriptor or releases the receiver; the
   already owned result must survive, and the next read must use new semantics.
7. C++ eligibility assertions: warm cache becomes InstanceSlot only for proven
   initialized exact/inherited-unique slots; aliases, duplicate-owner names,
   deleted slots and unsafe old cache values remain Descriptor/fallback.
8. Existing slots_model, inherited_getattr_slots, private_slots_mangling,
   load_attr_cache_precedence, custom-getattribute, property, graph/serialization
   and full fixture/C++ suites must pass.

Bounded measurement plan after accepted Release preservation and idle guard:

- One control/candidate diagnostic of the unchanged official SQLGlot parse body
  (same existing ten-parse counter driver) should show SlotDescriptor ownership
  traffic falls; counters are mechanism evidence only, not speed measurements.
- Use one small slots-read fixture-shaped process comparison covering exact,
  inherited unique and fallback rows; retain every neutral/slower control.
- Run the unchanged default 11-case/21-pair/5-warmup/10% fixed baseline gate.
- Run original official sqlglot_v2_parse with identical mode/source/dependencies
  on preserved control and candidate. If fast results are noisy, qualify them
  and use a bounded matched rigorous pair only when the candidate is directional.
- Retain only if mechanism and affected official result support a gain and the
  fixed gate passes. Do not broaden guard caches or repeat rejected reorderings
  to manufacture a positive result.

Evidence: doc/performance/sqlglot-parse-runtime-call-diagnosis-20261006.md;
doc/performance/sqlglot-parse-profile-20261005.md;
doc/performance/data/sqlglot-parse-current-dispatch-counters-20261006.log;
doc/performance/data/sqlglot-parse-native-samples-active-release-20261006.json;
doc/performance/sqlglot-set-hash-experiment-20261006.md;
doc/performance/load-local-attr-direct-read-trial-20261004.md;
doc/performance/callmethod-slot-lookup-cache-20261005.md.
