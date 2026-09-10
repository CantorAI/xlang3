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
#include "xlang3/object_model.h"
#include "runtime/memory/object_cache_lifetime.h"

#include "xlang3/builtin_methods.h"
#include "xlang3/builtins.h"
#include "xlang3/exceptions.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/ir.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value.h"
#include "xlang3/value_hash.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace xlang3 {

namespace {

std::vector<std::string> code_object_names(const ir::Module& module, const ir::Function& function) {
  std::vector<std::string> names = function.names;
  for (const auto& instruction : function.code) {
    if (instruction.op != ir::Op::LoadModuleSlot && instruction.op != ir::Op::StoreModuleSlot &&
        instruction.op != ir::Op::DeleteModuleSlot) {
      continue;
    }
    if (instruction.a >= module.global_slots.size()) {
      continue;
    }
    const auto& name = module.global_slots[instruction.a];
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      names.push_back(name);
    }
  }
  return names;
}

std::string code_object_compat_bytecode(const ir::Module& module, const ir::Function& function) {
  const auto names = code_object_names(module, function);
  std::string bytes;
  auto emit = [&](uint8_t opcode, size_t argument, size_t cache_entries) {
    if (argument > 255) {
      return;
    }
    bytes.push_back(static_cast<char>(opcode));
    bytes.push_back(static_cast<char>(argument));
    bytes.append(cache_entries * 2, '\0');
  };
  emit(128, 0, 0);  // RESUME in CPython 3.14.
  for (const auto& instruction : function.code) {
    if (instruction.op == ir::Op::LoadGlobal && instruction.a < function.names.size()) {
      emit(92, static_cast<size_t>(instruction.a) << 1u, 4);  // LOAD_GLOBAL.
      continue;
    }
    if ((instruction.op == ir::Op::LoadAttr || instruction.op == ir::Op::CallMethod) &&
        instruction.b < function.names.size()) {
      emit(80, static_cast<size_t>(instruction.b) << 1u, 9);  // LOAD_ATTR.
      continue;
    }
    if (instruction.op == ir::Op::LoadModuleSlot && instruction.a < module.global_slots.size()) {
      const auto& name = module.global_slots[instruction.a];
      const auto found = std::find(names.begin(), names.end(), name);
      if (found != names.end()) {
        emit(92, static_cast<size_t>(std::distance(names.begin(), found)) << 1u, 4);
      }
    }
  }
  emit(35, 0, 0);  // RETURN_VALUE in CPython 3.14.
  return bytes;
}

template <typename T>
T* allocate_object_model(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(kind);
  return obj;
}

struct InstanceFreeList {
  ~InstanceFreeList() {
    memory::object_caches_alive = false;
    for (auto* instance : items) {
      delete instance;
    }
  }

  std::vector<InstanceObject*> items;
};

thread_local InstanceFreeList instance_free_list;

InstanceObject* allocate_instance_object() {
  if (memory::object_caches_alive && !instance_free_list.items.empty()) {
    auto* obj = instance_free_list.items.back();
    instance_free_list.items.pop_back();
    obj->header.kind = ObjectKind::Instance;
    obj->header.refcnt = 1;
    xlang_perf_count_object_alloc(ObjectKind::Instance);
    return obj;
  }
  return allocate_object_model<InstanceObject>(ObjectKind::Instance);
}

bool function_descriptor_get_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "function.__get__ expected 1 or 2 arguments";
    return false;
  }
  if (value_as_function(args[0]) == nullptr) {
    error = "descriptor '__get__' requires a function object";
    return false;
  }
  if (args[1].tag == ValueTag::None) {
    value_assign_fast(out, args[0]);
  } else {
    out = Value::bound_method(args[1], args[0]);
  }
  return true;
}

bool function_annotate_method(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc > 1) {
    error = "function.__annotate__ expected at most 1 argument";
    return false;
  }

  auto* function_value = static_cast<Value*>(user_data);
  auto* function = function_value != nullptr ? value_as_function(*function_value) : nullptr;
  if (function == nullptr || function->annotations.tag == ValueTag::Invalid) {
    out = Value::dict({});
    return true;
  }

  value_assign_fast(out, function->annotations);
  return true;
}

void function_annotate_cleanup(void* user_data) {
  delete static_cast<Value*>(user_data);
}


void recycle_instance_object(InstanceObject* instance) {
  if (instance->native_data_cleanup != nullptr && instance->native_data != nullptr) {
    instance->native_data_cleanup(instance->native_owner);
  }
  instance->klass = Value::invalid();
  instance->mapping_storage = Value::invalid();
  instance->sequence_storage = Value::invalid();
  instance->native_type.clear();
  instance->native_data = nullptr;
  instance->native_data_cast = nullptr;
  instance->native_owner = nullptr;
  instance->native_data_cleanup = nullptr;
  instance->native_data_truthy = nullptr;
  instance->native_get_attr = nullptr;
  instance->native_set_attr = nullptr;
  instance->native_delete_attr = nullptr;
  for (uint32_t i = 0; i < instance->slot_count && i < 8; ++i) {
    value_set_invalid(instance->inline_slots[i]);
  }
  instance->overflow_slots.clear();
  instance->attrs.clear();
  instance->slot_count = 0;
  if (memory::object_caches_alive && instance_free_list.items.size() < 1024) {
    instance_free_list.items.push_back(instance);
    return;
  }
  delete instance;
}

Value class_value(const ClassObject* klass) {
  Value value;
  value.tag = ValueTag::Object;
  value.as.obj = const_cast<Object*>(&klass->header);
  retain(value);
  return value;
}

bool staticmethod_descriptor_get_method(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  auto* method = argc >= 1 ? value_as_static_method(args[0]) : nullptr;
  if (method == nullptr || argc < 2 || argc > 3) {
    error = "staticmethod.__get__ expected an instance and optional owner";
    return false;
  }
  value_assign_fast(out, method->function);
  return true;
}

bool classmethod_descriptor_get_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  auto* method = argc >= 1 ? value_as_class_method(args[0]) : nullptr;
  if (method == nullptr || argc < 2 || argc > 3) {
    error = "classmethod.__get__ expected an instance and optional owner";
    return false;
  }
  Value owner;
  if (argc == 3 && args[2].tag != ValueTag::None) {
    value_assign_fast(owner, args[2]);
  } else if (!runtime_type_of_value(runtime, args[1], owner)) {
    error = "classmethod.__get__ could not resolve owner";
    return false;
  }
  out = Value::bound_method(std::move(owner), method->function);
  return true;
}

void class_register_subclass(ClassObject* base, ClassObject* subclass) {
  if (base == nullptr || subclass == nullptr ||
      std::find(base->subclasses.begin(), base->subclasses.end(), subclass) != base->subclasses.end()) {
    return;
  }
  base->subclasses.push_back(subclass);
}

void class_unregister_subclass(ClassObject* base, ClassObject* subclass) {
  if (base == nullptr || subclass == nullptr) {
    return;
  }
  base->subclasses.erase(
      std::remove(base->subclasses.begin(), base->subclasses.end(), subclass),
      base->subclasses.end());
}

void invalidate_class_lookup_caches(ClassObject* klass, std::unordered_set<ClassObject*>& visited) {
  if (klass == nullptr || !visited.insert(klass).second) {
    return;
  }
  klass->mro_cache.clear();
  klass->mro_cache_version = 0;
  ++klass->version;
  for (auto* subclass : klass->subclasses) {
    invalidate_class_lookup_caches(subclass, visited);
  }
}

void invalidate_class_lookup_caches(ClassObject* klass) {
  std::unordered_set<ClassObject*> visited;
  invalidate_class_lookup_caches(klass, visited);
}

std::string class_display_name(const ClassObject& klass) {
  std::string name = klass.name;
  auto qualname_it = klass.attrs.find("__qualname__");
  if (qualname_it != klass.attrs.end()) {
    if (auto* qualname = value_as_string(qualname_it->second)) {
      name = string_object_to_string(*qualname);
    }
  }

  auto module_it = klass.attrs.find("__module__");
  if (module_it == klass.attrs.end()) {
    return name;
  }
  auto* module = value_as_string(module_it->second);
  if (module == nullptr) {
    return name;
  }
  std::string module_name = string_object_to_string(*module);
  if (module_name.empty() || module_name == "builtins") {
    return name;
  }
  return module_name + "." + name;
}

bool contains_class(const std::vector<const ClassObject*>& classes, const ClassObject* klass) {
  return std::find(classes.begin(), classes.end(), klass) != classes.end();
}

bool build_class_mro_classes(const ClassObject* klass, std::vector<const ClassObject*>& out, std::string& error);

bool class_mro_values(ClassObject* klass, const std::vector<Value>*& out, std::string& error);
bool class_lookup_attr(ClassObject* klass, const std::string& name, Value& out, std::string& error);

bool attr_truthy_marker(ClassObject* klass, const std::string& name) {
  Value marker;
  std::string error;
  return class_lookup_attr(klass, name, marker, error) && value_truthy(marker);
}

bool value_is_enum_auto_sentinel(const Value& value) {
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return false;
  }
  if (auto* klass = value_as_class(instance->klass);
      klass != nullptr && (klass->name == "auto" || class_display_name(*klass) == "enum.auto")) {
    return true;
  }
  for (const auto& attr : instance->attrs) {
    if (attr.first == "__xlang3_enum_auto__" && value_truthy(attr.second)) {
      return true;
    }
  }
  return false;
}

bool enum_value_equal(const Value& left, const Value& right) {
  return value_key_equal(left, right);
}

bool enum_member_candidate(const std::string& name, const Value& value) {
  if (name.empty() || name[0] == '_' || name == "name" || name == "value") {
    return false;
  }
  if (value_as_function(value) != nullptr || value_as_native_function(value) != nullptr ||
      value_as_class(value) != nullptr || value_as_static_method(value) != nullptr ||
      value_as_class_method(value) != nullptr || value_as_property(value) != nullptr) {
    return false;
  }
  return true;
}

bool enum_value_map_lookup(const std::vector<std::pair<Value, Value>>& map, const Value& value, Value& out) {
  for (const auto& entry : map) {
    if (enum_value_equal(entry.first, value)) {
      value_assign_fast(out, entry.second);
      return true;
    }
  }
  return false;
}

bool enum_class_has_base_name(const ClassObject& klass, std::string_view name) {
  if (klass.name == name) {
    return true;
  }
  for (const auto& base : klass.bases) {
    auto* base_class = value_as_class(base);
    if (base_class != nullptr && enum_class_has_base_name(*base_class, name)) {
      return true;
    }
  }
  return false;
}

std::string enum_auto_generated_name_value(const std::string& name) {
  std::string value = name;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool finalize_enum_class(ClassObject& klass) {
  if (klass.attrs.find("__xlang3_enum_finalized__") != klass.attrs.end()) {
    return true;
  }
  if (klass.attrs.find("__xlang3_enum_marker__") != klass.attrs.end()) {
    return true;
  }
  if (!attr_truthy_marker(&klass, "__xlang3_enum_marker__")) {
    return true;
  }
  if (klass.attrs.find("_member_names_") != klass.attrs.end() ||
      klass.attrs.find("_member_map_") != klass.attrs.end() ||
      klass.attrs.find("_value2member_map_") != klass.attrs.end()) {
    return true;
  }

  Value klass_value = class_value(&klass);
  std::vector<std::pair<Value, Value>> members;
  std::vector<std::pair<Value, Value>> value_members;
  std::vector<Value> member_names;
  std::vector<Value> member_values;
  int64_t next_auto_value = 1;

  std::vector<std::pair<std::string, Value>> candidates;
  candidates.reserve(klass.attrs.size());
  for (const auto& name : klass.definition_attr_order) {
    auto attr_it = klass.attrs.find(name);
    if (attr_it != klass.attrs.end() && enum_member_candidate(attr_it->first, attr_it->second)) {
      candidates.push_back(*attr_it);
    }
  }
  if (candidates.empty()) {
    for (const auto& attr : klass.attrs) {
      if (enum_member_candidate(attr.first, attr.second)) {
        candidates.push_back(attr);
      }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
      return left.first < right.first;
    });
  }

  for (auto& candidate : candidates) {
    Value raw_value = candidate.second;
    if (value_is_enum_auto_sentinel(raw_value)) {
      if (enum_class_has_base_name(klass, "str")) {
        raw_value = Value::string(enum_auto_generated_name_value(candidate.first));
      } else {
        raw_value = Value::int64(next_auto_value);
      }
    }
    if (raw_value.tag == ValueTag::Int64 && raw_value.as.i64 >= next_auto_value) {
      next_auto_value = raw_value.as.i64 + 1;
    }

    Value existing;
    if (enum_value_map_lookup(value_members, raw_value, existing)) {
      klass.attrs[candidate.first] = existing;
      members.push_back({Value::string(candidate.first), existing});
      continue;
    }

    Value member = Value::instance(klass_value);
    auto* instance = value_as_instance(member);
    if (instance == nullptr) {
      return false;
    }
    instance->attrs.push_back({"name", Value::string(candidate.first)});
    instance->attrs.push_back({"value", raw_value});
    instance->attrs.push_back({"_name_", Value::string(candidate.first)});
    instance->attrs.push_back({"_value_", raw_value});
    if (enum_class_has_base_name(klass, "str")) {
      instance->attrs.push_back({"__xlang3_string_value__", raw_value});
    } else {
      instance->attrs.push_back({"__xlang3_string_value__", Value::string(klass.name + "." + candidate.first)});
    }
    klass.attrs[candidate.first] = member;
    members.push_back({Value::string(candidate.first), member});
    value_members.push_back({raw_value, member});
    member_names.push_back(Value::string(candidate.first));
    member_values.push_back(member);
  }

  klass.attrs["_member_map_"] = Value::dict(members);
  klass.attrs["__members__"] = klass.attrs["_member_map_"];
  klass.attrs["_value2member_map_"] = Value::dict(value_members);
  klass.attrs["_member_names_"] = Value::list(member_names);
  klass.attrs["_member_list_"] = Value::list(member_values);
  klass.attrs["__xlang3_enum_finalized__"] = Value::boolean(true);
  ++klass.version;
  return true;
}

bool class_mro_classes(ClassObject* klass, std::vector<const ClassObject*>& out, std::string& error) {
  const std::vector<Value>* values = nullptr;
  if (!class_mro_values(klass, values, error)) {
    return false;
  }
  for (const auto& value : *values) {
    auto* item = value_as_class(value);
    if (item == nullptr) {
      error = "invalid class in method resolution order";
      return false;
    }
    out.push_back(item);
  }
  return true;
}

bool build_class_mro_classes(const ClassObject* klass, std::vector<const ClassObject*>& out, std::string& error) {
  std::vector<std::vector<const ClassObject*>> sequences;
  sequences.reserve(klass->bases.size() + 1);

  std::vector<const ClassObject*> direct_bases;
  direct_bases.reserve(klass->bases.size());
  for (const auto& base : klass->bases) {
    auto* base_class = value_as_class(base);
    if (base_class == nullptr) {
      error = "base object is not a class";
      return false;
    }
    std::vector<const ClassObject*> base_mro;
    if (!class_mro_classes(base_class, base_mro, error)) {
      return false;
    }
    sequences.push_back(std::move(base_mro));
    direct_bases.push_back(base_class);
  }
  if (!direct_bases.empty()) {
    sequences.push_back(std::move(direct_bases));
  }

  out.push_back(klass);
  while (!sequences.empty()) {
    const ClassObject* candidate = nullptr;
    for (const auto& sequence : sequences) {
      if (sequence.empty()) {
        continue;
      }
      const ClassObject* head = sequence.front();
      bool in_tail = false;
      for (const auto& other : sequences) {
        for (size_t i = 1; i < other.size(); ++i) {
          if (other[i] == head) {
            in_tail = true;
            break;
          }
        }
        if (in_tail) break;
      }
      if (!in_tail) {
        candidate = head;
        break;
      }
    }
    if (candidate == nullptr) {
      error = "cannot create a consistent method resolution order";
      return false;
    }
    if (!contains_class(out, candidate)) {
      out.push_back(candidate);
    }
    for (auto& sequence : sequences) {
      if (!sequence.empty() && sequence.front() == candidate) {
        sequence.erase(sequence.begin());
      }
    }
    sequences.erase(
        std::remove_if(sequences.begin(), sequences.end(), [](const auto& sequence) { return sequence.empty(); }),
        sequences.end());
  }
  return true;
}

bool class_mro_values(ClassObject* klass, const std::vector<Value>*& out, std::string& error) {
  if (klass->mro_cache_version == klass->version && !klass->mro_cache.empty()) {
    out = &klass->mro_cache;
    return true;
  }

  std::vector<const ClassObject*> classes;
  if (!build_class_mro_classes(klass, classes, error)) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(classes.size());
  for (auto* item : classes) {
    if (item == klass) {
      Value self;
      self.tag = ValueTag::Object;
      self.flags = kXlangValueBorrowedRefFlag;
      self.as.obj = &klass->header;
      values.push_back(std::move(self));
    } else {
      values.push_back(class_value(item));
    }
  }
  klass->mro_cache = std::move(values);
  klass->mro_cache_version = klass->version;
  out = &klass->mro_cache;
  return true;
}

} // namespace

bool choose_compatible_metaclass(Value& current, const Value& candidate, std::string& error) {
  auto* candidate_class = value_as_class(candidate);
  if (candidate_class == nullptr) {
    return true;
  }
  auto* current_class = value_as_class(current);
  if (current_class == nullptr || current_class->name == "type") {
    value_assign_fast(current, candidate);
    return true;
  }
  if (class_is_subclass(candidate_class, current_class)) {
    value_assign_fast(current, candidate);
    return true;
  }
  if (class_is_subclass(current_class, candidate_class)) {
    return true;
  }
  std::vector<const ClassObject*> current_mro;
  std::vector<const ClassObject*> candidate_mro;
  std::string ignored;
  if (class_mro_classes(current_class, current_mro, ignored) &&
      class_mro_classes(candidate_class, candidate_mro, ignored)) {
    for (const auto* current_base : current_mro) {
      if (current_base == nullptr || current_base->name == "type" || current_base->name == "object") {
        continue;
      }
      for (const auto* candidate_base : candidate_mro) {
        if (current_base == candidate_base) {
          current = class_value(current_base);
          return true;
        }
      }
    }
  }
  error = "metaclass conflict: the metaclass of a derived class must be a non-strict subclass of the metaclasses of all its bases";
  return false;
}

namespace {

bool class_has_builtin_base_name_impl(ClassObject* klass, std::string_view name) {
  std::vector<const ClassObject*> mro;
  std::string ignored;
  if (!class_mro_classes(klass, mro, ignored)) {
    return false;
  }
  for (const auto* item : mro) {
    if (item != nullptr && item->name == name) {
      return true;
    }
  }
  return false;
}

bool class_lookup_attr(ClassObject* klass, const std::string& name, Value& out, std::string& error) {
  const std::vector<Value>* mro = nullptr;
  if (!class_mro_values(klass, mro, error)) {
    return false;
  }
  for (const auto& class_value : *mro) {
    auto* candidate = value_as_class(class_value);
    if (candidate == nullptr) {
      error = "invalid class in method resolution order";
      return false;
    }
    auto it = candidate->attrs.find(name);
    if (it != candidate->attrs.end()) {
      if (it->second.tag == ValueTag::Invalid) {
        continue;
      }
      value_assign_fast(out, it->second);
      return true;
    }
  }
  return false;
}

bool bind_metaclass_attr_for_class_access(const Value& class_value, Value attr, Value& out) {
  if (auto* method = value_as_static_method(attr)) {
    value_assign_fast(out, method->function);
    return true;
  }
  if (auto* method = value_as_class_method(attr)) {
    Value function;
    value_assign_fast(function, method->function);
    out = Value::bound_method(class_value, std::move(function));
    return true;
  }
  if (value_as_function(attr) != nullptr ||
      (value_as_native_function(attr) != nullptr && value_as_native_function(attr)->bind_as_descriptor)) {
    out = Value::bound_method(class_value, std::move(attr));
    return true;
  }
  value_assign_fast(out, attr);
  return true;
}

bool descriptor_instance_payload(const Value& value, std::string_view builtin_base_name, Value& out) {
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  if (klass == nullptr || !class_has_builtin_base_name_impl(klass, builtin_base_name)) {
    return false;
  }
  for (const auto& attr : instance->attrs) {
    if (attr.first == "__func__") {
      value_assign_fast(out, attr.second);
      return true;
    }
  }
  return false;
}

bool instance_subclass_field(const Value& value, std::string_view builtin_base_name, std::string_view name, Value& out) {
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  if (klass == nullptr || !class_has_builtin_base_name_impl(klass, builtin_base_name)) {
    return false;
  }
  for (const auto& attr : instance->attrs) {
    if (attr.first == name) {
      value_assign_fast(out, attr.second);
      return true;
    }
  }
  return false;
}

bool class_or_bases_have_descriptors(const ClassObject* klass) {
  if (klass->has_descriptors) {
    return true;
  }
  for (const auto& base : klass->bases) {
    auto* base_class = value_as_class(base);
    if (base_class != nullptr && class_or_bases_have_descriptors(base_class)) {
      return true;
    }
  }
  return false;
}

bool object_model_value_has_abstract_marker(const Value& value) {
  Value marker;
  std::string ignored;
  return object_get_attr(value, "__isabstractmethod__", marker, ignored) && value_truthy(marker);
}

void object_model_add_abstract_name(std::vector<Value>& names, const std::string& name) {
  for (const auto& item : names) {
    auto* string = value_as_string(item);
    if (string != nullptr && string_object_to_string(*string) == name) {
      return;
    }
  }
  names.push_back(Value::string(name));
}

void object_model_collect_abstract_names(const Value& value, std::vector<std::string>& names) {
  auto add_name = [&names](const Value& item) {
    auto* string = value_as_string(item);
    if (string == nullptr) {
      return;
    }
    const auto name = string_object_to_string(*string);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      names.push_back(name);
    }
  };
  if (auto* set = value_as_set(value)) {
    for (const auto& item : set->items) {
      add_name(item);
    }
  } else if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      add_name(item);
    }
  } else if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      add_name(item);
    }
  }
}

bool object_model_inherited_concrete_attr(ClassObject& klass, const std::string& name) {
  for (const auto& base : klass.bases) {
    auto* base_class = value_as_class(base);
    if (base_class == nullptr) {
      continue;
    }
    const std::vector<Value>* mro = nullptr;
    std::string ignored;
    if (!class_mro_values(base_class, mro, ignored)) {
      auto it = base_class->attrs.find(name);
      if (it != base_class->attrs.end()) {
        return !object_model_value_has_abstract_marker(it->second);
      }
      continue;
    }
    for (const auto& item : *mro) {
      auto* candidate = value_as_class(item);
      if (candidate == nullptr) {
        continue;
      }
      auto it = candidate->attrs.find(name);
      if (it != candidate->attrs.end()) {
        return !object_model_value_has_abstract_marker(it->second);
      }
    }
  }
  return false;
}

bool class_or_bases_use_abc_meta(ClassObject& klass) {
  auto* metaclass = value_as_class(klass.metaclass);
  if (metaclass != nullptr &&
      (metaclass->name == "ABCMeta" || class_has_builtin_base_name_impl(metaclass, "ABCMeta"))) {
    return true;
  }
  for (const auto& base : klass.bases) {
    Value ignored_value;
    std::string ignored_error;
    if (object_get_attr(base, "__abstractmethods__", ignored_value, ignored_error)) {
      return true;
    }
  }
  return false;
}

void update_abc_abstract_methods_for_class(ClassObject& klass) {
  if (!class_or_bases_use_abc_meta(klass)) {
    return;
  }
  std::vector<Value> abstracts;
  std::vector<std::string> inherited_names;
  for (const auto& base : klass.bases) {
    Value base_abstracts;
    std::string ignored;
    if (object_get_attr(base, "__abstractmethods__", base_abstracts, ignored)) {
      object_model_collect_abstract_names(base_abstracts, inherited_names);
    }
  }
  for (const auto& name : inherited_names) {
    auto override_it = klass.attrs.find(name);
    if (override_it == klass.attrs.end()) {
      if (!object_model_inherited_concrete_attr(klass, name)) {
        object_model_add_abstract_name(abstracts, name);
      }
    } else if (object_model_value_has_abstract_marker(override_it->second)) {
      object_model_add_abstract_name(abstracts, name);
    }
  }
  for (const auto& attr : klass.attrs) {
    if (object_model_value_has_abstract_marker(attr.second)) {
      object_model_add_abstract_name(abstracts, attr.first);
    }
  }
  klass.attrs["__abstractmethods__"] = Value::frozenset(std::move(abstracts));
}

void add_unique_slot_name(std::vector<std::string>& slots, const std::string& name) {
  if (name == "__weakref__") {
    return;
  }
  if (std::find(slots.begin(), slots.end(), name) == slots.end()) {
    slots.push_back(name);
  }
}

bool collect_slot_names_from_value(
    const Value& value,
    std::vector<std::string>& slots,
    bool& allow_instance_dict,
    bool& allow_weakref) {
  if (auto* string = value_as_string(value)) {
    const auto name = string_object_to_string(*string);
    if (name == "__dict__") {
      allow_instance_dict = true;
    } else if (name == "__weakref__") {
      allow_weakref = true;
    } else {
      add_unique_slot_name(slots, name);
    }
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      if (!collect_slot_names_from_value(item, slots, allow_instance_dict, allow_weakref)) {
        return false;
      }
    }
    return true;
  }
  if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      if (!collect_slot_names_from_value(item, slots, allow_instance_dict, allow_weakref)) {
        return false;
      }
    }
    return true;
  }
  if (auto* set = value_as_set(value)) {
    for (const auto& item : set->items) {
      if (!collect_slot_names_from_value(item, slots, allow_instance_dict, allow_weakref)) {
        return false;
      }
    }
    return true;
  }
  if (auto* dict = value_as_dict(value)) {
    for (const auto& entry : dict->entries) {
      if (!collect_slot_names_from_value(entry.first, slots, allow_instance_dict, allow_weakref)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

void inherit_special_attr_flags(ClassObject& klass, const ClassObject& base) {
  klass.has_getattribute_hook = klass.has_getattribute_hook || base.has_getattribute_hook;
  klass.has_getattr_hook = klass.has_getattr_hook || base.has_getattr_hook;
  klass.has_setattr_hook = klass.has_setattr_hook || base.has_setattr_hook;
  klass.has_delattr_hook = klass.has_delattr_hook || base.has_delattr_hook;
}

void update_special_attr_flags(ClassObject& klass, const std::string& attr_name) {
  if (klass.name == "object") {
    return;
  }
  if (attr_name == "__getattribute__") {
    klass.has_getattribute_hook = true;
  } else if (attr_name == "__getattr__") {
    klass.has_getattr_hook = true;
  } else if (attr_name == "__setattr__") {
    klass.has_setattr_hook = true;
  } else if (attr_name == "__delattr__") {
    klass.has_delattr_hook = true;
  }
}

bool descriptor_lookup_method(const Value& value, const std::string& name) {
  if (value_as_static_method(value) != nullptr || value_as_class_method(value) != nullptr) {
    return name == "__get__";
  }
  if (value_as_property(value) != nullptr) {
    return name == "__get__" || name == "__set__" || name == "__delete__";
  }
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  if (klass == nullptr) {
    return false;
  }
  Value ignored;
  std::string error;
  return class_lookup_attr(klass, name, ignored, error);
}

Value module_globals_snapshot(const Value& module_value) {
  auto* module = value_as_module(module_value);
  if (module == nullptr) {
    return Value::dict({});
  }
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(module->name_to_slot.size() + 1);
  entries.push_back({Value::string("__name__"), Value::string(module->name)});
  for (const auto& item : module->name_to_slot) {
    if (item.first.empty() || item.first[0] == '#') {
      continue;
    }
    if (item.first == "__name__") {
      continue;
    }
    if (item.second >= module->slots.size()) {
      continue;
    }
    entries.push_back({Value::string(item.first), module->slots[item.second]});
  }
  return Value::dict(std::move(entries));
}

int64_t frame_source_line(const FrameObject& frame) {
  if (frame.module == nullptr || frame.function_id >= frame.module->functions.size()) {
    return static_cast<int64_t>(frame.instruction_index);
  }
  const auto& fn = frame.module->functions[frame.function_id];
  if (frame.instruction_index < fn.source_lines.size() && fn.source_lines[frame.instruction_index] != 0) {
    return static_cast<int64_t>(fn.source_lines[frame.instruction_index]);
  }
  return static_cast<int64_t>(frame.instruction_index);
}

std::string slot_descriptor_receiver_type_name(const Value& value) {
  if (value.tag == ValueTag::None) {
    return "NoneType";
  }
  if (value.tag == ValueTag::Bool) {
    return "bool";
  }
  if (value.tag == ValueTag::Int64) {
    return "int";
  }
  if (value.tag == ValueTag::Double) {
    return "float";
  }
  if (value_as_string(value) != nullptr) {
    return "str";
  }
  if (value_as_tuple(value) != nullptr) {
    return "tuple";
  }
  if (value_as_list(value) != nullptr) {
    return "list";
  }
  if (value_as_dict(value) != nullptr) {
    return "dict";
  }
  if (auto* klass = value_as_class(value)) {
    return klass->name;
  }
  if (auto* instance = value_as_instance(value)) {
    if (auto* klass = value_as_class(instance->klass)) {
      return klass->name;
    }
  }
  return value_to_string(value);
}

std::string slot_descriptor_wrong_receiver_error(const SlotDescriptorObject& slot, const Value& receiver) {
  return "descriptor '" + slot.name + "' for '" + slot.owner_name +
         "' objects doesn't apply to a '" + slot_descriptor_receiver_type_name(receiver) + "' object";
}

bool slot_descriptor_applies_to_tuple_backed_object(const SlotDescriptorObject& slot, const Value& receiver) {
  Value tuple_value;
  std::string ignored;
  if (!object_get_attr(receiver, "_tuple", tuple_value, ignored)) {
    return false;
  }
  auto* tuple = value_as_tuple(tuple_value);
  return tuple != nullptr && slot.index < tuple->items.size();
}

bool slot_descriptor_get_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2) {
    error = "__get__ expected at least 1 argument, got " + std::to_string(argc > 0 ? argc - 1 : 0);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 3) {
    error = "__get__ expected at most 2 arguments, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* slot = value_as_slot_descriptor(args[0]);
  if (slot == nullptr) {
    error = "member_descriptor.__get__ expected descriptor self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[1].tag == ValueTag::None) {
    if (argc < 3 || args[2].tag == ValueTag::None) {
      error = "__get__(None, None) is invalid";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_assign_fast(out, args[0]);
    return true;
  }
  if (value_as_class(args[1]) != nullptr &&
      slot->owner_name == "type" &&
      (slot->name == "__dict__" || slot->name == "__mro__" || slot->name == "__annotations__")) {
    if (slot->name == "__annotations__") {
      auto* klass = value_as_class(args[1]);
      auto annotations = klass->attrs.find("__annotations__");
      if (annotations != klass->attrs.end() && value_as_dict(annotations->second) != nullptr) {
        value_assign_fast(out, annotations->second);
        return true;
      }
      auto annotate = klass->attrs.find("__annotate__");
      if (annotate != klass->attrs.end() && annotate->second.tag != ValueTag::None) {
        const Value format = Value::int64(1);
        if (!runtime_call_callable(runtime, annotate->second, &format, 1, out, error)) {
          return false;
        }
        if (value_as_dict(out) == nullptr) {
          error = "__annotate__ returned a non-dict";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        value_assign_fast(klass->attrs["__annotations__"], out);
        ++klass->version;
        return true;
      }
    }
    return object_get_attr(args[1], slot->name, out, error);
  }
  auto* instance = value_as_instance(args[1]);
  if (instance == nullptr || slot->index >= instance_slot_count(instance)) {
    if (instance != nullptr) {
      for (const auto& attr : instance->attrs) {
        if (attr.first == slot->name) {
          value_assign_fast(out, attr.second);
          return true;
        }
      }
    }
    Value tuple_value;
    std::string tuple_error;
    if (object_get_attr(args[1], "_tuple", tuple_value, tuple_error)) {
      if (auto* tuple = value_as_tuple(tuple_value); tuple != nullptr && slot->index < tuple->items.size()) {
        value_assign_fast(out, tuple->items[slot->index]);
        return true;
      }
    }
    error = slot_descriptor_wrong_receiver_error(*slot, args[1]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto& slot_value = instance_slot_at(instance, slot->index);
  if (slot_value.tag == ValueTag::Invalid) {
    Value tuple_value;
    std::string tuple_error;
    if (object_get_attr(args[1], "_tuple", tuple_value, tuple_error)) {
      if (auto* tuple = value_as_tuple(tuple_value); tuple != nullptr && slot->index < tuple->items.size()) {
        value_assign_fast(out, tuple->items[slot->index]);
        return true;
      }
    }
    error = "object has no attribute '" + slot->name + "'";
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  value_assign_fast(out, slot_value);
  return true;
}

bool slot_descriptor_set_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 3) {
    error = "__set__ expected 2 arguments, got " + std::to_string(argc > 0 ? argc - 1 : 0);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* slot = value_as_slot_descriptor(args[0]);
  auto* instance = value_as_instance(args[1]);
  if (slot == nullptr) {
    error = "member_descriptor.__set__ expected descriptor self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (instance == nullptr || slot->index >= instance_slot_count(instance)) {
    if (slot_descriptor_applies_to_tuple_backed_object(*slot, args[1])) {
      error = "readonly attribute";
      runtime.raise_class_error("AttributeError", error);
      return false;
    }
    error = slot_descriptor_wrong_receiver_error(*slot, args[1]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(instance_slot_at(instance, slot->index), args[2]);
  value_set_none(out);
  return true;
}

bool slot_descriptor_delete_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "expected 1 argument, got " + std::to_string(argc > 0 ? argc - 1 : 0);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* slot = value_as_slot_descriptor(args[0]);
  auto* instance = value_as_instance(args[1]);
  if (slot == nullptr) {
    error = "member_descriptor.__delete__ expected descriptor self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (instance == nullptr || slot->index >= instance_slot_count(instance)) {
    if (slot_descriptor_applies_to_tuple_backed_object(*slot, args[1])) {
      error = "readonly attribute";
      runtime.raise_class_error("AttributeError", error);
      return false;
    }
    error = slot_descriptor_wrong_receiver_error(*slot, args[1]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_invalid(instance_slot_at(instance, slot->index));
  value_set_none(out);
  return true;
}

bool slot_descriptor_no_keyword_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)args;
  (void)argc;
  (void)kwargs;
  (void)kwargc;
  (void)out;
  const char* method = static_cast<const char*>(user_data);
  error = std::string("wrapper ") + method + "() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool code_lines_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "code.co_lines expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* code = value_as_code(args[0]);
  if (code == nullptr || code->module == nullptr || code->function_id >= code->module->functions.size()) {
    error = "invalid code object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  const auto& fn = code->module->functions[code->function_id];
  std::vector<Value> ranges;
  ranges.reserve(fn.source_lines.size());
  for (size_t i = 0; i < fn.source_lines.size(); ++i) {
    const int64_t line = fn.source_lines[i] == 0 ? -1 : static_cast<int64_t>(fn.source_lines[i]);
    ranges.push_back(Value::tuple({
        Value::int64(static_cast<int64_t>(i * 2)),
        Value::int64(static_cast<int64_t>((i + 1) * 2)),
        line < 0 ? Value::none() : Value::int64(line),
    }));
  }
  out = Value::sequence_iterator(Value::tuple(std::move(ranges)), 0);
  return true;
}

bool code_positions_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "code.co_positions expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* code = value_as_code(args[0]);
  if (code == nullptr || code->module == nullptr || code->function_id >= code->module->functions.size()) {
    error = "invalid code object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  const auto& fn = code->module->functions[code->function_id];
  std::vector<Value> positions;
  const size_t count = std::max(
      std::max(fn.source_lines.size(), fn.source_positions.size()) + 1,
      code_object_compat_bytecode(*code->module, fn).size() / 2);
  positions.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    ir::SourcePosition position;
    if (i == 0) {
      // co_code begins with RESUME, whose source position is the function's
      // definition line.  XLang IR instructions follow it one entry later.
      position.line = fn.first_line;
      position.end_line = fn.first_line;
      position.column = 1;
      position.end_column = 1;
    } else if (i - 1 < fn.source_positions.size()) {
      position = fn.source_positions[i - 1];
    } else if (i - 1 < fn.source_lines.size()) {
      position.line = fn.source_lines[i - 1];
      position.end_line = fn.source_lines[i - 1];
    }
    Value line = position.line == 0 ? Value::none() : Value::int64(static_cast<int64_t>(position.line));
    Value end_line = position.end_line == 0 ? line : Value::int64(static_cast<int64_t>(position.end_line));
    Value column = runtime.no_debug_ranges() || position.column == 0
        ? Value::none()
        : Value::int64(static_cast<int64_t>(position.column - 1));
    Value end_column = runtime.no_debug_ranges() || position.end_column == 0
        ? Value::none()
        : Value::int64(static_cast<int64_t>(position.end_column - 1));
    positions.push_back(Value::tuple({line, end_line, column, end_column}));
  }
  out = Value::sequence_iterator(Value::tuple(std::move(positions)), 0);
  return true;
}

bool code_varname_from_oparg_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "code._varname_from_oparg expected integer oparg";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* code = value_as_code(args[0]);
  if (code == nullptr || code->module == nullptr || code->function_id >= code->module->functions.size()) {
    error = "invalid code object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  const auto& fn = code->module->functions[code->function_id];
  const auto index = args[1].as.i64;
  if (index < 0 || static_cast<size_t>(index) >= fn.locals.size()) {
    error = "tuple index out of range";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  out = Value::string(fn.locals[static_cast<size_t>(index)]);
  return true;
}

bool code_replace_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "code.replace expected keyword-only arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* code = value_as_code(args[0]);
  if (code == nullptr) {
    error = "code.replace expected code object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::code(code->module, code->function_id, code->mode);
  auto* replaced = value_as_code(out);
  if (replaced != nullptr) {
    replaced->filename_override = code->filename_override;
    replaced->first_line_override = code->first_line_override;
    replaced->flags_override = code->flags_override;
  }
  return true;
}

bool frame_clear_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "frame.clear() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* frame = value_as_frame(args[0]);
  if (frame == nullptr) {
    error = "frame.clear() requires a frame object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (frame->live) {
    error = "cannot clear an executing frame";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  frame->locals = Value::dict({});
  value_set_none(out);
  return true;
}

bool code_replace_method_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (!code_replace_method(runtime, args, argc, out, error, nullptr)) {
    return false;
  }
  auto* replaced = value_as_code(out);
  if (replaced == nullptr) {
    error = "code.replace failed";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    const Value& value = *kwargs[i].value;
    if (name == "co_filename") {
      auto* filename = value_as_string(value);
      if (filename == nullptr) {
        error = "co_filename must be str";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      replaced->filename_override = string_object_to_string(*filename);
    } else if (name == "co_firstlineno") {
      if (value.tag != ValueTag::Int64) {
        error = "co_firstlineno must be int";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      replaced->first_line_override = value.as.i64;
    } else if (name == "co_flags") {
      if (value.tag != ValueTag::Int64) {
        error = "co_flags must be int";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      replaced->flags_override = value.as.i64;
    } else if (
        name == "co_argcount" || name == "co_posonlyargcount" || name == "co_kwonlyargcount" ||
        name == "co_nlocals" || name == "co_stacksize" ||
        name == "co_code" || name == "co_consts" || name == "co_names" ||
        name == "co_varnames" || name == "co_freevars" || name == "co_cellvars" ||
        name == "co_name" || name == "co_qualname" || name == "co_linetable" ||
        name == "co_exceptiontable") {
      continue;
    } else {
      error = "code.replace got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return true;
}

bool callable_metadata_attr(const Value& callable, const std::string& name, Value& out) {
  std::string ignored;
  if (object_get_attr(callable, name, out, ignored)) {
    return true;
  }
  if (name == "__annotations__") {
    out = Value::dict({});
    return true;
  }
  if (name == "__module__") {
    if (auto* native = value_as_native_function(callable)) {
      const size_t separator = native->name.find('.');
      out = Value::string(
          separator == std::string::npos ? "builtins" : native->name.substr(0, separator));
      return true;
    }
    value_set_none(out);
    return true;
  }
  if (name == "__doc__") {
    value_set_none(out);
    return true;
  }
  return false;
}

bool callable_name_attr(const Value& callable, Value& out) {
  if (callable_metadata_attr(callable, "__name__", out)) {
    return true;
  }
  out = Value::string(value_to_string(callable));
  return true;
}

bool callable_qualname_attr(const Value& callable, Value& out) {
  if (callable_metadata_attr(callable, "__qualname__", out)) {
    return true;
  }
  return callable_name_attr(callable, out);
}

} // namespace

bool class_has_builtin_base_name(ClassObject* klass, std::string_view name) {
  return class_has_builtin_base_name_impl(klass, name);
}

bool class_try_enum_value_lookup(const Value& klass, const Value& value, Value& out) {
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr || !attr_truthy_marker(klass_obj, "__xlang3_enum_marker__")) {
    return false;
  }
  auto it = klass_obj->attrs.find("_value2member_map_");
  if (it == klass_obj->attrs.end()) {
    return false;
  }
  auto* dict = value_as_dict(it->second);
  if (dict == nullptr) {
    return false;
  }
  for (const auto& entry : dict->entries) {
    if (enum_value_equal(entry.first, value)) {
      value_assign_fast(out, entry.second);
      return true;
    }
  }
  return false;
}

Value Value::class_object(
    std::string name,
    std::vector<std::pair<std::string, Value>> attrs,
    Value base,
    std::vector<std::string> instance_slots,
    Value metaclass,
    Value globals_module) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<ClassObject>(ObjectKind::Class);
  obj->name = std::move(name);
  obj->base = std::move(base);
  obj->metaclass = std::move(metaclass);
  obj->globals_module = std::move(globals_module);
  if (obj->base.tag != ValueTag::Invalid) {
    obj->bases.push_back(obj->base);
    if (auto* base_class = value_as_class(obj->base)) {
      std::string ignored;
      (void)choose_compatible_metaclass(obj->metaclass, base_class->metaclass, ignored);
      obj->has_descriptors = obj->has_descriptors || class_or_bases_have_descriptors(base_class);
      inherit_special_attr_flags(*obj, *base_class);
      obj->instance_slot_names = base_class->instance_slot_names;
      obj->allow_instance_dict = base_class->allow_instance_dict;
      obj->allow_weakref = base_class->allow_weakref;
    }
  }
  const size_t inherited_slot_count = obj->instance_slot_names.size();
  const bool inherited_instance_dict = obj->allow_instance_dict;
  const bool inherited_weakref = obj->allow_weakref;
  bool has_explicit_slots = false;
  for (auto& attr : attrs) {
    if (attr.first == "__new__" &&
        (value_as_function(attr.second) != nullptr || value_as_native_function(attr.second) != nullptr)) {
      attr.second = Value::static_method(attr.second);
    }
    if (object_value_is_descriptor(attr.second)) {
      obj->has_descriptors = true;
    }
    if (attr.first == "__slots__") {
      has_explicit_slots = true;
      obj->restrict_instance_attrs = true;
      obj->allow_instance_dict = false;
      obj->allow_weakref = false;
      collect_slot_names_from_value(attr.second, obj->instance_slot_names, obj->allow_instance_dict, obj->allow_weakref);
    }
    update_special_attr_flags(*obj, attr.first);
    if (obj->attrs.find(attr.first) == obj->attrs.end()) {
      obj->definition_attr_order.push_back(attr.first);
    }
    obj->attrs[std::move(attr.first)] = std::move(attr.second);
  }
  if (obj->attrs.find("__qualname__") == obj->attrs.end()) {
    obj->attrs.emplace("__qualname__", Value::string(obj->name));
  }
  if (!has_explicit_slots) {
    obj->allow_instance_dict = true;
    obj->allow_weakref = true;
  } else {
    obj->allow_instance_dict = obj->allow_instance_dict || inherited_instance_dict;
    obj->allow_weakref = obj->allow_weakref || inherited_weakref;
  }
  if (obj->name == "object") {
    obj->allow_instance_dict = false;
    obj->allow_weakref = false;
  }
  for (auto& slot : instance_slots) {
    if (!obj->restrict_instance_attrs ||
        std::find(obj->instance_slot_names.begin(), obj->instance_slot_names.end(), slot) !=
            obj->instance_slot_names.end()) {
      add_unique_slot_name(obj->instance_slot_names, slot);
    }
  }
  for (size_t i = 0; i < obj->instance_slot_names.size(); ++i) {
    obj->instance_slot_indices[obj->instance_slot_names[i]] = static_cast<uint32_t>(i);
  }
  for (size_t i = inherited_slot_count; i < obj->instance_slot_names.size(); ++i) {
    const auto& slot_name = obj->instance_slot_names[i];
    if (slot_name == "__dict__" || slot_name == "__weakref__" || obj->attrs.find(slot_name) != obj->attrs.end()) {
      continue;
    }
    obj->attrs.emplace(slot_name, slot_descriptor(obj->name, slot_name, static_cast<uint32_t>(i)));
    obj->has_descriptors = true;
  }
  v.as.obj = &obj->header;
  for (auto& attr : obj->attrs) {
    slot_descriptor_set_owner_class(attr.second, v);
  }
  for (const auto& direct_base : obj->bases) {
    class_register_subclass(value_as_class(direct_base), obj);
  }
  return v;
}

// Dict subclasses keep their Python attribute dictionary separate from entries.
// Keeping it in attrs also preserves it through the existing instance graph codec.
Value& instance_attribute_storage(InstanceObject& instance) {
  for (auto& attr : instance.attrs) {
    if (attr.first == "#__dict__") return attr.second;
  }
  return instance.mapping_storage;
}

bool runtime_value_compare(
    Runtime& runtime,
    const std::string& op,
    const Value& lhs,
    const Value& rhs,
    Value& out,
    std::string& error) {
  const char* left_method_name = nullptr;
  const char* right_method_name = nullptr;
  if (op == "==") left_method_name = right_method_name = "__eq__";
  else if (op == "!=") left_method_name = right_method_name = "__ne__";
  else if (op == "<") { left_method_name = "__lt__"; right_method_name = "__gt__"; }
  else if (op == "<=") { left_method_name = "__le__"; right_method_name = "__ge__"; }
  else if (op == ">") { left_method_name = "__gt__"; right_method_name = "__lt__"; }
  else if (op == ">=") { left_method_name = "__ge__"; right_method_name = "__le__"; }

  auto call_comparison_method = [&](const Value& target, const Value& argument,
                                    const char* method_name, bool& handled) -> bool {
    handled = false;
    auto* instance = value_as_instance(target);
    if (instance == nullptr || method_name == nullptr) return true;
    auto* klass = value_as_class(instance->klass);
    bool invert_result = false;
    if (op == "!=" && klass != nullptr &&
        klass->attrs.find("__ne__") == klass->attrs.end() &&
        klass->attrs.find("__eq__") != klass->attrs.end()) {
      method_name = "__eq__";
      invert_result = true;
    }
    const bool inherited_int_comparison =
        klass != nullptr && class_has_builtin_base_name(klass, "int") &&
        klass->attrs.find(method_name) == klass->attrs.end();
    const bool inherited_container_comparison =
        klass != nullptr &&
        (class_has_builtin_base_name(klass, "list") ||
         class_has_builtin_base_name(klass, "dict")) &&
        klass->attrs.find(method_name) == klass->attrs.end();
    if (inherited_int_comparison || inherited_container_comparison) return true;
    Value method;
    std::string ignored;
    if (!object_get_special_method(runtime, target, method_name, method, ignored)) return true;
    if (!runtime_call_callable(runtime, method, &argument, 1, out, error)) return false;
    const Value* not_implemented = runtime.find_builtin("NotImplemented");
    handled = not_implemented == nullptr || !value_is(out, *not_implemented);
    if (handled && invert_result) {
      bool truth = false;
      if (!runtime_truthy(runtime, out, truth, error)) return false;
      value_set_bool(out, !truth);
    }
    return true;
  };

  bool handled = false;
  if (!call_comparison_method(lhs, rhs, left_method_name, handled) || handled) return handled;
  if (!call_comparison_method(rhs, lhs, right_method_name, handled) || handled) return handled;

  if (auto* left_set = value_as_set(lhs)) {
    if (auto* right_set = value_as_set(rhs)) {
      const auto all_items_in = [&](const SetObject& source, const SetObject& target, bool& result) {
        for (const auto& source_item : source.items) {
          bool found = false;
          for (const auto& target_item : target.items) {
            if (value_is(source_item, target_item)) {
              found = true;
              break;
            }
            Value equal;
            if (!runtime_value_compare(runtime, "==", source_item, target_item, equal, error)) {
              return false;
            }
            bool is_equal = false;
            if (!runtime_truthy(runtime, equal, is_equal, error)) {
              return false;
            }
            if (is_equal) {
              found = true;
              break;
            }
          }
          if (!found) {
            result = false;
            return true;
          }
        }
        result = true;
        return true;
      };

      bool left_in_right = false;
      bool right_in_left = false;
      if (!all_items_in(*left_set, *right_set, left_in_right) ||
          !all_items_in(*right_set, *left_set, right_in_left)) {
        return false;
      }
      bool result = false;
      if (op == "==") result = left_in_right && right_in_left;
      else if (op == "!=") result = !left_in_right || !right_in_left;
      else if (op == "<") result = left_in_right && !right_in_left;
      else if (op == "<=") result = left_in_right;
      else if (op == ">") result = right_in_left && !left_in_right;
      else if (op == ">=") result = right_in_left;
      else {
        error = "unknown comparison operator";
        return false;
      }
      value_set_bool(out, result);
      return true;
    }
  }

  if (op == "==" || op == "!=") {
    const bool equality = op == "==";
    if (auto* left_method = value_as_bound_method(lhs)) {
      if (auto* right_method = value_as_bound_method(rhs)) {
        Value selves_equal;
        if (!runtime_value_compare(runtime, "==", left_method->self, right_method->self, selves_equal, error)) {
          return false;
        }
        const bool methods_equal = selves_equal.tag == ValueTag::Bool && selves_equal.as.b &&
            value_is(left_method->function, right_method->function);
        value_set_bool(out, equality ? methods_equal : !methods_equal);
        return true;
      }
    }
    if (auto* left_alias = value_as_generic_alias(lhs)) {
      if (auto* right_alias = value_as_generic_alias(rhs)) {
        if (left_alias->is_union != right_alias->is_union) {
          value_set_bool(out, !equality);
          return true;
        }
        if (left_alias->is_union) {
          auto* left_args = value_as_tuple(left_alias->args);
          auto* right_args = value_as_tuple(right_alias->args);
          if (left_args == nullptr || right_args == nullptr ||
              left_args->items.size() != right_args->items.size()) {
            value_set_bool(out, !equality);
            return true;
          }
          std::vector<bool> matched(right_args->items.size(), false);
          for (const auto& left_item : left_args->items) {
            bool found = false;
            for (size_t i = 0; i < right_args->items.size(); ++i) {
              if (matched[i]) continue;
              Value item_equal;
              if (!runtime_value_compare(
                      runtime, "==", left_item, right_args->items[i], item_equal, error)) {
                return false;
              }
              if (item_equal.tag == ValueTag::Bool && item_equal.as.b) {
                matched[i] = true;
                found = true;
                break;
              }
            }
            if (!found) {
              value_set_bool(out, !equality);
              return true;
            }
          }
          value_set_bool(out, equality);
          return true;
        }
        Value origins_equal;
        if (!runtime_value_compare(runtime, "==", left_alias->origin, right_alias->origin, origins_equal, error)) {
          return false;
        }
        if (origins_equal.tag != ValueTag::Bool || !origins_equal.as.b) {
          value_set_bool(out, !equality);
          return true;
        }
        Value args_equal;
        if (!runtime_value_compare(runtime, "==", left_alias->args, right_alias->args, args_equal, error)) {
          return false;
        }
        const bool aliases_equal = args_equal.tag == ValueTag::Bool && args_equal.as.b;
        value_set_bool(out, equality ? aliases_equal : !aliases_equal);
        return true;
      }
    }
    auto mapping_storage = [](const Value& value) -> const DictObject* {
      if (auto* dict = value_as_dict(value)) return dict;
      if (auto* instance = value_as_instance(value)) return value_as_dict(instance->mapping_storage);
      return nullptr;
    };
    if (auto* left_dict = mapping_storage(lhs)) {
      if (auto* right_dict = mapping_storage(rhs)) {
        if (left_dict->entries.size() != right_dict->entries.size()) {
          value_set_bool(out, !equality);
          return true;
        }
        for (const auto& left_entry : left_dict->entries) {
          const Value* matched_value = nullptr;
          for (const auto& right_entry : right_dict->entries) {
            Value keys_equal;
            if (!runtime_value_compare(runtime, "==", left_entry.first, right_entry.first, keys_equal, error)) {
              return false;
            }
            if (keys_equal.tag == ValueTag::Bool && keys_equal.as.b) {
              matched_value = &right_entry.second;
              break;
            }
          }
          if (matched_value == nullptr) {
            value_set_bool(out, !equality);
            return true;
          }
          Value values_equal;
          if (!runtime_value_compare(runtime, "==", left_entry.second, *matched_value, values_equal, error)) {
            return false;
          }
          if (values_equal.tag != ValueTag::Bool || !values_equal.as.b) {
            value_set_bool(out, !equality);
            return true;
          }
        }
        value_set_bool(out, equality);
        return true;
      }
    }
    if (auto* left_list = value_as_list_storage(lhs)) {
      if (auto* right_list = value_as_list_storage(rhs)) {
        if (left_list->items.size() != right_list->items.size()) {
          value_set_bool(out, !equality);
          return true;
        }
        for (size_t i = 0; i < left_list->items.size(); ++i) {
          if (value_is(left_list->items[i], right_list->items[i])) continue;
          Value items_equal;
          if (!runtime_value_compare(runtime, "==", left_list->items[i], right_list->items[i], items_equal, error)) {
            return false;
          }
          if (items_equal.tag != ValueTag::Bool || !items_equal.as.b) {
            value_set_bool(out, !equality);
            return true;
          }
        }
        value_set_bool(out, equality);
        return true;
      }
    }
    Value left_tuple_storage;
    Value right_tuple_storage;
    auto tuple_storage = [](const Value& value, Value& storage) -> const TupleObject* {
      if (auto* tuple = value_as_tuple(value)) return tuple;
      if (value_as_instance(value) == nullptr) return nullptr;
      std::string ignored;
      if (!object_get_attr(value, "_tuple", storage, ignored)) return nullptr;
      return value_as_tuple(storage);
    };
    if (auto* left_tuple = tuple_storage(lhs, left_tuple_storage)) {
      if (auto* right_tuple = tuple_storage(rhs, right_tuple_storage)) {
        if (left_tuple->items.size() != right_tuple->items.size()) {
          value_set_bool(out, !equality);
          return true;
        }
        for (size_t i = 0; i < left_tuple->items.size(); ++i) {
          if (value_is(left_tuple->items[i], right_tuple->items[i])) continue;
          Value items_equal;
          if (!runtime_value_compare(runtime, "==", left_tuple->items[i], right_tuple->items[i], items_equal, error)) {
            return false;
          }
          if (items_equal.tag != ValueTag::Bool || !items_equal.as.b) {
            value_set_bool(out, !equality);
            return true;
          }
        }
        value_set_bool(out, equality);
        return true;
      }
    }
  }
  return value_compare(op, lhs, rhs, out, error);
}

bool runtime_value_contains(
    Runtime& runtime,
    const Value& container,
    const Value& item,
    bool& out,
    std::string& error) {
  const auto contains_in = [&](const auto& items) {
    for (const auto& candidate : items) {
      if (value_is(candidate, item)) {
        out = true;
        return true;
      }
      Value equal;
      if (!runtime_value_compare(runtime, "==", candidate, item, equal, error)) return false;
      bool is_equal = false;
      if (!runtime_truthy(runtime, equal, is_equal, error)) return false;
      if (is_equal) {
        out = true;
        return true;
      }
    }
    out = false;
    return true;
  };
  if (auto* list = value_as_list(container)) return contains_in(list->items);
  if (auto* tuple = value_as_tuple(container)) return contains_in(tuple->items);
  if (auto* set = value_as_set(container)) return contains_in(set->items);
  return value_contains(container, item, out, error);
}

Value Value::instance(Value klass) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_instance_object();
  obj->klass = klass;
  if (auto* klass_obj = value_as_class(obj->klass)) {
    obj->slot_count = static_cast<uint32_t>(klass_obj->instance_slot_names.size());
    if (obj->slot_count > 8) {
      obj->overflow_slots.assign(obj->slot_count, Value::invalid());
    }
    if (class_has_builtin_base_name(klass_obj, "dict") ||
        class_has_builtin_base_name(klass_obj, "OrderedDict") ||
        class_has_builtin_base_name(klass_obj, "defaultdict")) {
      obj->mapping_storage = Value::dict({});
      obj->attrs.emplace_back("#__dict__", Value::dict({}));
    }
    if (class_has_builtin_base_name(klass_obj, "list")) {
      obj->sequence_storage = Value::list({});
    }
    if (class_has_builtin_base_name(klass_obj, "set")) {
      obj->sequence_storage = Value::set({});
    } else if (class_has_builtin_base_name(klass_obj, "frozenset")) {
      obj->sequence_storage = Value::frozenset({});
    }
  }
  if (obj->slot_count == 0) {
    obj->attrs.reserve(4);
  }
  v.as.obj = &obj->header;
  return v;
}

Value Value::bound_method(Value self, Value function) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<BoundMethodObject>(ObjectKind::BoundMethod);
  obj->self = std::move(self);
  obj->function = std::move(function);
  v.as.obj = &obj->header;
  return v;
}

Value Value::static_method(Value function) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<StaticMethodObject>(ObjectKind::StaticMethod);
  obj->function = std::move(function);
  v.as.obj = &obj->header;
  return v;
}

Value Value::class_method(Value function) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<ClassMethodObject>(ObjectKind::ClassMethod);
  obj->function = std::move(function);
  v.as.obj = &obj->header;
  return v;
}

Value Value::super_object(Value klass, Value self) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<SuperObject>(ObjectKind::Super);
  obj->klass = std::move(klass);
  obj->self = std::move(self);
  v.as.obj = &obj->header;
  return v;
}

Value slot_descriptor(std::string owner_name, std::string name, uint32_t index) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<SlotDescriptorObject>(ObjectKind::SlotDescriptor);
  value_set_invalid(obj->owner_class);
  obj->owner_name = std::move(owner_name);
  obj->name = std::move(name);
  obj->index = index;
  v.as.obj = &obj->header;
  return v;
}

void slot_descriptor_set_owner_class(Value& descriptor, const Value& owner_class) {
  if (auto* slot = value_as_slot_descriptor(descriptor)) {
    value_assign_fast(slot->owner_class, owner_class);
  }
}

void object_model_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::Class: {
      auto* klass = reinterpret_cast<ClassObject*>(object);
      for (const auto& base : klass->bases) {
        class_unregister_subclass(value_as_class(base), klass);
      }
      delete klass;
      break;
    }
    case ObjectKind::Instance:
      recycle_instance_object(reinterpret_cast<InstanceObject*>(object));
      break;
    case ObjectKind::BoundMethod:
      delete reinterpret_cast<BoundMethodObject*>(object);
      break;
    case ObjectKind::StaticMethod:
      delete reinterpret_cast<StaticMethodObject*>(object);
      break;
    case ObjectKind::ClassMethod:
      delete reinterpret_cast<ClassMethodObject*>(object);
      break;
    case ObjectKind::Super:
      delete reinterpret_cast<SuperObject*>(object);
      break;
    case ObjectKind::SlotDescriptor:
      delete reinterpret_cast<SlotDescriptorObject*>(object);
      break;
    default:
      break;
  }
}

bool event_subscribe(Value event, Value callable, uint64_t& cookie, std::string& error) {
  auto* object = value_as_event(event);
  if (object == nullptr) {
    error = "subscribe expected event object";
    return false;
  }
  if (callable.tag == ValueTag::Invalid || callable.tag == ValueTag::None) {
    error = "event handler is not callable";
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(object->mutex);
  cookie = object->next_cookie++;
  if (object->next_cookie == 0) {
    object->next_cookie = 1;
  }
  object->handlers.push_back(EventHandlerObject{cookie, std::move(callable)});
  auto changed = object->changed;
  if (changed && !(*changed)(object->handlers.size())) {
    error = "event subscription change handler failed";
    return false;
  }
  return true;
}

bool event_unsubscribe(Value event, uint64_t cookie, std::string& error) {
  auto* object = value_as_event(event);
  if (object == nullptr) {
    error = "unsubscribe expected event object";
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(object->mutex);
  auto it = std::remove_if(
      object->handlers.begin(),
      object->handlers.end(),
      [cookie](const EventHandlerObject& handler) { return handler.cookie == cookie; });
  if (it == object->handlers.end()) {
    return true;
  }
  object->handlers.erase(it, object->handlers.end());
  auto changed = object->changed;
  if (changed && !(*changed)(object->handlers.size())) {
    error = "event subscription change handler failed";
    return false;
  }
  return true;
}

bool event_fire(Runtime& runtime, Value event, const Value* args, uint32_t argc, Value& out, std::string& error) {
  return event_fire_kw(runtime, std::move(event), args, argc, {}, out, error);
}

bool event_fire_kw(Runtime& runtime, Value event, const Value* args, uint32_t argc,
    const std::vector<std::pair<std::string, Value>>& kwargs, Value& out, std::string& error) {
  auto* object = value_as_event(event);
  if (object == nullptr) {
    error = "fire expected event object";
    return false;
  }
  std::vector<EventHandlerObject> handlers;
  { std::lock_guard<std::recursive_mutex> lock(object->mutex); handlers = object->handlers; }
  Value last = Value::none();
  for (const auto& handler : handlers) {
    Value result;
    if (!runtime_call_callable_kw(runtime, handler.callable, args, argc, kwargs, result, error)) {
      return false;
    }
    value_assign_fast(last, result);
  }
  value_assign_fast(out, last);
  return true;
}

static bool event_subscribe_method(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "event.subscribe expects one handler";
    return false;
  }
  uint64_t cookie = 0;
  if (!event_subscribe(args[0], args[1], cookie, error)) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(cookie));
  return true;
}

static bool event_unsubscribe_method(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "event.unsubscribe expects one subscription cookie";
    return false;
  }
  if (!event_unsubscribe(args[0], static_cast<uint64_t>(args[1].as.i64), error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

static bool event_fire_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc == 0) {
    error = "event.fire missing event object";
    return false;
  }
  return event_fire(runtime, args[0], args + 1, argc - 1, out, error);
}

std::string object_model_to_string(const Value& value) {
  if (auto* klass = value_as_class(value)) {
    return "<class '" + class_display_name(*klass) + "'>";
  }
  if (auto* instance = value_as_instance(value)) {
    if (auto* klass = value_as_class(instance->klass)) {
      if (is_exception_class_name(klass->name) || class_has_builtin_base_name(klass, "BaseException")) {
        for (const auto& attr : instance->attrs) {
          if (attr.first == "message") {
            std::string message = value_to_string(attr.second);
            if (klass->name == "BaseExceptionGroup" || klass->name == "ExceptionGroup" ||
                class_has_builtin_base_name(klass, "BaseExceptionGroup")) {
              for (const auto& group_attr : instance->attrs) {
                if (group_attr.first != "exceptions") continue;
                if (auto* exceptions = value_as_tuple(group_attr.second)) {
                  message += " (" + std::to_string(exceptions->items.size()) + " sub-exception";
                  if (exceptions->items.size() != 1) message += "s";
                  message += ")";
                }
                break;
              }
            }
            return message;
          }
        }
      }
      for (const auto& attr : instance->attrs) {
        if (attr.first == "__xlang3_string_value__" && value_as_string(attr.second) != nullptr) {
          return string_object_to_string(*value_as_string(attr.second));
        }
      }
      for (const auto& attr : instance->attrs) {
        if (attr.first == "_tuple" && value_as_tuple(attr.second) != nullptr) {
          return "<" + klass->name + " object>";
        }
      }
      if (class_has_builtin_base_name(klass, "int")) {
        for (const auto& attr : instance->attrs) {
          if ((attr.first == "__xlang3_int_value__" || attr.first == "_value_") &&
              attr.second.tag == ValueTag::Int64) {
            return std::to_string(attr.second.as.i64);
          }
        }
      }
      if (class_has_builtin_base_name(klass, "float")) {
        for (const auto& attr : instance->attrs) {
          if (attr.first == "__xlang3_float_value__" && attr.second.tag == ValueTag::Double) {
            return value_to_string(attr.second);
          }
        }
      }
      if (instance->native_data != nullptr || !instance->native_type.empty()) {
        return "<" + class_display_name(*klass) + " object>";
      }
      if (attr_truthy_marker(klass, "__xlang3_compact_repr__")) {
        return "<" + klass->name + " object>";
      }
      if (attr_truthy_marker(klass, "__xlang3_enum_finalized__") ||
          enum_class_has_base_name(*klass, "Enum")) {
        return "<" + klass->name + " object>";
      }
      std::ostringstream address;
      address << "0x" << std::hex << reinterpret_cast<uintptr_t>(instance);
      return "<" + class_display_name(*klass) + " object at " + address.str() + ">";
    }
    return "<object>";
  }
  if (value_as_bound_method(value) != nullptr) {
    return "<bound method>";
  }
  if (value_as_static_method(value) != nullptr) {
    return "<staticmethod object>";
  }
  if (value_as_class_method(value) != nullptr) {
    return "<classmethod object>";
  }
  if (value_as_super(value) != nullptr) {
    return "<super object>";
  }
  if (auto* slot = value_as_slot_descriptor(value)) {
    return "<member '" + slot->name + "' of '" + slot->owner_name + "' objects>";
  }
  if (auto* event = value_as_event(value)) {
    return "<event '" + event->name + "'>";
  }
  return "<object>";
}

static bool native_function_descriptor_get_compat(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3 || value_as_native_function(args[0]) == nullptr) {
    error = "descriptor '__get__' requires a native function";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[1].tag == ValueTag::None) {
    value_assign_fast(out, args[0]);
  } else {
    out = Value::bound_method(args[1], args[0]);
  }
  return true;
}

bool object_get_attr(const Value& object, const std::string& name, Value& out, std::string& error) {
  if (auto* slot = value_as_slot_descriptor(object)) {
    if (name == "__name__") {
      out = Value::string(slot->name);
      return true;
    }
    if (name == "__objclass__") {
      if (slot->owner_class.tag != ValueTag::Invalid) {
        value_assign_fast(out, slot->owner_class);
      } else {
        out = Value::string(slot->owner_name);
      }
      return true;
    }
    if (name == "__module__") {
      out = Value::string("builtins");
      return true;
    }
    if (name == "__doc__") {
      value_set_none(out);
      return true;
    }
    if (name == "__get__") {
      out = Value::bound_method(
          object,
          Value::native_function(
              0,
              "member_descriptor.__get__",
              slot_descriptor_get_method,
              const_cast<char*>("__get__"),
              nullptr,
              nullptr,
              false,
              slot_descriptor_no_keyword_method));
      return true;
    }
    if (name == "__set__") {
      out = Value::bound_method(
          object,
          Value::native_function(
              0,
              "member_descriptor.__set__",
              slot_descriptor_set_method,
              const_cast<char*>("__set__"),
              nullptr,
              nullptr,
              false,
              slot_descriptor_no_keyword_method));
      return true;
    }
    if (name == "__delete__") {
      out = Value::bound_method(
          object,
          Value::native_function(
              0,
              "member_descriptor.__delete__",
              slot_descriptor_delete_method,
              const_cast<char*>("__delete__"),
              nullptr,
              nullptr,
              false,
              slot_descriptor_no_keyword_method));
      return true;
    }
    error = "member descriptor has no attribute '" + name + "'";
    return false;
  }

  if (auto* type_param = value_as_type_param(object)) {
    if (name == "__name__") {
      out = Value::string(type_param->name);
      return true;
    }
    if (name == "__bound__") {
      value_assign_fast(out, type_param->bound);
      return true;
    }
    if (name == "__default__") {
      value_assign_fast(out, type_param->default_value);
      return true;
    }
    error = "type parameter has no attribute '" + name + "'";
    return false;
  }

  if (auto* generic_alias = value_as_generic_alias(object)) {
    if (name == "__class__" && value_as_class(generic_alias->klass) != nullptr) {
      value_assign_fast(out, generic_alias->klass);
      return true;
    }
    if (name == "__origin__") {
      value_assign_fast(out, generic_alias->origin);
      return true;
    }
    if (name == "__args__") {
      value_assign_fast(out, generic_alias->args);
      return true;
    }
    if (name == "origin") {
      value_assign_fast(out, generic_alias->origin);
      return true;
    }
    if (name == "args") {
      auto* alias_args = value_as_tuple(generic_alias->args);
      if (alias_args != nullptr && alias_args->items.size() == 1) {
        value_assign_fast(out, alias_args->items[0]);
      } else {
        value_assign_fast(out, generic_alias->args);
      }
      return true;
    }
    if (name == "__parameters__") {
      out = Value::tuple({});
      return true;
    }
    if (name == "__name__" || name == "__qualname__" || name == "__module__" || name == "__dict__") {
      return object_get_attr(generic_alias->origin, name, out, error);
    }
    if (value_as_class(generic_alias->klass) != nullptr &&
        object_get_attr(generic_alias->klass, name, out, error)) {
      return true;
    }
    error = "GenericAlias object has no attribute '" + name + "'";
    return false;
  }

  if (auto* event = value_as_event(object)) {
    if (name == "__name__") {
      out = Value::string(event->name);
      return true;
    }
    if (name == "__doc__") {
      value_set_none(out);
      return true;
    }
    if (name == "subscribe") {
      out = Value::bound_method(object, Value::native_function(0, "event.subscribe", event_subscribe_method));
      return true;
    }
    if (name == "unsubscribe") {
      out = Value::bound_method(object, Value::native_function(0, "event.unsubscribe", event_unsubscribe_method));
      return true;
    }
    if (name == "fire" || name == "__call__") {
      out = Value::bound_method(object, Value::native_function(0, "event.fire", event_fire_method));
      return true;
    }
    error = "event has no attribute '" + name + "'";
    return false;
  }

  if (auto* view = value_as_memoryview(object)) {
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    if (name == "readonly") {
      value_set_bool(out, view->readonly);
      return true;
    }
    if (name == "nbytes") {
      value_set_int64(out, static_cast<int64_t>(view->size));
      return true;
    }
    if (name == "itemsize") {
      value_set_int64(out, static_cast<int64_t>(memoryview_format_itemsize(view->format)));
      return true;
    }
    if (name == "format") {
      out = Value::string(view->format);
      return true;
    }
    if (name == "ndim") {
      value_set_int64(out, 1);
      return true;
    }
    if (name == "shape") {
      out = Value::tuple({Value::int64(static_cast<int64_t>(memoryview_item_count(*view)))});
      return true;
    }
    if (name == "strides") {
      out = Value::tuple({Value::int64(static_cast<int64_t>(memoryview_format_itemsize(view->format)))});
      return true;
    }
    if (name == "suboffsets") {
      value_set_none(out);
      return true;
    }
    if (name == "obj") {
      value_assign_fast(out, view->owner);
      return true;
    }
    if (name == "c_contiguous" || name == "f_contiguous" || name == "contiguous") {
      value_set_bool(out, true);
      return true;
    }
    error = "memoryview has no attribute '" + name + "'";
    return false;
  }

  if (auto* function = value_as_function(object)) {
    if (function->attrs_dict.tag != ValueTag::Invalid) {
      std::string ignored;
      if (mapping_get_item(function->attrs_dict, Value::string(name), out, ignored)) {
        return true;
      }
    }
    if (name == "__name__") {
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        out = Value::string(function->module->functions[function->function_id].name);
      } else {
        out = Value::string("<function>");
      }
      return true;
    }
    if (name == "__qualname__") {
      if (!function->qualname.empty()) {
        out = Value::string(function->qualname);
      } else if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        out = Value::string(fn.qualname.empty() ? fn.name : fn.qualname);
      } else {
        out = Value::string("<function>");
      }
      return true;
    }
    if (name == "__module__") {
      if (auto* module = value_as_module(function->globals_module)) {
        Value module_name;
        std::string ignored;
        if (module_get_attr(function->globals_module, "__name__", module_name, ignored) &&
            value_as_string(module_name) != nullptr) {
          value_assign_fast(out, module_name);
        } else {
          out = Value::string(module->name);
        }
      } else {
        value_set_none(out);
      }
      return true;
    }
    if (name == "__doc__") {
      if (function->doc.tag == ValueTag::Invalid) {
        value_set_none(out);
      } else {
        value_assign_fast(out, function->doc);
      }
      return true;
    }
    if (name == "__defaults__") {
      if (function->positional_defaults.empty()) {
        value_set_none(out);
      } else {
        out = Value::tuple(function->positional_defaults);
      }
      return true;
    }
    if (name == "__kwdefaults__") {
      if (function->kwdefaults.empty()) {
        value_set_none(out);
      } else {
        std::vector<std::pair<Value, Value>> entries;
        entries.reserve(function->kwdefaults.size());
        for (const auto& entry : function->kwdefaults) {
          entries.push_back({Value::string(entry.first), entry.second});
        }
        out = Value::dict(std::move(entries));
      }
      return true;
    }
    if (name == "__type_params__") {
      std::vector<Value> values;
      values.reserve(function->type_params.size());
      for (const auto& type_param : function->type_params) {
        values.push_back(Value::type_param(type_param));
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "__annotations__") {
      if (function->annotations.tag == ValueTag::Invalid) {
        Value custom_annotate;
        std::string ignored;
        if (function->attrs_dict.tag != ValueTag::Invalid &&
            mapping_get_item(function->attrs_dict, Value::string("__annotate__"), custom_annotate, ignored)) {
          value_set_none(out);
        } else {
          out = Value::dict({});
        }
      } else {
        value_assign_fast(out, function->annotations);
      }
      return true;
    }
    if (name == "__annotate__") {
      out = Value::native_function(
          0,
          "function.__annotate__",
          function_annotate_method,
          new Value(object),
          function_annotate_cleanup);
      return true;
    }
    if (name == "__code__") {
      if (function->module == nullptr || function->function_id >= function->module->functions.size()) {
        value_set_none(out);
      } else {
        out = Value::code(function->module, function->function_id);
      }
      return true;
    }
    if (name == "__globals__") {
      if (function->globals_dict.tag != ValueTag::Invalid) {
        value_assign_fast(out, function->globals_dict);
      } else {
        value_assign_fast(out, function->globals_module);
      }
      return true;
    }
    if (name == "__get__") {
      out = Value::bound_method(object, Value::native_function(0, "function.__get__", function_descriptor_get_method));
      return true;
    }
    if (name == "__call__") {
      // A Python function's method-wrapper ultimately invokes the function
      // with the same arguments. Returning the function preserves that call
      // behavior for stdlib code that checks getattr(func, "__call__").
      value_assign_fast(out, object);
      return true;
    }
    if (name == "__closure__") {
      if (function->closure.empty()) {
        value_set_none(out);
      } else {
        out = Value::tuple(function->closure);
      }
      return true;
    }
    if (name == "__dict__") {
      if (function->attrs_dict.tag == ValueTag::Invalid) {
        function->attrs_dict = Value::dict({});
      }
      value_assign_fast(out, function->attrs_dict);
      return true;
    }
    error = "function has no attribute '" + name + "'";
    return false;
  }

  if (auto* code = value_as_code(object)) {
    if (code->module == nullptr || code->function_id >= code->module->functions.size()) {
      error = "invalid code object";
      return false;
    }
    const auto& fn = code->module->functions[code->function_id];
    if (name == "co_name") {
      out = Value::string(fn.name);
      return true;
    }
    if (name == "co_qualname") {
      out = Value::string(fn.qualname.empty() ? fn.name : fn.qualname);
      return true;
    }
    if (name == "co_filename") {
      out = Value::string(!code->filename_override.empty()
                              ? code->filename_override
                              : (code->module->source_file.empty() ? "<xlang3>" : code->module->source_file));
      return true;
    }
    if (name == "co_firstlineno") {
      out = Value::int64(code->first_line_override > 0 ? code->first_line_override : fn.first_line);
      return true;
    }
    if (name == "co_argcount") {
      uint32_t count = 0;
      if (!fn.signature.empty()) {
        for (const auto& param : fn.signature) {
          if (param.kind == ir::ParamKind::PosOnly || param.kind == ir::ParamKind::PosOrKeyword) {
            ++count;
          }
        }
      } else {
        count = static_cast<uint32_t>(fn.params.size());
      }
      out = Value::int64(count);
      return true;
    }
    if (name == "co_posonlyargcount") {
      uint32_t count = 0;
      for (const auto& param : fn.signature) {
        if (param.kind == ir::ParamKind::PosOnly) {
          ++count;
        }
      }
      out = Value::int64(count);
      return true;
    }
    if (name == "co_kwonlyargcount") {
      uint32_t count = 0;
      for (const auto& param : fn.signature) {
        if (param.kind == ir::ParamKind::KeywordOnly) {
          ++count;
        }
      }
      out = Value::int64(count);
      return true;
    }
    if (name == "co_nlocals") {
      out = Value::int64(static_cast<int64_t>(fn.locals.size()));
      return true;
    }
    if (name == "co_stacksize") {
      out = Value::int64(static_cast<int64_t>(fn.register_count));
      return true;
    }
    if (name == "co_flags") {
      if (code->flags_override >= 0) {
        out = Value::int64(code->flags_override);
        return true;
      }
      int64_t flags = 0x01 | 0x02;
      for (const auto& param : fn.signature) {
        if (param.kind == ir::ParamKind::VarArgs) {
          flags |= 0x04;
        } else if (param.kind == ir::ParamKind::KwArgs) {
          flags |= 0x08;
        }
      }
      if (fn.is_generator && !fn.is_coroutine && !fn.is_async) {
        flags |= 0x20;
      }
      if (fn.is_coroutine) {
        flags |= 0x80;
      }
      if (fn.is_async && fn.is_generator && !fn.is_coroutine) {
        flags |= 0x200;
      }
      out = Value::int64(flags);
      return true;
    }
    if (name == "co_varnames") {
      std::vector<Value> values;
      values.reserve(fn.locals.size());
      std::unordered_set<std::string> parameter_names;
      auto append_parameters = [&](ir::ParamKind kind) {
        for (const auto& param : fn.signature) {
          if (param.kind == kind) {
            values.push_back(Value::string(param.name));
            parameter_names.insert(param.name);
          }
        }
      };
      if (!fn.signature.empty()) {
        append_parameters(ir::ParamKind::PosOnly);
        append_parameters(ir::ParamKind::PosOrKeyword);
        append_parameters(ir::ParamKind::KeywordOnly);
        append_parameters(ir::ParamKind::VarArgs);
        append_parameters(ir::ParamKind::KwArgs);
      }
      for (const auto& local : fn.locals) {
        if (parameter_names.find(local) == parameter_names.end()) {
          values.push_back(Value::string(local));
        }
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_freevars") {
      std::vector<Value> values;
      values.reserve(fn.free_vars.size());
      for (const auto& item : fn.free_vars) {
        values.push_back(Value::string(item));
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_cellvars") {
      std::vector<Value> values;
      values.reserve(fn.cell_slots.size());
      for (const auto slot : fn.cell_slots) {
        if (slot < fn.locals.size()) {
          values.push_back(Value::string(fn.locals[slot]));
        }
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_names") {
      const auto names = code_object_names(*code->module, fn);
      std::vector<Value> values;
      values.reserve(names.size());
      for (const auto& item : names) {
        values.push_back(Value::string(item));
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_consts") {
      std::vector<Value> values;
      values.reserve(fn.constants.size());
      for (const auto& constant : fn.constants) {
        // Invalid is an internal register/cell sentinel, not a Python
        // constant.  Exposing it makes iteration assign an unbound value.
        if (constant.tag != ValueTag::Invalid) {
          values.push_back(constant);
        }
      }
      std::unordered_set<uint32_t> nested_ids;
      for (const auto& instruction : fn.code) {
        if (instruction.op == ir::Op::MakeFunction &&
            instruction.b < code->module->functions.size() &&
            nested_ids.insert(instruction.b).second) {
          values.push_back(Value::code(code->module, instruction.b));
        }
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_code") {
      out = Value::bytes(code_object_compat_bytecode(*code->module, fn));
      return true;
    }
    if (name == "co_linetable" || name == "co_exceptiontable") {
      out = Value::bytes({});
      return true;
    }
    if (name == "co_lines") {
      out = Value::bound_method(object, Value::native_function(0, "code.co_lines", code_lines_method));
      return true;
    }
    if (name == "co_positions") {
      out = Value::bound_method(object, Value::native_function(0, "code.co_positions", code_positions_method));
      return true;
    }
    if (name == "_varname_from_oparg") {
      out = Value::bound_method(
          object,
          Value::native_function(0, "code._varname_from_oparg", code_varname_from_oparg_method));
      return true;
    }
    if (name == "replace") {
      out = Value::bound_method(
          object,
          Value::native_function(0, "code.replace", code_replace_method, nullptr, nullptr, nullptr, false,
                                 code_replace_method_kw));
      return true;
    }
    error = "code has no attribute '" + name + "'";
    return false;
  }

  if (auto* frame = value_as_frame(object)) {
    if (name == "f_code") {
      if (frame->module == nullptr) {
        value_set_none(out);
      } else {
        out = Value::code(frame->module, frame->function_id);
      }
      return true;
    }
    if (name == "f_globals") {
      value_assign_fast(out, frame->globals_module);
      return true;
    }
    if (name == "f_builtins") {
      if (frame->builtins.tag == ValueTag::Invalid) {
        out = Value::dict({});
      } else {
        value_assign_fast(out, frame->builtins);
      }
      return true;
    }
    if (name == "f_back") {
      if (frame->back.tag == ValueTag::Invalid) {
        value_set_none(out);
      } else {
        value_assign_fast(out, frame->back);
      }
      return true;
    }
    if (name == "f_lineno") {
      out = Value::int64(frame_source_line(*frame));
      return true;
    }
    if (name == "f_lasti") {
      out = Value::int64(static_cast<int64_t>(frame->instruction_index + 1) * 2);
      return true;
    }
    if (name == "f_locals") {
      if (frame->locals.tag == ValueTag::Invalid) {
        out = Value::dict({});
      } else {
        value_assign_fast(out, frame->locals);
      }
      return true;
    }
    if (name == "f_trace") {
      value_set_none(out);
      return true;
    }
    if (name == "f_trace_lines") {
      value_set_bool(out, true);
      return true;
    }
    if (name == "f_trace_opcodes") {
      value_set_bool(out, false);
      return true;
    }
    if (name == "clear") {
      out = Value::bound_method(
          object,
          Value::native_function(0, "frame.clear", frame_clear_method));
      return true;
    }
    error = "frame has no attribute '" + name + "'";
    return false;
  }

  if (auto* traceback = value_as_traceback(object)) {
    if (name == "tb_frame") {
      value_assign_fast(out, traceback->frame);
      return true;
    }
    if (name == "tb_next") {
      value_assign_fast(out, traceback->next);
      return true;
    }
    if (name == "tb_lineno") {
      out = Value::int64(traceback->line);
      return true;
    }
    if (name == "tb_lasti") {
      if (traceback->lasti != -2) {
        out = Value::int64(traceback->lasti);
      } else if (auto* frame = value_as_frame(traceback->frame)) {
        out = Value::int64(static_cast<int64_t>(frame->instruction_index + 1) * 2);
      } else {
        out = Value::int64(-1);
      }
      return true;
    }
    error = "traceback has no attribute '" + name + "'";
    return false;
  }

  if (auto* cell = value_as_cell(object)) {
    if (name == "cell_contents") {
      if (cell->value.tag == ValueTag::Invalid) {
        error = "Cell is empty";
        return false;
      }
      value_assign_fast(out, cell->value);
      return true;
    }
    error = "cell has no attribute '" + name + "'";
    return false;
  }

  if (auto* native = value_as_native_function(object)) {
    if (native->attrs_dict != nullptr && native->attrs_dict->tag != ValueTag::Invalid) {
      std::string ignored;
      if (mapping_get_item(*native->attrs_dict, Value::string(name), out, ignored)) {
        return true;
      }
    }
    if (name == "__name__") {
      const size_t separator = native->name.rfind('.');
      out = Value::string(separator == std::string::npos
          ? native->name
          : native->name.substr(separator + 1));
      return true;
    }
    if (name == "__get__" && native->bind_as_descriptor) {
      Value get = Value::native_function(
          0,
          "method_descriptor.__get__",
          native_function_descriptor_get_compat,
          nullptr,
          nullptr,
          nullptr,
          false,
          nullptr,
          false);
      out = Value::bound_method(object, std::move(get));
      return true;
    }
    if (name == "__call__") {
      value_assign_fast(out, object);
      return true;
    }
    if (name == "__module__") {
      const size_t separator = native->name.find('.');
      out = Value::string(
          separator == std::string::npos ? "builtins" : native->name.substr(0, separator));
      return true;
    }
    if (name == "__qualname__") {
      out = Value::string(native->name);
      return true;
    }
    if (name == "__doc__") {
      value_set_none(out);
      return true;
    }
    if (name == "__annotations__") {
      out = Value::dict({});
      return true;
    }
    if (name == "__defaults__" || name == "__kwdefaults__" || name == "__closure__" ||
        name == "__text_signature__") {
      value_set_none(out);
      return true;
    }
    if (name == "__dict__") {
      if (native->attrs_dict == nullptr) {
        native->attrs_dict = new Value(Value::dict({}));
      }
      value_assign_fast(out, *native->attrs_dict);
      return true;
    }
    error = "function has no attribute '" + name + "'";
    return false;
  }

  if (auto* bound = value_as_bound_method(object)) {
    if (name == "__self__") {
      value_assign_fast(out, bound->self);
      return true;
    }
    if (name == "__func__") {
      value_assign_fast(out, bound->function);
      return true;
    }
    if (name == "__call__") {
      value_assign_fast(out, object);
      return true;
    }
    if (name == "__name__") {
      return callable_name_attr(bound->function, out);
    }
    if (name == "__qualname__") {
      return callable_qualname_attr(bound->function, out);
    }
    if (name == "__module__" && value_as_native_function(bound->function) != nullptr) {
      value_set_none(out);
      return true;
    }
    if (name == "__module__" || name == "__doc__" || name == "__annotations__" || name == "__text_signature__") {
      return callable_metadata_attr(bound->function, name, out);
    }
    std::string function_error;
    if (object_get_attr(bound->function, name, out, function_error)) {
      return true;
    }
    error = "method has no attribute '" + name + "'";
    return false;
  }

  if (auto* method = value_as_static_method(object)) {
    if (method->attrs_dict.tag != ValueTag::Invalid) {
      std::string ignored;
      if (mapping_get_item(method->attrs_dict, Value::string(name), out, ignored)) {
        return true;
      }
    }
    if (name == "__func__") {
      value_assign_fast(out, method->function);
      return true;
    }
    if (name == "__wrapped__") {
      value_assign_fast(out, method->function);
      return true;
    }
    if (name == "__get__") {
      out = Value::bound_method(
          object,
          Value::native_function(0, "staticmethod.__get__", staticmethod_descriptor_get_method));
      return true;
    }
    if (name == "__name__") {
      return callable_name_attr(method->function, out);
    }
    if (name == "__qualname__") {
      return callable_qualname_attr(method->function, out);
    }
    if (name == "__module__" || name == "__doc__" || name == "__annotations__") {
      return callable_metadata_attr(method->function, name, out);
    }
    if (name == "__dict__") {
      if (method->attrs_dict.tag == ValueTag::Invalid) {
        method->attrs_dict = Value::dict({});
      }
      value_assign_fast(out, method->attrs_dict);
      return true;
    }
    error = "staticmethod has no attribute '" + name + "'";
    return false;
  }

  if (auto* method = value_as_class_method(object)) {
    if (method->attrs_dict.tag != ValueTag::Invalid) {
      std::string ignored;
      if (mapping_get_item(method->attrs_dict, Value::string(name), out, ignored)) {
        return true;
      }
    }
    if (name == "__func__") {
      value_assign_fast(out, method->function);
      return true;
    }
    if (name == "__wrapped__") {
      value_assign_fast(out, method->function);
      return true;
    }
    if (name == "__get__") {
      out = Value::bound_method(
          object,
          Value::native_function(0, "classmethod.__get__", classmethod_descriptor_get_method));
      return true;
    }
    if (name == "__name__") {
      return callable_name_attr(method->function, out);
    }
    if (name == "__qualname__") {
      return callable_qualname_attr(method->function, out);
    }
    if (name == "__module__" || name == "__doc__" || name == "__annotations__") {
      return callable_metadata_attr(method->function, name, out);
    }
    if (name == "__dict__") {
      if (method->attrs_dict.tag == ValueTag::Invalid) {
        method->attrs_dict = Value::dict({});
      }
      value_assign_fast(out, method->attrs_dict);
      return true;
    }
    error = "classmethod has no attribute '" + name + "'";
    return false;
  }

  if (auto* super = value_as_super(object)) {
    if (name == "__self__") {
      value_assign_fast(out, super->self);
      return true;
    }
    if (name == "__thisclass__") {
      value_assign_fast(out, super->klass);
      return true;
    }
    auto* klass = value_as_class(super->klass);
    if (klass == nullptr) {
      error = "super object has invalid class";
      return false;
    }
    ClassObject* lookup_klass = klass;
    if (auto* self_instance = value_as_instance(super->self)) {
      if (auto* self_klass = value_as_class(self_instance->klass)) {
        lookup_klass = self_klass;
      }
    } else if (auto* self_klass = value_as_class(super->self)) {
      lookup_klass = self_klass;
      const std::vector<Value>* self_mro = nullptr;
      std::string self_mro_error;
      if (class_mro_values(self_klass, self_mro, self_mro_error)) {
        const bool target_in_class_mro = std::any_of(
            self_mro->begin(), self_mro->end(),
            [klass](const Value& value) { return value_as_class(value) == klass; });
        if (!target_in_class_mro) {
          if (auto* metaclass = value_as_class(self_klass->metaclass)) {
            lookup_klass = metaclass;
          }
        }
      }
    }
    const std::vector<Value>* mro = nullptr;
    if (!class_mro_values(lookup_klass, mro, error)) {
      return false;
    }
    bool start_class_in_mro = false;
    for (const auto& class_value : *mro) {
      if (value_as_class(class_value) == klass) {
        start_class_in_mro = true;
        break;
      }
    }
    if (!start_class_in_mro && lookup_klass != klass && !class_mro_values(klass, mro, error)) {
      return false;
    }
    bool use_next = false;
    Value attr;
    bool found = false;
    for (const auto& class_value : *mro) {
      auto* candidate = value_as_class(class_value);
      if (candidate == nullptr) {
        continue;
      }
      if (!use_next) {
        if (candidate == klass) {
          use_next = true;
        }
        continue;
      }
      auto it = candidate->attrs.find(name);
      if (it != candidate->attrs.end()) {
        value_assign_fast(attr, it->second);
        found = true;
        break;
      }
    }
    if (!found) {
      if (auto* instance = value_as_instance(super->self)) {
        for (const auto& instance_attr : instance->attrs) {
          if (instance_attr.first == name) {
            value_assign_fast(out, instance_attr.second);
            return true;
          }
        }
      }
      error = "super object has no attribute '" + name + "'";
      return false;
    }
    if (auto* method = value_as_static_method(attr)) {
      value_assign_fast(out, method->function);
    } else if (auto* method = value_as_class_method(attr)) {
      Value function;
      value_assign_fast(function, method->function);
      out = Value::bound_method(super->klass, std::move(function));
    } else if (Value function; descriptor_instance_payload(attr, "staticmethod", function)) {
      value_assign_fast(out, function);
    } else if (Value function; descriptor_instance_payload(attr, "classmethod", function)) {
      out = Value::bound_method(super->klass, std::move(function));
    } else if (value_as_function(attr) != nullptr ||
               (value_as_native_function(attr) != nullptr && value_as_native_function(attr)->bind_as_descriptor)) {
      if (name == "__new__") {
        value_assign_fast(out, attr);
      } else {
        out = Value::bound_method(super->self, attr);
      }
    } else {
      value_assign_fast(out, attr);
    }
    return true;
  }

  if (auto* klass = value_as_class(object)) {
    if (name == "__class__") {
      if (klass->metaclass.tag == ValueTag::Invalid) {
        error = "class has no metaclass";
        return false;
      }
      value_assign_fast(out, klass->metaclass);
      return true;
    }
    if (name == "__name__") {
      out = Value::string(klass->name);
      return true;
    }
    if (name == "__base__") {
      if (klass->base.tag == ValueTag::Invalid) {
        value_set_none(out);
      } else {
        value_assign_fast(out, klass->base);
      }
      return true;
    }
    if (name == "__bases__") {
      out = Value::tuple(klass->bases);
      return true;
    }
    if (name == "__mro__") {
      const std::vector<Value>* mro = nullptr;
      if (!class_mro_values(klass, mro, error)) {
        return false;
      }
      out = Value::tuple(*mro);
      return true;
    }
    if (name == "__flags__") {
      constexpr int64_t kTypeFlagIsAbstract = 1LL << 20;
      const auto abstracts = klass->attrs.find("__abstractmethods__");
      const auto* abstract_set = abstracts == klass->attrs.end() ? nullptr : value_as_set(abstracts->second);
      out = Value::int64(abstract_set != nullptr && !abstract_set->items.empty() ? kTypeFlagIsAbstract : 0);
      return true;
    }
    if (name == "__dictoffset__") {
      value_set_int64(out, klass->allow_instance_dict ? 1 : 0);
      return true;
    }
    if (name == "__weakrefoffset__") {
      value_set_int64(out, klass->allow_weakref ? 1 : 0);
      return true;
    }
    if (name == "__dict__") {
      out = mapping_proxy(object);
      return true;
    }
    if (name == "__weakref__" && klass->allow_weakref) {
      out = slot_descriptor(klass->name, "__weakref__", UINT32_MAX);
      slot_descriptor_set_owner_class(out, object);
      return true;
    }
    if (name == "__annotations__") {
      auto it = klass->attrs.find("__annotations__");
      if (it != klass->attrs.end()) {
        value_assign_fast(out, it->second);
      } else {
        out = Value::dict({});
        klass->attrs["__annotations__"] = out;
      }
      return true;
    }
    if (name == "__doc__") {
      auto it = klass->attrs.find("__doc__");
      if (it == klass->attrs.end()) {
        value_set_none(out);
      } else {
        value_assign_fast(out, it->second);
      }
      return true;
    }
    if (name == "__text_signature__") {
      auto text_signature = klass->attrs.find(name);
      if (klass->globals_module.tag == ValueTag::Invalid && text_signature != klass->attrs.end()) {
        value_assign_fast(out, text_signature->second);
        return true;
      }
      auto doc = klass->attrs.find("__doc__");
      auto* doc_string = doc == klass->attrs.end() ? nullptr : value_as_string(doc->second);
      if (doc_string != nullptr) {
        const std::string text = string_object_to_string(*doc_string);
        const size_t line_end = text.find('\n');
        const std::string_view first_line(text.data(), line_end == std::string::npos ? text.size() : line_end);
        const std::string prefix = klass->name + "(";
        const bool clinic_marker = line_end != std::string::npos &&
            text.substr(line_end, 5) == "\n--\n\n";
        if (clinic_marker && first_line.rfind(prefix, 0) == 0 && first_line.back() == ')') {
          out = Value::string(std::string(first_line.substr(klass->name.size())));
          return true;
        }
      }
      value_set_none(out);
      return true;
    }
    if (!class_lookup_attr(klass, name, out, error)) {
      if (name == "__members__" && class_lookup_attr(klass, "_member_map_", out, error)) {
        return true;
      }
      auto* metaclass = value_as_class(klass->metaclass);
      if (metaclass != nullptr) {
        Value meta_attr;
        std::string meta_error;
        if (class_lookup_attr(metaclass, name, meta_attr, meta_error)) {
          return bind_metaclass_attr_for_class_access(object, std::move(meta_attr), out);
        }
      }
      error = "class '" + klass->name + "' has no attribute '" + name + "'";
      return false;
    }
    if (auto* method = value_as_static_method(out)) {
      Value function;
      value_assign_fast(function, method->function);
      out = std::move(function);
      return true;
    }
    if (auto* method = value_as_class_method(out)) {
      Value function;
      value_assign_fast(function, method->function);
      out = Value::bound_method(object, std::move(function));
      return true;
    }
    if (Value function; descriptor_instance_payload(out, "staticmethod", function)) {
      value_assign_fast(out, function);
      return true;
    }
    if (Value function; descriptor_instance_payload(out, "classmethod", function)) {
      out = Value::bound_method(object, std::move(function));
      return true;
    }
    return true;
  }

  if (auto* instance = value_as_instance(object)) {
    if (name == "__class__") {
      value_assign_fast(out, instance->klass);
      return true;
    }
    auto* klass = value_as_class(instance->klass);
    if (klass == nullptr) {
      error = "instance has invalid class";
      return false;
    }
    if (name == "__weakref__") {
      if (!klass->allow_weakref) {
        error = "object has no attribute '__weakref__'";
        return false;
      }
      if (!weakref_find_ref(object, out)) {
        value_set_none(out);
      }
      return true;
    }
    if (name == "__dict__") {
      if (instance->native_get_attr != nullptr && instance->native_get_attr(object, name, out, error)) {
        return true;
      }
      if (klass->restrict_instance_attrs && !klass->allow_instance_dict) {
        error = "object has no attribute '__dict__'";
        return false;
      }
      const bool created_attribute_dict = instance_attribute_storage(*instance).tag == ValueTag::Invalid;
      if (created_attribute_dict) {
        instance_attribute_storage(*instance) = Value::dict({});
      }
      auto sync_dict_attr = [&](const std::string& attr_name, const Value& attr_value) {
        std::string ignored;
        mapping_set_item(instance_attribute_storage(*instance), Value::string(attr_name), attr_value, ignored);
      };
      if (created_attribute_dict) {
        if (klass != nullptr) {
          const uint32_t count = instance_slot_count(instance);
          for (size_t i = 0; i < klass->instance_slot_names.size() && i < count; ++i) {
            const auto& slot_value = instance_slot_at(instance, static_cast<uint32_t>(i));
            if (slot_value.tag != ValueTag::Invalid) {
              sync_dict_attr(klass->instance_slot_names[i], slot_value);
            }
          }
        }
        for (const auto& attr : instance->attrs) {
          if (!attr.first.empty() && attr.first[0] == '#') {
            continue;
          }
          if (attr.first.rfind("__xlang3_", 0) == 0) {
            continue;
          }
          sync_dict_attr(attr.first, attr.second);
        }
      }
      value_assign_fast(out, instance_attribute_storage(*instance));
      return true;
    }
    if (instance->native_get_attr != nullptr && instance->native_get_attr(object, name, out, error)) {
      return true;
    }
    auto slot_it = klass->instance_slot_indices.find(name);
    if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
      const auto& slot_value = instance_slot_at(instance, slot_it->second);
      if (slot_value.tag != ValueTag::Invalid) {
        value_assign_fast(out, slot_value);
        return true;
      }
      if (value_as_dict(instance_attribute_storage(*instance)) != nullptr &&
          mapping_get_item(instance_attribute_storage(*instance), Value::string(name), out, error)) {
        return true;
      }
    }
    const bool has_attribute_dict = value_as_dict(instance_attribute_storage(*instance)) != nullptr;
    if (has_attribute_dict) {
      if (mapping_get_item(instance_attribute_storage(*instance), Value::string(name), out, error)) {
        return true;
      }
      // Runtime payloads are deliberately omitted from an instance's public
      // __dict__.  They remain in the compact attribute storage after a
      // __dict__ is created and must still be visible to native base-type
      // methods (for example methods inherited by a str subclass).
      if (name.rfind("__xlang3_", 0) == 0) {
        for (const auto& attr : instance->attrs) {
          if (attr.first == name) {
            value_assign_fast(out, attr.second);
            return true;
          }
        }
      }
    } else {
      for (const auto& attr : instance->attrs) {
        if (attr.first == name) {
          value_assign_fast(out, attr.second);
          return true;
        }
      }
    }
    if (is_exception_class_name(klass->name) || class_has_builtin_base_name(klass, "BaseException")) {
      if (name == "__traceback__" || name == "__cause__" || name == "__context__") {
        value_set_none(out);
        return true;
      }
      if (name == "__suppress_context__") {
        value_set_bool(out, false);
        return true;
      }
      if (name == "args") {
        out = Value::tuple({});
        return true;
      }
    }
    if (name == "__name__") {
      Value fget;
      if (instance_subclass_field(object, "property", "fget", fget) &&
          fget.tag != ValueTag::None && fget.tag != ValueTag::Invalid) {
        std::string ignored;
        if (object_get_attr(fget, "__name__", out, ignored)) {
          return true;
        }
      }
    }
    if (name == "__text_signature__") {
      const std::vector<Value>* mro = nullptr;
      if (!class_mro_values(klass, mro, error)) {
        return false;
      }
      for (size_t i = 0; i < mro->size(); ++i) {
        auto* candidate = value_as_class((*mro)[i]);
        if (candidate == nullptr) {
          continue;
        }
        auto attr = candidate->attrs.find(name);
        if (attr == candidate->attrs.end() || attr->second.tag == ValueTag::Invalid) {
          continue;
        }
        if (i + 1 == mro->size()) {
          error = "object has no attribute '__text_signature__'";
          return false;
        }
        break;
      }
    }
    Value class_attr;
    if (!class_lookup_attr(klass, name, class_attr, error)) {
      if (name == "__doc__") {
        value_set_none(out);
        return true;
      }
      if (value_as_dict(instance->mapping_storage) != nullptr && dict_get_method(instance->mapping_storage, name, out)) {
        return true;
      }
      if (value_as_list(instance->sequence_storage) != nullptr && list_get_method(instance->sequence_storage, name, out)) {
        return true;
      }
      if (value_as_set(instance->sequence_storage) != nullptr && set_get_method(instance->sequence_storage, name, out)) {
        return true;
      }
      error = "object has no attribute '" + name + "'";
      return false;
    }
    if (auto* slot = value_as_slot_descriptor(class_attr)) {
      if (slot->index >= instance_slot_count(instance)) {
        for (const auto& instance_attr : instance->attrs) {
          if (instance_attr.first == slot->name) {
            value_assign_fast(out, instance_attr.second);
            return true;
          }
        }
        Value tuple_value;
        std::string tuple_error;
        if (object_get_attr(object, "_tuple", tuple_value, tuple_error)) {
          if (auto* tuple = value_as_tuple(tuple_value); tuple != nullptr && slot->index < tuple->items.size()) {
            value_assign_fast(out, tuple->items[slot->index]);
            return true;
          }
        }
        error = "descriptor does not apply to this object";
        return false;
      }
      const auto& slot_value = instance_slot_at(instance, slot->index);
      if (slot_value.tag == ValueTag::Invalid) {
        if (value_as_dict(instance_attribute_storage(*instance)) != nullptr &&
            mapping_get_item(instance_attribute_storage(*instance), Value::string(slot->name), out, error)) {
          return true;
        }
        Value tuple_value;
        std::string tuple_error;
        if (object_get_attr(object, "_tuple", tuple_value, tuple_error)) {
          if (auto* tuple = value_as_tuple(tuple_value); tuple != nullptr && slot->index < tuple->items.size()) {
            value_assign_fast(out, tuple->items[slot->index]);
            return true;
          }
        }
        error = "object has no attribute '" + slot->name + "'";
        return false;
      }
      value_assign_fast(out, slot_value);
      return true;
    }
    if (auto* method = value_as_static_method(class_attr)) {
      value_assign_fast(out, method->function);
    } else if (auto* method = value_as_class_method(class_attr)) {
      Value function;
      value_assign_fast(function, method->function);
      out = Value::bound_method(instance->klass, std::move(function));
    } else if (Value function; descriptor_instance_payload(class_attr, "staticmethod", function)) {
      value_assign_fast(out, function);
    } else if (Value function; descriptor_instance_payload(class_attr, "classmethod", function)) {
      out = Value::bound_method(instance->klass, std::move(function));
    } else if (value_as_function(class_attr) != nullptr ||
               (value_as_native_function(class_attr) != nullptr && value_as_native_function(class_attr)->bind_as_descriptor)) {
      out = Value::bound_method(object, class_attr);
    } else {
      value_assign_fast(out, class_attr);
    }
    return true;
  }

  if (name == "__doc__") {
    value_set_none(out);
    return true;
  }
  error = "object has no attributes";
  return false;
}

bool object_set_attr(Value& object, const std::string& name, const Value& value, std::string& error) {
  if (object.tag == ValueTag::Object && object.as.obj != nullptr &&
      object.as.obj->kind == ObjectKind::File) {
    auto* file = reinterpret_cast<FileObject*>(object.as.obj);
    auto* text = value_as_string(value);
    if ((name == "name" || name == "mode") && text != nullptr) {
      if (name == "name") {
        file->path = string_object_to_string(*text);
      } else {
        file->mode = string_object_to_string(*text);
      }
      return true;
    }
    error = "file attribute '" + name + "' is read-only";
    return false;
  }
  if (auto* cell = value_as_cell(object)) {
    if (name == "cell_contents") {
      value_assign_fast(cell->value, value);
      return true;
    }
    error = "cell attribute '" + name + "' is read-only";
    return false;
  }

  if (auto* function = value_as_function(object)) {
    if (name == "__qualname__") {
      auto* string = value_as_string(value);
      if (string == nullptr) {
        error = "__qualname__ must be set to a string";
        return false;
      }
      function->qualname = string_object_to_string(*string);
      return true;
    }
    if (name == "__defaults__") {
      if (value.tag == ValueTag::None) {
        function->positional_defaults.clear();
        function->defaults.clear();
        return true;
      }
      auto* tuple = value_as_tuple(value);
      if (tuple == nullptr) {
        error = "__defaults__ must be set to a tuple or None";
        return false;
      }
      function->positional_defaults.clear();
      function->positional_defaults.reserve(tuple->items.size());
      for (const auto& item : tuple->items) {
        function->positional_defaults.push_back(item);
      }
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        // A trailing invalid entry identifies defaults assigned through the
        // writable function.__defaults__ attribute.  The preceding entries
        // are indexed by parameter, allowing defaults to be added to a
        // function whose original code signature had none.
        function->defaults.assign(fn.signature.size() + 1, Value::invalid());
        std::vector<size_t> positional_params;
        for (size_t i = 0; i < fn.signature.size(); ++i) {
          if (fn.signature[i].kind == ir::ParamKind::PosOnly ||
              fn.signature[i].kind == ir::ParamKind::PosOrKeyword) {
            positional_params.push_back(i);
          }
        }
        const size_t copy_count = std::min(positional_params.size(), function->positional_defaults.size());
        const size_t param_start = positional_params.size() - copy_count;
        const size_t value_start = function->positional_defaults.size() - copy_count;
        for (size_t i = 0; i < copy_count; ++i) {
          value_assign_fast(function->defaults[positional_params[param_start + i]],
                            function->positional_defaults[value_start + i]);
        }
        for (size_t i = 0; i < fn.signature.size(); ++i) {
          if (fn.signature[i].kind != ir::ParamKind::KeywordOnly) {
            continue;
          }
          for (const auto& item : function->kwdefaults) {
            if (item.first == fn.signature[i].name) {
              value_assign_fast(function->defaults[i], item.second);
              break;
            }
          }
        }
      }
      return true;
    }
    if (name == "__kwdefaults__") {
      if (value.tag == ValueTag::None) {
        function->kwdefaults.clear();
        if (function->module != nullptr && function->function_id < function->module->functions.size()) {
          const auto& fn = function->module->functions[function->function_id];
          const bool dynamic_defaults =
              function->defaults.size() == fn.signature.size() + 1 &&
              !function->defaults.empty() && function->defaults.back().tag == ValueTag::Invalid;
          for (size_t i = 0; i < fn.signature.size(); ++i) {
            const auto& param = fn.signature[i];
            const size_t default_index = dynamic_defaults ? i : param.default_reg;
            if (param.kind == ir::ParamKind::KeywordOnly &&
                param.default_reg != UINT32_MAX &&
                default_index < function->defaults.size()) {
              value_set_invalid(function->defaults[default_index]);
            }
          }
        }
        return true;
      }
      auto* dict = value_as_dict(value);
      if (dict == nullptr) {
        error = "__kwdefaults__ must be set to a dict or None";
        return false;
      }
      function->kwdefaults.clear();
      function->kwdefaults.reserve(dict->entries.size());
      for (const auto& entry : dict->entries) {
        auto* key = value_as_string(entry.first);
        if (key == nullptr) {
          error = "__kwdefaults__ keys must be strings";
          return false;
        }
        function->kwdefaults.push_back({string_object_to_string(*key), entry.second});
      }
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        const bool dynamic_defaults =
            function->defaults.size() == fn.signature.size() + 1 &&
            !function->defaults.empty() && function->defaults.back().tag == ValueTag::Invalid;
        for (size_t i = 0; i < fn.signature.size(); ++i) {
          const auto& param = fn.signature[i];
          const size_t default_index = dynamic_defaults ? i : param.default_reg;
          if (param.kind != ir::ParamKind::KeywordOnly ||
              param.default_reg == UINT32_MAX ||
              default_index >= function->defaults.size()) {
            continue;
          }
          value_set_invalid(function->defaults[default_index]);
          for (const auto& item : function->kwdefaults) {
            if (item.first == param.name) {
              value_assign_fast(function->defaults[default_index], item.second);
              break;
            }
          }
        }
      }
      return true;
    }
    if (name == "__annotations__") {
      value_assign_fast(function->annotations, value);
      return true;
    }
    if (name == "__annotate__") {
      value_set_invalid(function->annotations);
    }
    if (name == "__doc__") {
      value_assign_fast(function->doc, value);
      return true;
    }
    if (function->attrs_dict.tag == ValueTag::Invalid) {
      function->attrs_dict = Value::dict({});
    }
    return mapping_set_item(function->attrs_dict, Value::string(name), value, error);
  }
  if (auto* native = value_as_native_function(object)) {
    if (name == "__isabstractmethod__") {
      error = "native function attribute '" + name + "' is read-only";
      return false;
    }
    if (name == "__doc__" || name == "__name__" || name == "__module__") {
      if (native->attrs_dict != nullptr && native->attrs_dict->tag != ValueTag::Invalid) {
        Value existing;
        std::string ignored;
        if (mapping_get_item(*native->attrs_dict, Value::string(name), existing, ignored)) {
          return mapping_set_item(*native->attrs_dict, Value::string(name), value, error);
        }
      }
      error = "native function attribute '" + name + "' is read-only";
      return false;
    }
    if (native->attrs_dict == nullptr) {
      native->attrs_dict = new Value(Value::dict({}));
    }
    return mapping_set_item(*native->attrs_dict, Value::string(name), value, error);
  }
  if (auto* method = value_as_static_method(object)) {
    if (name == "__isabstractmethod__") {
      error = "staticmethod attribute '" + name + "' is read-only";
      return false;
    }
    if (method->attrs_dict.tag == ValueTag::Invalid) {
      method->attrs_dict = Value::dict({});
    }
    return mapping_set_item(method->attrs_dict, Value::string(name), value, error);
  }
  if (auto* method = value_as_class_method(object)) {
    if (name == "__isabstractmethod__") {
      error = "classmethod attribute '" + name + "' is read-only";
      return false;
    }
    if (method->attrs_dict.tag == ValueTag::Invalid) {
      method->attrs_dict = Value::dict({});
    }
    return mapping_set_item(method->attrs_dict, Value::string(name), value, error);
  }
  if (auto* property = value_as_property(object)) {
    if (name == "__name__") {
      value_assign_fast(property->name, value);
      property->has_name = true;
      property->name_from_getter = false;
      return true;
    }
    if (name == "__doc__") {
      value_assign_fast(property->doc, value);
      property->doc_from_getter = false;
      return true;
    }
    error = "property attribute '" + name + "' is read-only";
    return false;
  }
  if (auto* frame = value_as_frame(object)) {
    if (name == "f_lineno") {
      if (value.tag != ValueTag::Int64) {
        error = "f_lineno must be an integer";
        return false;
      }
      if (frame->module != nullptr && frame->function_id < frame->module->functions.size()) {
        const auto& fn = frame->module->functions[frame->function_id];
        for (size_t i = 0; i < fn.source_lines.size(); ++i) {
          if (static_cast<int64_t>(fn.source_lines[i]) == value.as.i64) {
            frame->instruction_index = static_cast<uint32_t>(i);
            return true;
          }
        }
      }
      error = "line is not in current frame";
      return false;
    }
    if (name == "f_trace" || name == "f_trace_lines" || name == "f_trace_opcodes") {
      return true;
    }
    error = "frame attribute '" + name + "' is read-only";
    return false;
  }
  if (auto* traceback = value_as_traceback(object)) {
    if (name == "tb_next") {
      if (value.tag != ValueTag::None && value_as_traceback(value) == nullptr) {
        error = "tb_next must be a traceback or None";
        return false;
      }
      value_assign_fast(traceback->next, value);
      return true;
    }
    if (name == "tb_lineno") {
      if (value.tag != ValueTag::Int64) {
        error = "tb_lineno must be an integer";
        return false;
      }
      traceback->line = value.as.i64;
      return true;
    }
    error = "traceback attribute '" + name + "' is read-only";
    return false;
  }
  if (auto* instance = value_as_instance(object)) {
    auto* klass = value_as_class(instance->klass);
    if (name == "__class__") {
      auto* new_class = value_as_class(value);
      if (new_class == nullptr) {
        error = "__class__ must be set to a class";
        return false;
      }
      if (klass == nullptr) {
        error = "instance has invalid class";
        return false;
      }
      const bool related_classes = class_is_subclass(new_class, klass) || class_is_subclass(klass, new_class);
      auto is_heap_class = [](const ClassObject* candidate) {
        auto module_it = candidate->attrs.find("__module__");
        auto* module_name = module_it == candidate->attrs.end()
                                ? nullptr
                                : value_as_string(module_it->second);
        return module_name == nullptr || string_object_to_string(*module_name) != "builtins";
      };
      const bool compatible_layout =
          new_class->instance_slot_names == klass->instance_slot_names &&
          new_class->allow_instance_dict == klass->allow_instance_dict &&
          new_class->allow_weakref == klass->allow_weakref;
      if (!related_classes &&
          !(is_heap_class(klass) && is_heap_class(new_class) && compatible_layout)) {
        error = "__class__ assignment: object layout differs";
        return false;
      }
      const bool fixed_layout = klass->restrict_instance_attrs || new_class->restrict_instance_attrs;
      if (fixed_layout) {
        if (!compatible_layout) {
          error = "__class__ assignment: object layout differs";
          return false;
        }
      }
      value_assign_fast(instance->klass, value);
      return true;
    }
    if (instance->native_set_attr != nullptr && instance->native_set_attr(object, name, value, error)) {
      return true;
    }
    if (klass != nullptr) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
        value_assign_fast(instance_slot_at(instance, slot_it->second), value);
        return true;
      }
    }
    for (auto& attr : instance->attrs) {
      if (attr.first == name) {
        value_assign_fast(attr.second, value);
        if (value_as_dict(instance_attribute_storage(*instance)) != nullptr) {
          std::string ignored;
          mapping_set_item(instance_attribute_storage(*instance), Value::string(name), value, ignored);
        }
        if (klass != nullptr && name == "__name__" && class_has_builtin_base_name_impl(klass, "property")) {
          std::string ignored;
          object_set_attr(object, "__xlang3_name_from_getter__", Value::boolean(false), ignored);
        }
        return true;
      }
    }
    if (klass != nullptr && klass->name == "object") {
      error = "object has no attribute '" + name + "'";
      return false;
    }
    if (klass != nullptr && klass->restrict_instance_attrs && !klass->allow_instance_dict) {
      error = "object has no attribute '" + name + "'";
      return false;
    }
    instance->attrs.push_back(std::make_pair(name, value));
    if (value_as_dict(instance_attribute_storage(*instance)) != nullptr) {
      std::string ignored;
      mapping_set_item(instance_attribute_storage(*instance), Value::string(name), value, ignored);
    }
    if (klass != nullptr && name == "__name__" && class_has_builtin_base_name_impl(klass, "property")) {
      std::string ignored;
      object_set_attr(object, "__xlang3_name_from_getter__", Value::boolean(false), ignored);
    }
    return true;
  }
  if (auto* klass = value_as_class(object)) {
    auto immutable_module = klass->attrs.find("__module__");
    auto* immutable_module_name = immutable_module == klass->attrs.end()
        ? nullptr
        : value_as_string(immutable_module->second);
    if (klass->name == "socket" && immutable_module_name != nullptr &&
        string_object_to_string(*immutable_module_name) == "_socket") {
      error = "cannot set '" + name + "' attribute of immutable type '_socket.socket'";
      return false;
    }
    if (name == "__bases__") {
      auto* requested_bases = value_as_tuple(value);
      if (requested_bases == nullptr || requested_bases->items.empty()) {
        error = "can only assign a non-empty tuple to __bases__";
        return false;
      }
      for (const auto& requested_base : requested_bases->items) {
        auto* requested_class = value_as_class(requested_base);
        if (requested_class == nullptr) {
          error = "__bases__ items must be classes";
          return false;
        }
        if (requested_class == klass || class_is_subclass(requested_class, klass)) {
          error = "a __bases__ item causes an inheritance cycle";
          return false;
        }
      }

      const auto previous_bases = klass->bases;
      const Value previous_base = klass->base;
      for (const auto& previous : klass->bases) {
        class_unregister_subclass(value_as_class(previous), klass);
      }
      klass->bases = requested_bases->items;
      value_assign_fast(klass->base, klass->bases.front());
      for (const auto& requested : klass->bases) {
        class_register_subclass(value_as_class(requested), klass);
      }
      invalidate_class_lookup_caches(klass);

      const std::vector<Value>* validated_mro = nullptr;
      if (!class_mro_values(klass, validated_mro, error)) {
        for (const auto& requested : klass->bases) {
          class_unregister_subclass(value_as_class(requested), klass);
        }
        klass->bases = previous_bases;
        value_assign_fast(klass->base, previous_base);
        for (const auto& previous : klass->bases) {
          class_register_subclass(value_as_class(previous), klass);
        }
        invalidate_class_lookup_caches(klass);
        return false;
      }
      return true;
    }
    if (name == "__isabstractmethod__") {
      auto module_it = klass->attrs.find("__module__");
      auto* module_name = module_it != klass->attrs.end() ? value_as_string(module_it->second) : nullptr;
      if (module_name != nullptr && string_object_to_string(*module_name) == "builtins") {
        error = "cannot set '__isabstractmethod__' attribute of immutable type '" + klass->name + "'";
        return false;
      }
    }
    if (name == "__annotate__") {
      klass->attrs.erase("__annotations__");
    }
    if (name == "__module__") {
      klass->attrs.erase("__firstlineno__");
      auto& order = klass->definition_attr_order;
      order.erase(std::remove(order.begin(), order.end(), "__firstlineno__"), order.end());
    }
    if (klass->attrs.find(name) == klass->attrs.end()) {
      klass->definition_attr_order.push_back(name);
    }
    klass->attrs[name] = value;
    if (object_value_is_descriptor(value)) {
      klass->has_descriptors = true;
    }
    update_special_attr_flags(*klass, name);
    ++klass->version;
    return true;
  }
  error = "object does not support attribute assignment";
  return false;
}

bool object_delete_attr(Value& object, const std::string& name, std::string& error) {
  if (auto* cell = value_as_cell(object)) {
    if (name == "cell_contents") {
      value_set_invalid(cell->value);
      return true;
    }
    error = "cell has no attribute '" + name + "'";
    return false;
  }

  if (value_as_module(object) != nullptr) {
    return module_set_attr(object, name, Value::invalid(), error);
  }

  if (auto* function = value_as_function(object)) {
    if (function->attrs_dict.tag != ValueTag::Invalid &&
        mapping_delete_item(function->attrs_dict, Value::string(name), error)) {
      return true;
    }
    error = "function has no attribute '" + name + "'";
    return false;
  }
  if (auto* native = value_as_native_function(object)) {
    if (native->attrs_dict != nullptr &&
        native->attrs_dict->tag != ValueTag::Invalid &&
        mapping_delete_item(*native->attrs_dict, Value::string(name), error)) {
      return true;
    }
    error = "function has no attribute '" + name + "'";
    return false;
  }
  if (auto* method = value_as_static_method(object)) {
    if (method->attrs_dict.tag != ValueTag::Invalid &&
        mapping_delete_item(method->attrs_dict, Value::string(name), error)) {
      return true;
    }
    error = "staticmethod has no attribute '" + name + "'";
    return false;
  }
  if (auto* method = value_as_class_method(object)) {
    if (method->attrs_dict.tag != ValueTag::Invalid &&
        mapping_delete_item(method->attrs_dict, Value::string(name), error)) {
      return true;
    }
    error = "classmethod has no attribute '" + name + "'";
    return false;
  }
  if (auto* property = value_as_property(object)) {
    if (name == "__name__") {
      Value getter_name;
      std::string ignored;
      if (property->fget.tag != ValueTag::None &&
          property->fget.tag != ValueTag::Invalid &&
          object_get_attr(property->fget, "__name__", getter_name, ignored)) {
        value_assign_fast(property->name, getter_name);
        property->has_name = true;
        property->name_from_getter = true;
      } else {
        value_set_invalid(property->name);
        property->has_name = false;
        property->name_from_getter = false;
      }
      return true;
    }
    error = "property has no attribute '" + name + "'";
    return false;
  }
  if (auto* instance = value_as_instance(object)) {
    auto* klass = value_as_class(instance->klass);
    if (instance->native_delete_attr != nullptr && instance->native_delete_attr(object, name, error)) {
      return true;
    }
    if (klass != nullptr) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
        value_set_invalid(instance_slot_at(instance, slot_it->second));
        if (value_as_dict(instance_attribute_storage(*instance)) != nullptr) {
          std::string ignored;
          (void)mapping_delete_item(instance_attribute_storage(*instance), Value::string(name), ignored);
        }
        return true;
      }
    }
    for (auto it = instance->attrs.begin(); it != instance->attrs.end(); ++it) {
      if (it->first == name) {
        instance->attrs.erase(it);
        if (value_as_dict(instance_attribute_storage(*instance)) != nullptr) {
          std::string ignored;
          (void)mapping_delete_item(instance_attribute_storage(*instance), Value::string(name), ignored);
        }
        return true;
      }
    }
    error = "object has no attribute '" + name + "'";
    return false;
  }
  if (auto* klass = value_as_class(object)) {
    auto it = klass->attrs.find(name);
    if (it == klass->attrs.end()) {
      error = "class '" + klass->name + "' has no attribute '" + name + "'";
      return false;
    }
    klass->attrs.erase(it);
    auto& order = klass->definition_attr_order;
    order.erase(std::remove(order.begin(), order.end(), name), order.end());
    ++klass->version;
    return true;
  }
  error = "object does not support attribute deletion";
  return false;
}

bool object_construct(Value klass, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (value_as_class(klass) == nullptr) {
    error = "object is not a class";
    return false;
  }
  if (argc != 0) {
    error = "class construction with arguments requires __init__ dispatch";
    return false;
  }
  out = Value::instance(std::move(klass));
  return true;
}

bool class_set_base(Value klass, Value base, std::string& error) {
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    error = "object is not a class";
    return false;
  }
  if (base.tag != ValueTag::Invalid && value_as_class(base) == nullptr) {
    error = "base object is not a class";
    return false;
  }
  auto* added_base_class = value_as_class(base);
  std::vector<std::string> own_slots;
  for (const auto& attr : klass_obj->attrs) {
    auto* descriptor = value_as_slot_descriptor(attr.second);
    if (descriptor != nullptr && descriptor->owner_name == klass_obj->name) {
      own_slots.push_back(descriptor->name);
    }
  }
  for (const auto& slot : own_slots) {
    klass_obj->instance_slot_names.erase(
        std::remove(
            klass_obj->instance_slot_names.begin(),
            klass_obj->instance_slot_names.end(),
            slot),
        klass_obj->instance_slot_names.end());
  }
  klass_obj->has_explicit_bases = true;
  klass_obj->bases.push_back(base);
  if (auto* base_class = added_base_class) {
    if (!choose_compatible_metaclass(klass_obj->metaclass, base_class->metaclass, error)) {
      return false;
    }
    klass_obj->has_descriptors = klass_obj->has_descriptors || class_or_bases_have_descriptors(base_class);
    inherit_special_attr_flags(*klass_obj, *base_class);
    klass_obj->allow_weakref = klass_obj->allow_weakref || base_class->allow_weakref;
    for (const auto& slot : base_class->instance_slot_names) {
      if (std::find(klass_obj->instance_slot_names.begin(), klass_obj->instance_slot_names.end(), slot) ==
          klass_obj->instance_slot_names.end()) {
        klass_obj->instance_slot_names.push_back(slot);
      }
    }
  }
  for (auto& slot : own_slots) {
    if (std::find(klass_obj->instance_slot_names.begin(), klass_obj->instance_slot_names.end(), slot) ==
        klass_obj->instance_slot_names.end()) {
      klass_obj->instance_slot_names.push_back(slot);
    }
  }
  for (size_t i = 0; i < klass_obj->instance_slot_names.size(); ++i) {
    klass_obj->instance_slot_indices[klass_obj->instance_slot_names[i]] = static_cast<uint32_t>(i);
  }
  for (const auto& slot : own_slots) {
    auto index_it = klass_obj->instance_slot_indices.find(slot);
    if (index_it == klass_obj->instance_slot_indices.end()) {
      continue;
    }
    auto attr_it = klass_obj->attrs.find(slot);
    if (attr_it == klass_obj->attrs.end() || value_as_slot_descriptor(attr_it->second) != nullptr) {
      klass_obj->attrs[slot] = slot_descriptor(klass_obj->name, slot, index_it->second);
      slot_descriptor_set_owner_class(klass_obj->attrs[slot], klass);
      klass_obj->has_descriptors = true;
    }
  }
  if (klass_obj->bases.size() == 1) {
    klass_obj->base = std::move(base);
  }
  update_abc_abstract_methods_for_class(*klass_obj);
  if (!finalize_enum_class(*klass_obj)) {
    error = "enum class finalization failed";
    return false;
  }
  class_register_subclass(added_base_class, klass_obj);
  ++klass_obj->version;
  return true;
}

bool class_get_subclasses(const Value& klass, Value& out, std::string& error) {
  auto* class_object = value_as_class(klass);
  if (class_object == nullptr) {
    error = "__subclasses__() requires a class";
    return false;
  }
  std::vector<Value> subclasses;
  subclasses.reserve(class_object->subclasses.size());
  for (auto* subclass : class_object->subclasses) {
    if (subclass != nullptr) {
      subclasses.push_back(class_value(subclass));
    }
  }
  out = Value::list(std::move(subclasses));
  return true;
}

bool class_is_subclass(const ClassObject* klass, const ClassObject* base) {
  std::vector<const ClassObject*> mro;
  std::string error;
  if (!class_mro_classes(const_cast<ClassObject*>(klass), mro, error)) {
    return false;
  }
  return contains_class(mro, base);
}

bool object_get_class_attr_for_instance(const Value& object, const std::string& name, Value& out, std::string& error) {
  auto* instance = value_as_instance(object);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  if (klass == nullptr) {
    error = "instance has invalid class";
    return false;
  }
  return class_lookup_attr(klass, name, out, error);
}

bool class_get_bound_attr(
    Runtime& runtime,
    const Value& owner_class,
    const Value& instance,
    const std::string& name,
    Value& out,
    std::string& error) {
  auto* class_object = value_as_class(owner_class);
  if (class_object == nullptr || !class_lookup_attr(class_object, name, out, error)) {
    return false;
  }
  if (auto* native = value_as_native_function(out);
      native != nullptr && native->name == "type.__call__") {
    return false;
  }
  if (auto* method = value_as_static_method(out)) {
    value_assign_fast(out, method->function);
    return true;
  }
  if (auto* method = value_as_class_method(out)) {
    Value function;
    value_assign_fast(function, method->function);
    out = Value::bound_method(owner_class, std::move(function));
    return true;
  }
  if (Value function; descriptor_instance_payload(out, "staticmethod", function)) {
    value_assign_fast(out, function);
    return true;
  }
  if (Value function; descriptor_instance_payload(out, "classmethod", function)) {
    out = Value::bound_method(owner_class, std::move(function));
    return true;
  }
  if (value_as_function(out) != nullptr ||
      (value_as_native_function(out) != nullptr && value_as_native_function(out)->bind_as_descriptor)) {
    out = Value::bound_method(instance, out);
    return true;
  }
  if (!object_value_has_descriptor_get(out)) {
    return true;
  }
  Value get_method;
  if (!object_get_attr(out, "__get__", get_method, error)) {
    return false;
  }
  Value descriptor_args[2];
  value_assign_fast(descriptor_args[0], instance);
  value_assign_fast(descriptor_args[1], owner_class);
  Value resolved;
  if (!runtime_call_callable(runtime, get_method, descriptor_args, 2, resolved, error)) {
    return false;
  }
  out = std::move(resolved);
  return true;
}

bool object_get_special_method(
    Runtime& runtime,
    const Value& object,
    const std::string& name,
    Value& out,
    std::string& error) {
  if (auto* instance = value_as_instance(object)) {
    if (value_as_class(instance->klass) != nullptr) {
      if (class_get_bound_attr(runtime, instance->klass, object, name, out, error)) {
        return true;
      }
      if (instance->native_get_attr == nullptr) {
        return false;
      }
      error.clear();
      return object_get_attr(object, name, out, error);
    }
  }
  return object_get_attr(object, name, out, error);
}

bool object_get_class_annotations(Runtime& runtime, const Value& object, Value& out, std::string& error) {
  auto* klass = value_as_class(object);
  if (klass == nullptr) {
    error = "type.__annotations__ getter expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto annotations = klass->attrs.find("__annotations__");
  if (annotations != klass->attrs.end() && value_as_property(annotations->second) == nullptr) {
    value_assign_fast(out, annotations->second);
    return true;
  }
  auto annotate = klass->attrs.find("__annotate__");
  if (annotate != klass->attrs.end() && annotate->second.tag != ValueTag::None &&
      annotate->second.tag != ValueTag::Invalid) {
    const Value format = Value::int64(1);
    if (!runtime_call_callable(runtime, annotate->second, &format, 1, out, error)) {
      return false;
    }
    if (value_as_dict(out) == nullptr) {
      error = "__annotate__ returned a non-dict";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  } else {
    out = Value::dict({});
  }
  value_assign_fast(klass->attrs["__annotations__"], out);
  ++klass->version;
  return true;
}

bool object_get_function_annotations(Runtime& runtime, const Value& object, Value& out, std::string& error) {
  auto* function = value_as_function(object);
  if (function == nullptr) {
    error = "function.__annotations__ getter expected a function";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (function->annotations.tag != ValueTag::Invalid) {
    value_assign_fast(out, function->annotations);
    return true;
  }

  Value annotate;
  std::string ignored;
  if (function->attrs_dict.tag == ValueTag::Invalid ||
      !mapping_get_item(function->attrs_dict, Value::string("__annotate__"), annotate, ignored) ||
      annotate.tag == ValueTag::None) {
    out = Value::dict({});
  } else {
    const Value format = Value::int64(1);
    if (!runtime_call_callable(runtime, annotate, &format, 1, out, error)) {
      return false;
    }
    if (value_as_dict(out) == nullptr) {
      error = "__annotate__ returned a non-dict";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  value_assign_fast(function->annotations, out);
  return true;
}

bool object_lookup_class_attr(const Value& klass, const std::string& name, Value& out, std::string& error) {
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    error = "object is not a class";
    return false;
  }
  return class_lookup_attr(klass_obj, name, out, error);
}

bool object_lookup_inherited_class_attr(
    const Value& klass,
    const std::string& name,
    Value& out,
    std::string& error) {
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    error = "object is not a class";
    return false;
  }
  const std::vector<Value>* mro = nullptr;
  if (!class_mro_values(klass_obj, mro, error)) {
    return false;
  }
  for (size_t i = 1; i < mro->size(); ++i) {
    auto* candidate = value_as_class((*mro)[i]);
    if (candidate == nullptr) {
      error = "invalid class in method resolution order";
      return false;
    }
    auto it = candidate->attrs.find(name);
    if (it != candidate->attrs.end() && it->second.tag != ValueTag::Invalid) {
      value_assign_fast(out, it->second);
      return true;
    }
  }
  return false;
}

bool object_value_has_descriptor_get(const Value& value) {
  if (value_as_slot_descriptor(value) != nullptr) {
    return true;
  }
  return descriptor_lookup_method(value, "__get__");
}

bool object_value_has_descriptor_set(const Value& value) {
  if (value_as_slot_descriptor(value) != nullptr) {
    return true;
  }
  return descriptor_lookup_method(value, "__set__");
}

bool object_value_has_descriptor_delete(const Value& value) {
  if (value_as_slot_descriptor(value) != nullptr) {
    return true;
  }
  return descriptor_lookup_method(value, "__delete__");
}

bool object_value_is_descriptor(const Value& value) {
  return object_value_has_descriptor_get(value) ||
         object_value_has_descriptor_set(value) ||
         object_value_has_descriptor_delete(value);
}

bool object_value_is_data_descriptor(const Value& value) {
  return value_as_property(value) != nullptr ||
         object_value_has_descriptor_set(value) ||
         object_value_has_descriptor_delete(value);
}

bool instance_set_native_data(
    Value instance,
    std::string native_type,
    void* native_data,
    void (*native_data_cleanup)(void*),
    std::string& error) {
  return instance_set_native_owner(std::move(instance), std::move(native_type), native_data,
      native_data, native_data_cleanup, error);
}

bool instance_set_native_owner(Value instance, std::string native_type, void* native_data,
    void* owner, void (*native_data_cleanup)(void*), std::string& error) {
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr) {
    error = "object is not an instance";
    return false;
  }
  if (instance_obj->native_data_cleanup != nullptr && instance_obj->native_data != nullptr) {
    instance_obj->native_data_cleanup(instance_obj->native_owner);
  }
  instance_obj->native_type = std::move(native_type);
  instance_obj->native_data = native_data;
  instance_obj->native_data_cast = nullptr;
  instance_obj->native_owner = owner;
  instance_obj->native_data_cleanup = native_data_cleanup;
  instance_obj->native_data_truthy = nullptr;
  instance_obj->native_get_attr = nullptr;
  instance_obj->native_set_attr = nullptr;
  instance_obj->native_delete_attr = nullptr;
  return true;
}

void* instance_get_native_data(const Value& instance, const std::string& native_type) {
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr) {
    return nullptr;
  }
  if (instance_obj->native_type == native_type) return instance_obj->native_data;
  return instance_obj->native_data_cast
      ? instance_obj->native_data_cast(instance_obj->native_data, native_type.c_str()) : nullptr;
}

bool instance_set_native_truthy(Value instance, bool (*truthy)(const void*), std::string& error) {
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr) {
    error = "object is not an instance";
    return false;
  }
  instance_obj->native_data_truthy = truthy;
  return true;
}

bool instance_native_truthy(const Value& instance, bool& out) {
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr || instance_obj->native_data == nullptr || instance_obj->native_data_truthy == nullptr) {
    return false;
  }
  out = instance_obj->native_data_truthy(instance_obj->native_data);
  return true;
}

bool runtime_instance_truthy(Runtime& runtime, const Value& value, bool& out, std::string& error) {
  if (instance_native_truthy(value, out)) return true;
  Value hook;
  std::string ignored;
  const bool has_bool = object_get_class_attr_for_instance(value, "__bool__", hook, ignored);
  if (!has_bool && !object_get_class_attr_for_instance(value, "__len__", hook, ignored)) {
    out = true;
    return true;
  }
  Value result;
  error.clear();
  if (!runtime_call_callable(runtime, hook, &value, 1, result, error)) return false;
  if (has_bool && result.tag == ValueTag::Bool) { out = result.as.b; return true; }
  if (!has_bool && (result.tag == ValueTag::Int64 || result.tag == ValueTag::Bool)) {
    const int64_t length = result.tag == ValueTag::Bool ? result.as.b : result.as.i64;
    if (length >= 0) { out = length != 0; return true; }
    error = "__len__() should return >= 0";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  error = has_bool ? "__bool__ should return bool" : "__len__ should return an integer";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool instance_set_native_attr_hooks(
    Value instance,
    NativeInstanceGetAttr get_attr,
    NativeInstanceSetAttr set_attr,
    NativeInstanceDeleteAttr delete_attr,
    std::string& error) {
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr) {
    error = "object is not an instance";
    return false;
  }
  instance_obj->native_get_attr = get_attr;
  instance_obj->native_set_attr = set_attr;
  instance_obj->native_delete_attr = delete_attr;
  return true;
}

} // namespace xlang3
