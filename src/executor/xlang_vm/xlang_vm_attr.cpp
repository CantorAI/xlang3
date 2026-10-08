/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "xlang_vm_attr.h"

#include "xlang3/attribute.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include <algorithm>



namespace xlang3 {

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
  if (owner == nullptr) return CanonicalSlotPromotion::ShapeIneligible;
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
  if (owner == &klass) {
    // Preserve the accepted own-only proof and its native/legacy fallbacks.
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
  } else {
    // Flattened names repeat inherited storage and can hide duplicate own
    // declarations. Prove one immutable own occurrence across the current MRO
    // once, then reuse the existing version/index hit without per-read scans.
    for (const auto& class_value : *mro) {
      auto* candidate = value_as_class(class_value);
      if (candidate == nullptr) return CanonicalSlotPromotion::Retry;
      if (!candidate->own_instance_slot_declarations_known)
        return CanonicalSlotPromotion::ShapeIneligible;
      const auto occurrences = std::count(candidate->own_instance_slot_declarations.begin(),
          candidate->own_instance_slot_declarations.end(), name);
      if (occurrences == 0) continue;
      if (occurrences != 1 || ++declarations != 1)
        return CanonicalSlotPromotion::ShapeIneligible;
      const auto found = candidate->attrs.find(name);
      exact_owner_seen = candidate == owner && found != candidate->attrs.end() &&
          value_as_slot_descriptor(found->second) == slot;
    }
    // VM descriptor dispatch checks raw slot->index for missing/getattr before
    // name remapping. A displaced receiver index must retain that original path.
    if (index != slot->index) return CanonicalSlotPromotion::ShapeIneligible;
    const auto owner_index = owner->instance_slot_indices.find(name);
    if (owner_index == owner->instance_slot_indices.end() ||
        owner_index->second >= owner->instance_slot_names.size() ||
        owner->instance_slot_names[owner_index->second] != name ||
        owner_index->second != slot->index) return CanonicalSlotPromotion::ShapeIneligible;
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

XLANG3_NOINLINE bool xlang_vm_resolve_method_value(
    Runtime& runtime, const Value& object, const std::string& name,
    Value& out, std::string& error) {
  auto* instance = value_as_instance(object);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  Value descriptor;
  std::string ignored;
  if (klass != nullptr && (klass->has_getattribute_hook ||
      (klass->has_descriptors &&
       object_get_class_attr_for_instance(object, name, descriptor, ignored) &&
       object_value_has_descriptor_get(descriptor)))) {
    // CALL_METHOD must resolve a callable property/cached_property like
    // LOAD_ATTR; CALL_FUNCTION. Keep this work on generic cache misses: ordinary
    // function/native method hits retain their direct dispatch. Call the
    // original attribute primitive directly, without exposing an artificial
    // builtins.getattr C-profile event for an implicit attribute operation.
    const Value* getattr = runtime.find_builtin("getattr");
    auto* native = getattr == nullptr ? nullptr : value_as_native_function(*getattr);
    if (native != nullptr && native->callback != nullptr) {
      Value args[] = {object, Value::string(name)};
      return native->callback(runtime, args, 2, out, error, native->user_data);
    }
  }
  return attribute_get(object, name, out, error);
}

XLANG3_NOINLINE bool xlang_vm_load_attr_cached(
    const Value& object,
    const std::string& name,
    AttrSiteCache& cache,
    Value& out,
    std::string& error) {
  if (auto* instance = value_as_instance(object)) {
    if (instance->native_get_attr != nullptr) {
      cache.kind = AttrSiteKind::Empty;
      return attribute_get(object, name, out, error);
    }
    auto* klass = value_as_class(instance->klass);
    const bool has_attribute_dict = value_as_dict(instance_attribute_storage(*instance)) != nullptr;
    const bool compact_attribute_visible = !has_attribute_dict || name.rfind("__xlang3_", 0) == 0;
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::InstanceSlot &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        cache.index < instance_slot_count(instance) &&
        cache.index < klass->instance_slot_names.size() &&
        klass->instance_slot_names[cache.index] == name) {
      const auto& slot_value = instance_slot_at(instance, cache.index);
      if (slot_value.tag != ValueTag::Invalid) {
        value_assign_fast(out, slot_value);
        return true;
      }
      cache.kind = AttrSiteKind::Empty;
    }
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::Descriptor &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        object_value_is_data_descriptor(cache.value) &&
        object_value_has_descriptor_get(cache.value)) {
      if (cache.index != kNoCanonicalSlotPromotion) {
        const auto promotion = try_promote_canonical_slot_read(
            *instance, *klass, name, cache.value, cache, out);
        if (promotion == CanonicalSlotPromotion::Promoted) return true;
        if (promotion == CanonicalSlotPromotion::ShapeIneligible)
          cache.index = kNoCanonicalSlotPromotion;
      }
      value_assign_fast(out, cache.value);
      return true;
    }
    if (klass != nullptr && klass->has_descriptors) {
      Value descriptor;
      std::string descriptor_error;
      if (object_get_class_attr_for_instance(object, name, descriptor, descriptor_error) &&
          object_value_is_data_descriptor(descriptor) &&
          object_value_has_descriptor_get(descriptor)) {
        const auto promotion = try_promote_canonical_slot_read(
            *instance, *klass, name, descriptor, cache, out);
        if (promotion == CanonicalSlotPromotion::Promoted) return true;
        // A setter-only descriptor still handles assignments, but cannot
        // preempt instance storage on reads (NetworkX's cache resetters use
        // this pattern). Cache a read descriptor only when it has a getter.
        // Publishing a changed descriptor under fresh guards must not expose
        // weak accessor pointers from the old property. Disable scalar flags
        // before its owning value can release a function or run a finalizer;
        // owning accessor constants keep their normal cache-cleanup lifetime.
        if (cache.owner != &klass->header || cache.version != klass->version ||
            !value_is(cache.value, descriptor)) {
          cache.getter_inline = false;
          cache.setter_inline = false;
          cache.deleter_inline = false;
          cache.property_attr_name = nullptr;
          cache.class_value = nullptr;
          cache.accessor_function = nullptr;
          cache.accessor_code_version = 0;
        }
        cache.kind = AttrSiteKind::Descriptor;
        cache.owner = &klass->header;
        cache.version = klass->version;
        cache.index = promotion == CanonicalSlotPromotion::ShapeIneligible
            ? kNoCanonicalSlotPromotion : 0;
        value_assign_fast(cache.value, descriptor);
        value_assign_fast(out, descriptor);
        return true;
      }
    }
    if (auto* attributes = value_as_dict(instance_attribute_storage(*instance))) {
      if (cache.kind == AttrSiteKind::InstanceDict &&
          cache.owner == &attributes->header && klass != nullptr &&
          cache.version == klass->version && cache.index < attributes->entries.size()) {
        const auto& entry = attributes->entries[cache.index];
        auto* key = value_as_string(entry.first);
        if (key != nullptr && string_object_view(*key) == name) {
          value_assign_fast(out, entry.second);
          return true;
        }
      }
      size_t attribute_index = 0;
      if (mapping_get_string_item(
              instance_attribute_storage(*instance), name, out, error, &attribute_index)) {
        cache.kind = AttrSiteKind::InstanceDict;
        cache.owner = &attributes->header;
        cache.version = klass == nullptr ? 0 : klass->version;
        cache.index = static_cast<uint32_t>(attribute_index);
        return true;
      }
      error.clear();
    }
    if (compact_attribute_visible && klass != nullptr &&
        cache.kind == AttrSiteKind::InstanceAttr &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        cache.index < instance->attrs.size() &&
        instance->attrs[cache.index].first == name) {
      value_assign_fast(out, instance->attrs[cache.index].second);
      return true;
    }
    if (klass != nullptr) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() &&
          slot_it->second < instance_slot_count(instance) &&
          slot_it->second < klass->instance_slot_names.size() &&
          klass->instance_slot_names[slot_it->second] == name &&
          class_allows_raw_instance_slot_fallback(klass, name)) {
        const auto& slot_value = instance_slot_at(instance, slot_it->second);
        if (slot_value.tag != ValueTag::Invalid) {
          cache.index = slot_it->second;
          cache.kind = AttrSiteKind::InstanceSlot;
          cache.owner = &klass->header;
          cache.version = klass->version;
          value_assign_fast(out, slot_value);
          return true;
        }
      }
    }
    for (size_t attr_i = 0; compact_attribute_visible && attr_i < instance->attrs.size(); ++attr_i) {
      if (instance->attrs[attr_i].first == name) {
        cache.index = static_cast<uint32_t>(attr_i);
        cache.kind = AttrSiteKind::InstanceAttr;
        if (klass != nullptr) {
          cache.owner = &klass->header;
          cache.version = klass->version;
        }
        value_assign_fast(out, instance->attrs[attr_i].second);
        return true;
      }
    }
  }
  cache.kind = AttrSiteKind::Empty;
  return attribute_get(object, name, out, error);
}

XLANG3_NOINLINE bool xlang_vm_store_attr_cached(
    Value& object,
    const std::string& name,
    const Value& value,
    AttrSiteCache& cache,
    std::string& error) {
  if (name == "__class__" || name == "__dict__") {
    cache.kind = AttrSiteKind::Empty;
    return object_set_attr(object, name, value, error);
  }
  if (auto* instance = value_as_instance(object)) {
    if (instance->native_set_attr != nullptr) {
      cache.kind = AttrSiteKind::Empty;
      return object_set_attr(object, name, value, error);
    }
    auto* klass = value_as_class(instance->klass);
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::InstanceSlot &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        cache.index < instance_slot_count(instance) &&
        cache.index < klass->instance_slot_names.size() &&
        klass->instance_slot_names[cache.index] == name) {
      value_assign_fast(instance_slot_at(instance, cache.index), value);
      return true;
    }
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::InstanceAttr &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        cache.index < instance->attrs.size() &&
        instance->attrs[cache.index].first == name) {
      value_assign_fast(instance->attrs[cache.index].second, value);
      if (value_as_dict(instance_attribute_storage(*instance)) != nullptr) {
        std::string ignored;
        mapping_set_item(instance_attribute_storage(*instance), Value::string(name), value, ignored);
      }
      return true;
    }
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::Descriptor &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        object_value_is_descriptor(cache.value)) {
      error = "descriptor assignment requires VM dispatch";
      return false;
    }
    if (klass != nullptr && klass->has_descriptors) {
      Value descriptor;
      std::string descriptor_error;
      if (object_get_class_attr_for_instance(object, name, descriptor, descriptor_error) &&
          object_value_is_data_descriptor(descriptor)) {
        cache.kind = AttrSiteKind::Descriptor;
        cache.owner = &klass->header;
        cache.version = klass->version;
        value_assign_fast(cache.value, descriptor);
        error = "descriptor assignment requires VM dispatch";
        return false;
      }
    }
    if (klass != nullptr) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() &&
          slot_it->second < instance_slot_count(instance) &&
          slot_it->second < klass->instance_slot_names.size() &&
          klass->instance_slot_names[slot_it->second] == name &&
          class_allows_raw_instance_slot_fallback(klass, name)) {
        cache.index = slot_it->second;
        cache.kind = AttrSiteKind::InstanceSlot;
        cache.owner = &klass->header;
        cache.version = klass->version;
        value_assign_fast(instance_slot_at(instance, slot_it->second), value);
        return true;
      }
    }
    for (size_t attr_i = 0; attr_i < instance->attrs.size(); ++attr_i) {
      if (instance->attrs[attr_i].first == name) {
        cache.index = static_cast<uint32_t>(attr_i);
        cache.kind = AttrSiteKind::InstanceAttr;
        if (klass != nullptr) {
          cache.owner = &klass->header;
          cache.version = klass->version;
        }
        value_assign_fast(instance->attrs[attr_i].second, value);
        if (name == "#__dict__") instance->has_separate_attribute_storage = true;
        if (value_as_dict(instance_attribute_storage(*instance)) != nullptr) {
          std::string ignored;
          mapping_set_item(instance_attribute_storage(*instance), Value::string(name), value, ignored);
        }
        return true;
      }
    }
    if (klass != nullptr && klass->restrict_instance_attrs && !klass->allow_instance_dict) {
      error = "object has no attribute '" + name + "'";
      return false;
    }
    instance->attrs.push_back(std::make_pair(name, value));
    if (name == "#__dict__") instance->has_separate_attribute_storage = true;
    cache.index = static_cast<uint32_t>(instance->attrs.size() - 1);
    cache.kind = AttrSiteKind::InstanceAttr;
    if (klass != nullptr) {
      cache.owner = &klass->header;
      cache.version = klass->version;
    }
    if (value_as_dict(instance_attribute_storage(*instance)) != nullptr) {
      std::string ignored;
      mapping_set_item(instance_attribute_storage(*instance), Value::string(name), value, ignored);
    }
    return true;
  }
  return attribute_set(object, name, value, error);
}

} // namespace xlang3
