// Descriptor.index is otherwise unused by read dispatch. Keep class-shape
// rejection in that existing word instead of retrying an impossible promotion
// for every inherited/property/aliased read. Other cache kinds own their normal
// index meaning; version mismatch and every new Descriptor install recompute it.
constexpr uint32_t kNoCanonicalSlotPromotion = UINT32_MAX;
enum class CanonicalSlotPromotion : uint8_t { Promoted, Retry, ShapeIneligible };

// Resolve canonical initialized slot storage once, then reuse the existing
// class/version/index cache. Ambiguous and aliased descriptors stay generic.
static CanonicalSlotPromotion try_promote_canonical_slot_read(
    const InstanceObject& instance, ClassObject& klass,
    const std::string& name, const Value& descriptor_value,
    AttrSiteCache& cache, Value& out) {
  const auto* slot = value_as_slot_descriptor(descriptor_value);
  if (slot == nullptr || slot->name != name || klass.has_getattribute_hook)
    return CanonicalSlotPromotion::ShapeIneligible;
  // Receiver state and release callbacks are not class-shape facts: another
  // instance, a later slot write or safe cache cleanup may allow promotion.
  if (instance.native_get_attr != nullptr ||
      (cache.value.tag == ValueTag::Object &&
       !value_is(cache.value, descriptor_value))) return CanonicalSlotPromotion::Retry;
  auto* owner = value_as_class(slot->owner_class);
  // Flattened slot metadata can hide duplicate subclass declarations.
  // Until own declarations have durable metadata, inherited slots stay generic.
  if (owner != &klass) return CanonicalSlotPromotion::ShapeIneligible;
  const auto index_it = klass.instance_slot_indices.find(name);
  if (index_it == klass.instance_slot_indices.end())
    return CanonicalSlotPromotion::ShapeIneligible;
  const uint32_t index = index_it->second;
  if (index >= klass.instance_slot_names.size() ||
      klass.instance_slot_names[index] != name) return CanonicalSlotPromotion::ShapeIneligible;
  if (index >= instance_slot_count(&instance) ||
      instance_slot_at(&instance, index).tag == ValueTag::Invalid)
    return CanonicalSlotPromotion::Retry;
  const std::vector<Value>* mro = nullptr;
  std::string error;
  if (!class_get_mro_values(&klass, mro, error)) return CanonicalSlotPromotion::Retry;
  uint32_t declarations = 0;
  bool exact_owner_seen = false;
  for (const auto& class_value : *mro) {
    auto* candidate = value_as_class(class_value);
    if (candidate == nullptr) return CanonicalSlotPromotion::Retry;
    if (candidate != &klass &&
        candidate->instance_slot_indices.find(name) !=
            candidate->instance_slot_indices.end()) return CanonicalSlotPromotion::ShapeIneligible;
    const auto found = candidate->attrs.find(name);
    if (found == candidate->attrs.end()) continue;
    const auto* declared = value_as_slot_descriptor(found->second);
    if (declared == nullptr ||
        value_as_class(declared->owner_class) != candidate ||
        declared->name != name) continue;
    if (++declarations != 1) return CanonicalSlotPromotion::ShapeIneligible;
    exact_owner_seen = candidate == owner && declared == slot;
  }
  if (!exact_owner_seen || declarations != 1)
    return CanonicalSlotPromotion::ShapeIneligible;
  Value result = instance_slot_at(&instance, index);
  // Canonical initialized slots should become the existing index cache, not
  // owning Descriptor entries: repeated LOAD_ATTR otherwise copies the member
  // descriptor and resolves its slot name on every read. Class version tags
  // preserve descriptor changes and inheritance; absent/deleted/ambiguous slots
  // keep the original descriptor and __getattr__ path. Own the returned value
  // before old output can run a finalizer, then never touch borrowed storage.
  // Reusing this cache must not preserve a prior property accessor flag:
  // deleting the slot later must never dereference its old weak function.
  cache.getter_inline = false;
  cache.setter_inline = false;
  cache.deleter_inline = false;
  cache.property_attr_name = nullptr;
  cache.class_value = nullptr;
  cache.accessor_function = nullptr;
  cache.accessor_code_version = 0;
  cache.kind = AttrSiteKind::InstanceSlot;
  cache.owner = &klass.header;
  cache.version = klass.version;
  cache.index = index;
  value_set_invalid(cache.value); // only empty/scalar or this owned descriptor
  value_assign_fast(out, result);
  return CanonicalSlotPromotion::Promoted;
}

