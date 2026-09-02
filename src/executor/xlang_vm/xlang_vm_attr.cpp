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

namespace xlang3 {

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
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::InstanceSlot &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        cache.index < instance_slot_count(instance)) {
      const auto& slot_value = instance_slot_at(instance, cache.index);
      if (slot_value.tag != ValueTag::Invalid) {
        value_assign_fast(out, slot_value);
        return true;
      }
      cache.kind = AttrSiteKind::Empty;
    }
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::InstanceAttr &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        cache.index < instance->attrs.size() &&
        instance->attrs[cache.index].first == name) {
      value_assign_fast(out, instance->attrs[cache.index].second);
      return true;
    }
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::Descriptor &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        object_value_is_descriptor(cache.value)) {
      value_assign_fast(out, cache.value);
      return true;
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
        value_assign_fast(out, descriptor);
        return true;
      }
    }
    if (klass != nullptr) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
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
    for (size_t attr_i = 0; attr_i < instance->attrs.size(); ++attr_i) {
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
    if (value_as_dict(instance->mapping_storage) != nullptr &&
        mapping_get_item(instance->mapping_storage, Value::string(name), out, error)) {
      cache.kind = AttrSiteKind::Empty;
      return true;
    }
  }
  return attribute_get(object, name, out, error);
}

XLANG3_NOINLINE bool xlang_vm_store_attr_cached(
    Value& object,
    const std::string& name,
    const Value& value,
    AttrSiteCache& cache,
    std::string& error) {
  if (name == "__class__") {
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
        cache.index < instance_slot_count(instance)) {
      value_assign_fast(instance_slot_at(instance, cache.index), value);
      if (value_as_dict(instance->mapping_storage) != nullptr) {
        std::string ignored;
        mapping_set_item(instance->mapping_storage, Value::string(name), value, ignored);
      }
      return true;
    }
    if (klass != nullptr &&
        cache.kind == AttrSiteKind::InstanceAttr &&
        cache.owner == &klass->header &&
        cache.version == klass->version &&
        cache.index < instance->attrs.size() &&
        instance->attrs[cache.index].first == name) {
      value_assign_fast(instance->attrs[cache.index].second, value);
      if (value_as_dict(instance->mapping_storage) != nullptr) {
        std::string ignored;
        mapping_set_item(instance->mapping_storage, Value::string(name), value, ignored);
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
      if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
        cache.index = slot_it->second;
        cache.kind = AttrSiteKind::InstanceSlot;
        cache.owner = &klass->header;
        cache.version = klass->version;
        value_assign_fast(instance_slot_at(instance, slot_it->second), value);
        if (value_as_dict(instance->mapping_storage) != nullptr) {
          std::string ignored;
          mapping_set_item(instance->mapping_storage, Value::string(name), value, ignored);
        }
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
        if (value_as_dict(instance->mapping_storage) != nullptr) {
          std::string ignored;
          mapping_set_item(instance->mapping_storage, Value::string(name), value, ignored);
        }
        return true;
      }
    }
    if (klass != nullptr && klass->restrict_instance_attrs && !klass->allow_instance_dict) {
      error = "object has no attribute '" + name + "'";
      return false;
    }
    instance->attrs.push_back(std::make_pair(name, value));
    cache.index = static_cast<uint32_t>(instance->attrs.size() - 1);
    cache.kind = AttrSiteKind::InstanceAttr;
    if (klass != nullptr) {
      cache.owner = &klass->header;
      cache.version = klass->version;
    }
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      std::string ignored;
      mapping_set_item(instance->mapping_storage, Value::string(name), value, ignored);
    }
    return true;
  }
  return attribute_set(object, name, value, error);
}

} // namespace xlang3
