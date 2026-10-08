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
#pragma once

#include "xlang3/compiler.h"
#include "xlang3/value.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace xlang3 {

using NativeInstanceGetAttr = bool (*)(const Value& self, const std::string& name, Value& out, std::string& error);
using NativeInstanceSetAttr = bool (*)(Value& self, const std::string& name, const Value& value, std::string& error);
using NativeInstanceDeleteAttr = bool (*)(Value& self, const std::string& name, std::string& error);

struct CallArgsView;
struct ClassObject;
using NativeTypeConstructorCallback = bool (*)(
    Runtime& runtime, const ClassObject& klass, const CallArgsView& args,
    bool& handled, Value& out, std::string& error);



// Class cache versions are process-wide tags, like CPython's type version
// tags. A tag identifies both this class lifetime and its current attributes,
// so non-owning VM caches can survive calls without being fooled by allocator
// address reuse after a class is collected.
uint64_t next_class_version_tag() noexcept;

struct ClassObject {
  Object header;
  std::string name;
  Value base;
  Value metaclass;
  Value globals_module;
  std::vector<Value> bases;
  std::unordered_map<std::string, Value> attrs;
  std::vector<std::string> definition_attr_order;
  std::vector<std::string> instance_slot_names;
  std::unordered_map<std::string, uint32_t> instance_slot_indices;
  // Keep actual own declaration occurrences before inherited layout dedup.
  // Inherited LOAD_ATTR can prove one canonical declaration only from this
  // immutable history; mutable __slots__/attrs and flattened names cannot.
  std::vector<std::string> own_instance_slot_declarations;
  bool own_instance_slot_declarations_known = false;
  uint64_t version = next_class_version_tag();
  // Cached builtin-container traits for Value::instance. The version tag lets
  // instance creation avoid repeated MRO queries while still tracking changes
  // to this class or one of its bases.
  uint64_t instance_container_traits_version = 0;
  uint8_t instance_container_traits = 0;
  // Cache the common negative __del__ lookup at the class level. The version
  // guard makes later class or base mutations invalidate the result.
  // Zero is uncached; otherwise low bit is presence and upper bits are the
  // class version used for the cached answer.
  std::atomic_uint64_t release_finalizer_cache{0};
  bool has_explicit_bases = false;
  bool has_descriptors = false;
  bool has_getattribute_hook = false;
  bool has_getattr_hook = false;
  bool has_setattr_hook = false;
  bool has_delattr_hook = false;
  bool restrict_instance_attrs = false;
  bool allow_instance_dict = true;
  bool allow_weakref = true;
  // Native heap types may provide a full constructor for their exact,
  // unmodified class. The version guard leaves subclasses and monkey-patched
  // __new__/__init__ methods on generic Python-compatible construction.
  NativeTypeConstructorCallback native_type_constructor = nullptr;
  uint64_t native_type_constructor_version = 0;
  std::vector<ClassObject*> subclasses;
  std::vector<Value> mro_cache;
  uint64_t mro_cache_version = 0;
};

using NativeGCReferenceVisitor = void (*)(Object*, void*);
using NativeGCTraverse = void (*)(void*, NativeGCReferenceVisitor, void*);

struct InstanceObject {
  Object header;
  Value klass;
  Value mapping_storage;
  Value sequence_storage;
  uint32_t slot_count = 0;
  // Dict subclasses keep Python attribute storage apart from mapping entries
  // in an internal #__dict__ attr. Ordinary instances skip scanning attrs for
  // that marker on hot attribute and method lookup paths. Every writer that
  // can add the reserved name must keep this bit in sync; graph restore derives
  // it from the serialized attribute names.
  uint32_t has_separate_attribute_storage : 1;
  std::string native_type;
  void* native_data = nullptr;
  void* (*native_data_cast)(void*, const char*) = nullptr;
  void* native_owner = nullptr;
  void (*native_data_cleanup)(void*) = nullptr;
  std::vector<Object*> native_gc_references;
  NativeGCTraverse native_gc_traverse = nullptr;
  void (*native_data_clear)(void*) = nullptr;
  bool (*native_data_truthy)(const void*) = nullptr;
  NativeInstanceGetAttr native_get_attr = nullptr;
  NativeInstanceSetAttr native_set_attr = nullptr;
  NativeInstanceDeleteAttr native_delete_attr = nullptr;
  bool finalizer_started = false;
  // Lets ordinary instance recycling skip probing the native-edge registry.
  bool native_gc_registered = false;
  Value inline_slots[8];
  std::vector<Value> overflow_slots;
  std::vector<std::pair<std::string, Value>> attrs;
};

inline bool instance_has_native_gc_references(const InstanceObject& instance) {
  return instance.native_gc_traverse != nullptr ||
      !instance.native_gc_references.empty();
}

template <typename Visitor>
inline void instance_visit_native_gc_references(
    const InstanceObject& instance, Visitor&& visitor) {
  if (instance.native_gc_traverse != nullptr) {
    using VisitorType = std::remove_reference_t<Visitor>;
    const auto visit = [](Object* target, void* context) {
      if (target != nullptr)
        (*static_cast<VisitorType*>(context))(target);
    };
    instance.native_gc_traverse(
        instance.native_owner, visit, const_cast<void*>(
            static_cast<const void*>(std::addressof(visitor))));
    return;
  }
  for (auto* target : instance.native_gc_references) {
    if (target != nullptr) visitor(target);
  }
}

Value& instance_attribute_storage(InstanceObject& instance);
bool runtime_value_compare(Runtime& runtime, const std::string& op, const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool runtime_value_contains(
    Runtime& runtime,
    const Value& container,
    const Value& item,
    bool& out,
    std::string& error);

struct BoundMethodObject {
  Object header;
  Value self;
  Value function;
};

struct StaticMethodObject {
  Object header;
  Value function;
  Value attrs_dict;
};

struct ClassMethodObject {
  Object header;
  Value function;
  Value attrs_dict;
};

struct SuperObject {
  Object header;
  Value klass;
  Value self;
};

struct SlotDescriptorObject {
  Object header;
  Value owner_class;
  std::string owner_name;
  std::string name;
  uint32_t index = 0;
};

XLANG3_HOT_INLINE ClassObject* value_as_class(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Class) {
    return nullptr;
  }
  return reinterpret_cast<ClassObject*>(value.as.obj);
}

XLANG3_HOT_INLINE InstanceObject* value_as_instance(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Instance) {
    return nullptr;
  }
  return reinterpret_cast<InstanceObject*>(value.as.obj);
}

XLANG3_HOT_INLINE BoundMethodObject* value_as_bound_method(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::BoundMethod) {
    return nullptr;
  }
  return reinterpret_cast<BoundMethodObject*>(value.as.obj);
}

XLANG3_HOT_INLINE StaticMethodObject* value_as_static_method(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::StaticMethod) {
    return nullptr;
  }
  return reinterpret_cast<StaticMethodObject*>(value.as.obj);
}

XLANG3_HOT_INLINE ClassMethodObject* value_as_class_method(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::ClassMethod) {
    return nullptr;
  }
  return reinterpret_cast<ClassMethodObject*>(value.as.obj);
}

XLANG3_HOT_INLINE SuperObject* value_as_super(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Super) {
    return nullptr;
  }
  return reinterpret_cast<SuperObject*>(value.as.obj);
}

XLANG3_HOT_INLINE SlotDescriptorObject* value_as_slot_descriptor(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::SlotDescriptor) {
    return nullptr;
  }
  return reinterpret_cast<SlotDescriptorObject*>(value.as.obj);
}

XLANG3_HOT_INLINE uint32_t instance_slot_count(const InstanceObject* instance) {
  return instance->slot_count;
}

XLANG3_HOT_INLINE Value& instance_slot_at(InstanceObject* instance, uint32_t index) {
  return instance->slot_count <= 8 ? instance->inline_slots[index] : instance->overflow_slots[index];
}

XLANG3_HOT_INLINE const Value& instance_slot_at(const InstanceObject* instance, uint32_t index) {
  return instance->slot_count <= 8 ? instance->inline_slots[index] : instance->overflow_slots[index];
}

void object_model_release_object(Object* object);
bool object_model_class_is_live(const ClassObject* klass);

Value slot_descriptor(std::string owner_name, std::string name, uint32_t index);
void slot_descriptor_set_owner_class(Value& descriptor, const Value& owner_class);
std::string object_model_to_string(const Value& value);

bool object_get_attr(const Value& object, const std::string& name, Value& out, std::string& error);
bool object_get_super_method_for_call(
    const Value& object, const std::string& name, Value& method, Value& receiver);
void function_capture_builtins(Runtime& runtime, FunctionObject& function, const Value& globals);
bool object_set_attr(Value& object, const std::string& name, const Value& value, std::string& error);
bool object_delete_attr(Value& object, const std::string& name, std::string& error);
bool object_get_class_attr_for_instance(const Value& object, const std::string& name, Value& out, std::string& error);
bool class_get_bound_attr(
    Runtime& runtime,
    const Value& owner_class,
    const Value& instance,
    const std::string& name,
    Value& out,
    std::string& error);
bool object_get_special_method(
    Runtime& runtime,
    const Value& object,
    const std::string& name,
    Value& out,
    std::string& error);
bool object_get_function_annotations(Runtime& runtime, const Value& object, Value& out, std::string& error);
bool object_get_class_annotations(Runtime& runtime, const Value& object, Value& out, std::string& error);
bool object_lookup_class_attr(const Value& klass, const std::string& name, Value& out, std::string& error);
bool object_lookup_class_attr_before_base(
    const Value& klass, const std::string& name, std::string_view stop_base,
    Value& out, std::string& error);
bool object_lookup_inherited_class_attr(const Value& klass, const std::string& name, Value& out, std::string& error);
bool object_value_has_descriptor_get(const Value& value);
bool object_value_has_descriptor_set(const Value& value);
bool object_value_has_descriptor_delete(const Value& value);
bool object_value_is_descriptor(const Value& value);
bool object_value_is_data_descriptor(const Value& value);
bool object_construct(Value klass, const Value* args, uint32_t argc, Value& out, std::string& error);
bool class_set_base(Value klass, Value base, std::string& error);
// Only Python construction keeps captured declaration history through base
// assembly. SDK/native mutation retains the original entry and becomes unknown.
bool class_set_base_for_construction(Value klass, Value base, std::string& error);
// Cold layout writers invalidate descendant guards before losing their proof.
void class_forget_slot_declarations(ClassObject* klass);
bool class_get_subclasses(const Value& klass, Value& out, std::string& error);
bool class_is_subclass(const ClassObject* klass, const ClassObject* base);
// Borrow the version-validated MRO without allocating a tuple/pointer vector.
// The view is invalidated by class/base mutation; do not keep it across Python
// callbacks or other operations that can change the class hierarchy.
bool class_get_mro_values(ClassObject* klass, const std::vector<Value>*& out, std::string& error);
bool class_has_builtin_base_name(ClassObject* klass, std::string_view name);
bool class_try_enum_value_lookup(const Value& klass, const Value& value, Value& out);
bool instance_set_native_data(
    Value instance,
    std::string native_type,
    void* native_data,
    void (*native_data_cleanup)(void*),
    std::string& error);
bool instance_set_native_owner(Value instance, std::string native_type, void* native_data,
    void* owner, void (*cleanup)(void*), std::string& error);
bool instance_set_native_gc_references(Value instance, const Value* references,
    uint32_t reference_count, void (*clear)(void*), std::string& error);
bool instance_set_native_gc_traversal(Value instance,
    NativeGCTraverse traverse, void (*clear)(void*), std::string& error);
const std::unordered_set<Object*>& native_gc_instance_registry();
void* instance_get_native_data(const Value& instance, const std::string& native_type);
bool instance_set_native_truthy(Value instance, bool (*truthy)(const void*), std::string& error);
bool instance_native_truthy(const Value& instance, bool& out);
bool runtime_instance_truthy(Runtime& runtime, const Value& value, bool& out, std::string& error);
inline bool runtime_truthy(Runtime& runtime, const Value& value, bool& out, std::string& error) {
  switch (value.tag) {
    case ValueTag::Invalid:
    case ValueTag::None:
      out = false;
      return true;
    case ValueTag::Bool:
      out = value.as.b;
      return true;
    case ValueTag::Int64:
      out = value.as.i64 != 0;
      return true;
    case ValueTag::Double:
      out = value.as.f64 != 0.0;
      return true;
    case ValueTag::Object:
      break;
  }
  if (value_as_instance(value)) return runtime_instance_truthy(runtime, value, out, error);
  out = value_truthy(value);
  return true;
}
bool instance_set_native_attr_hooks(
    Value instance,
    NativeInstanceGetAttr get_attr,
    NativeInstanceSetAttr set_attr,
    NativeInstanceDeleteAttr delete_attr,
    std::string& error);

} // namespace xlang3
