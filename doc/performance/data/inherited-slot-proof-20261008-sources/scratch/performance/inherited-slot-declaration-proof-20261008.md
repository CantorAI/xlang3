# Inherited slot caching: durable declaration proof

Read-only proposal against the R4 candidate, 2026-10-08. No engine/test edits,
compilation, runtime tests or benchmark processes were performed for this note.
No measured gain is claimed. The current own-slot R4 change has no new correctness
blocker from this audit.

## Smallest useful metadata

Add these cold fields to `ClassObject` beside its flattened slot layout:

```cpp
std::vector<std::string> own_instance_slot_declarations;
bool own_instance_slot_declarations_known = false;
```

The vector records normalized **own declaration occurrences**, excluding
`__dict__` and `__weakref__`, before inherited names are merged or duplicates
are removed. Preserve duplicate occurrences; optionally sort after collection
so a cold proof can use `equal_range`. An empty known vector means the class
declared no own slots. Unknown means the history was not captured and inherited
promotion must remain generic.

Capture the names while `collect_slot_names_from_value` already processes the
actual construction value and applies name mangling. An optional declaration
output parameter, forwarded through its existing recursive calls, avoids a
second traversal and exposes the name before `add_unique_slot_name` loses it.
Mark known only after successful collection. Unsupported/partially collected
values remain unknown. For a class without explicit `__slots__`, record accepted
`instance_slots` inputs once as native/compiler own layout declarations. With
explicit `__slots__`, those same compiler inputs must not count the declaration
twice. Inputs discarded by the existing storage-mutation guard must not count.

Never reconstruct this vector from the current `__slots__` attribute, its list,
class `attrs`, or flattened `instance_slot_names`. Each can lose the original
declaration: a list can be mutated, `__slots__` can be replaced/deleted, and
inherited duplicate declarations can create no own descriptor at all.

A single whole-class “unique layout” bit saves the vector but loses the own
declarations needed after `__bases__` mutation and unnecessarily rejects valid
names when another name is ambiguous. The vector plus a known bit keeps the
proof local to the requested slot and leaves the warm cache unchanged.

## Cold eligibility extension

Preserve the current exact-owner R4 path. When the descriptor owner differs from
the receiver class, perform this additional cold proof:

1. Keep the existing exact native SlotDescriptor, requested-name match,
   custom `__getattribute__`, live native-hook, unsafe-old-cache, initialized
   receiver storage and class index/name guards.
2. Get the validated current MRO. Every class must have known own-declaration
   metadata. Count occurrences of the requested normalized name across **own**
   vectors, visiting the C3 MRO once; require exactly one occurrence.
3. Require the sole declaring class to equal `descriptor.owner_class`, to occur
   in this MRO, and to own `attrs[name]` with exactly this descriptor identity.
   Cross-check the owner's slot name/index metadata and the receiver's effective
   name/index. Use the receiver's effective index, as generic VM reads do.
4. Reuse R4's nonowning InstanceSlot owner/version/index cache and result
   ownership ordering. Deleted or uninitialized storage remains Retry; the
   stable ineligible/unknown shape uses R4's negative Descriptor marker.

Do not count every ancestor's flattened names: they repeat the same inherited
storage. Do not count only currently visible descriptors: dynamic
`type('Dup', (Base,), {'__slots__': ('x',)})` currently deduplicates the declaration
against Base's layout and can omit `Dup.x`, defeating that test.

The proof permits a normal inherited x through subclasses with empty declarations
or distinct added names, including a subclass which also has an instance dict.
It rejects same-name declarations across classes or within one declaration,
aliases under a different requested name, foreign owners, removed/replaced owner
descriptors, unknown/native rewritten layouts and invalid MROs. Existing generic
behavior remains authoritative for rejected cases; this proposal does not repair
XLang3's broader duplicate-layout differences from CPython.

## Writers and invalidation

| Source/location | Required handling |
|---|---|
| `src/internal/xlang3/object_model.h:57` | Add the vector and known bit; no extra AttrSiteCache fields or per-read allocation. |
| `src/runtime/object_model.cpp:1222,2010` | Capture own normalized occurrences during existing collection/construction, before inherited dedup. |
| `src/runtime/object_model.cpp:5543` (`class_set_base`) | Keep durable own metadata; do not infer it afresh from descriptor attrs. Revisit recursive subclass invalidation for SDK base additions before enabling inherited caches across this mutation path. Its current tail stamps only this class. |
| `src/runtime/object_model.cpp:5303` (`__bases__`) | Preserve historical own declarations; its recursive class-tag invalidation allows the next cold proof to use the new MRO. |
| `src/runtime/object_model.cpp:2949` (`slot_descriptor_set_owner_class`) | Owner-only rebinding currently has no version invalidation. For a future proof depending on owner identity, initialize fresh owners cheaply and invalidate old/new owner descendants for a real rebind, or explicitly keep such layouts unknown. Do not add an owner check to every warm read. |
| `src/builtins/object_type_builtins.cpp:382` | When `apply_optimized_instance_slots` actually extends a metaclass-produced layout, conservatively mark metadata unknown before mutation. An unchanged prefix/layout can retain existing metadata. |
| `src/serialize/value_graph_reader.cpp:250` | Explicitly mark restored classes unknown before overwriting placeholder construction metadata; versions 1–4 contain flattened layout only. Keep wire format unchanged for the first trial. |
| `src/runtime/modules/system/weakref_module.cpp:506` | Its direct replacement of names/indices must mark metadata unknown. |
| `src/executor/xlang_vm/xlang_vm_attr.cpp:33` | Add inherited cold proof; preserve R4 tri-state, stale-property cleanup, native hooks and output-finalizer ownership. |

`Value::class_object` centralizes MakeClass, inline type construction, builtin
`type.__new__` and native class creation. `class_set_base` also handles the
MakeClass/SetClassBase path and the exported X3 SDK callback; silently rejecting
every call would miss ordinary Python single-base inheritance. The stable X3
module ABI needs no new function parameter. These ClassObject fields are internal
runtime layout and require the normal coherent runtime rebuild.

Serialization can intentionally omit the new proof for now: restored inherited
slots remain on today's generic path. A later format change can store declaration
occurrences explicitly with known/unknown state and accept old versions as
unknown. Inferring proof from an old serialized mutable namespace is unsafe.

The SDK/version observations above are pre-existing limitations, not evidence of
an introduced R4 value regression. In particular, owner-only rebinding does not
change current VM slot values: `xlang_vm_effective_slot_index` resolves by the
descriptor name in the receiver layout and ignores `owner_class`.

## CPython 3.14.7 reference and performance rationale

CPython retains normalized own slot names in `PyHeapTypeObject.ht_slots` during
type creation and creates member descriptors with concrete storage offsets.
See [3.14.7 typeobject.c](https://raw.githubusercontent.com/python/cpython/v3.14.7/Objects/typeobject.c)
at `type_new_copy_slots`, `type_new_init` and `type_new_descriptors`.

Its attribute specialization validates member-descriptor applicability to the
receiver, then caches the offset and type version. Its warm LOAD_ATTR_SLOT guards
the version, loads the indexed value and deopts on an empty slot. See
[3.14.7 specialize.c](https://raw.githubusercontent.com/python/cpython/v3.14.7/Python/specialize.c)
OBJECT_SLOT and [3.14.7 bytecodes.c](https://raw.githubusercontent.com/python/cpython/v3.14.7/Python/bytecodes.c)
LOAD_ATTR_SLOT. XLang3's name-flattened storage needs its own conservative proof;
CPython's byte offset cannot simply be reused.

SQLGlot's Token, Tokenizer and Parser declare slots; Expression declares args,
parent and arg_key that its subclasses inherit. The shared dependency source is
under `venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages/sqlglot`.
Eligible inherited reads could avoid owning/copying a descriptor and the generic
effective-index name search, using the already persistent nonowning warm cache.
Metadata allocation and MRO declaration scans belong to construction/promotion,
not each instruction. End-to-end benefit and construction cost remain unmeasured.

## Bounded next validation

Extend the existing CPP eligibility/native-hook tests and Python fixture with
ordinary inherited promotion, empty-own intermediate class, dict-bearing child,
distinct added slots, private-name mangling, an empty-layout diamond, repeated
own names, dynamic-type hidden duplicates, alias/foreign-owner fallback, and
slot delete/restore under unchanged class version. Mutate/delete the original
`__slots__` attribute/list after construction to prove history remains durable.
Add warmed base descriptor/property replacement, SDK base addition and owner
rebind tests only to establish the chosen cold invalidation policy. A graph
round-trip must explicitly remain generic when declaration history is unknown.

Preserve the accepted R4 Release first. Reuse the unchanged own/inherited/ordinary
probe and original SQLGlot body with all seven alternating pairs and raw controls.
If focused correctness and measured inherited behavior improve without slowing
the other rows, run the complete correctness/SDK/graph suite, unchanged fixed
Release gate and original official SQLGlot benchmark. Retain neutral/slower rows
and instability warnings. This proposal is not itself acceptance evidence.
