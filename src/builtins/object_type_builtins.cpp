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
#include "xlang3/builtins.h"

#include "xlang3/attribute.h"
#include "xlang3/builtin_methods.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/generator.h"
#include "xlang3/interpreter.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

#include <cstdint>
#include <algorithm>
#include <exception>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

Value make_dict_fromkeys_classmethod();
bool choose_compatible_metaclass(Value& current, const Value& candidate, std::string& error);

namespace {

bool resolve_class_bases(Runtime& runtime, TupleObject* bases, std::vector<Value>& resolved_bases, std::string& error);

bool class_attrs_have(const std::vector<std::pair<std::string, Value>>& attrs, const std::string& name) {
  for (const auto& attr : attrs) {
    if (attr.first == name) {
      return true;
    }
  }
  return false;
}

bool call_set_name_descriptors(
    Runtime& runtime,
    const Value& cls,
    const std::vector<std::pair<std::string, Value>>& attrs,
    std::string& error) {
  for (const auto& attr : attrs) {
    Value set_name;
    std::string attr_error;
    if (!attribute_get(attr.second, "__set_name__", set_name, attr_error)) {
      continue;
    }
    Value name_arg = Value::string(attr.first);
    Value call_args[] = {cls, name_arg};
    Value ignored;
    if (!runtime_call_callable(runtime, set_name, call_args, 2, ignored, error)) {
      if (error.empty()) {
        error = "Error calling __set_name__";
      }
      return false;
    }
  }
  return true;
}

bool call_init_subclass(
    Runtime& runtime,
    const Value& cls,
    TupleObject* bases,
    const std::vector<std::pair<std::string, Value>>& kwargs,
    std::string& error) {
  if (bases == nullptr || bases->items.empty()) {
    return true;
  }
  Value hook;
  if (!object_lookup_inherited_class_attr(cls, "__init_subclass__", hook, error)) {
    error.clear();
    return true;
  }
  Value ignored;
  if (auto* method = value_as_class_method(hook)) {
    Value call_args[] = {cls};
    return runtime_call_callable_kw(runtime, method->function, call_args, 1, kwargs, ignored, error);
  }
  if (auto* static_method = value_as_static_method(hook)) {
    return runtime_call_callable_kw(runtime, static_method->function, nullptr, 0, kwargs, ignored, error);
  }
  if (value_as_function(hook) != nullptr || value_as_native_function(hook) != nullptr) {
    Value call_args[] = {cls};
    return runtime_call_callable_kw(runtime, hook, call_args, 1, kwargs, ignored, error);
  }
  return runtime_call_callable_kw(runtime, hook, nullptr, 0, kwargs, ignored, error);
}

bool call_init_subclass(Runtime& runtime, const Value& cls, TupleObject* bases, std::string& error) {
  static const std::vector<std::pair<std::string, Value>> empty_kwargs;
  return call_init_subclass(runtime, cls, bases, empty_kwargs, error);
}

std::string current_module_name(Runtime& runtime) {
  Value name_value;
  std::string ignored;
  if (module_get_attr(runtime.current_globals_module(), "__name__", name_value, ignored)) {
    if (auto* name = value_as_string(name_value)) {
      return string_object_to_string(*name);
    }
  }
  return "__main__";
}

std::string builtin_value_type_name(Runtime& runtime, const Value& value) {
  Value type;
  if (runtime_type_of_value(runtime, value, type)) {
    if (auto* klass = value_as_class(type)) {
      return klass->name;
    }
  }
  return value_to_string(value);
}

bool dict_init_update_one(Runtime& runtime, Value& target, const Value& source, std::string& error) {
  if (auto* source_dict = value_as_dict(source)) {
    for (const auto& entry : source_dict->entries) {
      if (!mapping_set_item(target, entry.first, entry.second, error)) {
        return false;
      }
    }
    return true;
  }
  if (auto* source_instance = value_as_instance(source)) {
    if (auto* storage = value_as_dict(source_instance->mapping_storage)) {
      for (const auto& entry : storage->entries) {
        if (!mapping_set_item(target, entry.first, entry.second, error)) {
          return false;
        }
      }
      return true;
    }
  }

  Value keys_method;
  std::string attr_error;
  if (object_get_attr(source, "keys", keys_method, attr_error)) {
    Value keys_result;
    if (!runtime_call_callable(runtime, keys_method, nullptr, 0, keys_result, error)) {
      return false;
    }
    Value getitem_method;
    if (!object_get_attr(source, "__getitem__", getitem_method, attr_error)) {
      error = "'" + builtin_value_type_name(runtime, source) + "' object is not a mapping";
      return false;
    }
    std::vector<Value> keys;
    if (!runtime_collect_iterable(runtime, keys_result, keys, error)) {
      return false;
    }
    for (const auto& key : keys) {
      Value value;
      if (!runtime_call_callable(runtime, getitem_method, &key, 1, value, error) ||
          !mapping_set_item(target, key, value, error)) {
        return false;
      }
    }
    return true;
  }

  Value iterator;
  if (!runtime_get_iter(runtime, source, iterator, error)) {
    if (error == "object is not iterable") {
      error = "'" + builtin_value_type_name(runtime, source) + "' object is not iterable";
    }
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    const TupleObject* tuple = value_as_tuple(item);
    const ListObject* list = value_as_list(item);
    const Value* key = nullptr;
    const Value* value = nullptr;
    if (tuple != nullptr && tuple->items.size() == 2) {
      key = &tuple->items[0];
      value = &tuple->items[1];
    } else if (list != nullptr && list->items.size() == 2) {
      key = &list->items[0];
      value = &list->items[1];
    } else {
      error = "dict update sequence element has length other than 2";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (!mapping_set_item(target, *key, *value, error)) {
      return false;
    }
  }
  return true;
}

bool builtin_dict_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "descriptor '__init__' of 'dict' object needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 2) {
    error = "dict expected at most 1 argument, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target;
  if (auto* dict = value_as_dict(args[0])) {
    dict->entries.clear();
    target = args[0];
  } else if (auto* instance = value_as_instance(args[0])) {
    if (auto* storage = value_as_dict(instance->mapping_storage)) {
      storage->entries.clear();
      target = instance->mapping_storage;
    }
  }
  if (target.tag == ValueTag::Invalid) {
    error = "descriptor '__init__' requires a 'dict' object but received a '" + builtin_value_type_name(runtime, args[0]) + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 2 && !dict_init_update_one(runtime, target, args[1], error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (!mapping_set_item(target, Value::string(kwargs[i].name), *kwargs[i].value, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool builtin_dict_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return builtin_dict_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool builtin_list_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "list expected at most 1 argument, got " + std::to_string(argc > 0 ? argc - 1 : 0);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* target = value_as_mutable_list_storage(args[0]);
  if (target == nullptr) {
    error = "descriptor '__init__' requires a 'list' object but received a '" +
            builtin_value_type_name(runtime, args[0]) + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> items;
  if (argc == 2 && !runtime_collect_iterable(runtime, args[1], items, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  target->items = std::move(items);
  value_set_none(out);
  return true;
}

bool collect_type_new_slots(const Value& value, std::vector<std::string>& slots) {
  if (auto* string = value_as_string(value)) {
    const auto name = string_object_to_string(*string);
    if (name != "__dict__" && name != "__weakref__" &&
        std::find(slots.begin(), slots.end(), name) == slots.end()) {
      slots.push_back(name);
    }
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      if (!collect_type_new_slots(item, slots)) {
        return false;
      }
    }
    return true;
  }
  if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      if (!collect_type_new_slots(item, slots)) {
        return false;
      }
    }
    return true;
  }
  if (auto* set = value_as_set(value)) {
    for (const auto& item : set->items) {
      if (!collect_type_new_slots(item, slots)) {
        return false;
      }
    }
    return true;
  }
  if (auto* dict = value_as_dict(value)) {
    for (const auto& entry : dict->entries) {
      if (!collect_type_new_slots(entry.first, slots)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

DictObject* type_new_namespace_dict(const Value& value) {
  if (auto* dict = value_as_dict(value)) {
    return dict;
  }
  if (auto* instance = value_as_instance(value)) {
    return value_as_dict(instance->mapping_storage);
  }
  return nullptr;
}

bool value_has_abstract_marker(const Value& value) {
  Value marker;
  std::string ignored;
  return attribute_get(value, "__isabstractmethod__", marker, ignored) && value_truthy(marker);
}

bool metaclass_is_abc_meta(const Value& value) {
  auto* klass = value_as_class(value);
  if (klass == nullptr) {
    return false;
  }
  return klass->name == "ABCMeta" || class_has_builtin_base_name(klass, "ABCMeta");
}

void add_abstract_name(std::vector<Value>& names, const std::string& name) {
  for (const auto& item : names) {
    auto* string = value_as_string(item);
    if (string != nullptr && string_object_to_string(*string) == name) {
      return;
    }
  }
  names.push_back(Value::string(name));
}

void collect_abstract_names_from_iterable(const Value& value, std::vector<std::string>& names) {
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

Value abc_abstract_methods_for_type_new(TupleObject* bases, DictObject* namespace_dict) {
  std::vector<Value> abstracts;
  std::vector<std::string> inherited_names;
  for (const auto& base : bases->items) {
    Value base_abstracts;
    std::string ignored;
    if (object_get_attr(base, "__abstractmethods__", base_abstracts, ignored)) {
      collect_abstract_names_from_iterable(base_abstracts, inherited_names);
    }
  }
  for (const auto& name : inherited_names) {
    Value override_value;
    bool has_override = false;
    for (const auto& entry : namespace_dict->entries) {
      auto* key = value_as_string(entry.first);
      if (key != nullptr && string_object_to_string(*key) == name) {
        value_assign_fast(override_value, entry.second);
        has_override = true;
        break;
      }
    }
    if (!has_override || value_has_abstract_marker(override_value)) {
      add_abstract_name(abstracts, name);
    }
  }
  for (const auto& entry : namespace_dict->entries) {
    auto* key = value_as_string(entry.first);
    if (key != nullptr && value_has_abstract_marker(entry.second)) {
      add_abstract_name(abstracts, string_object_to_string(*key));
    }
  }
  return Value::frozenset(std::move(abstracts));
}

const char* builtin_type_name_for_kind(ObjectKind kind) {
  switch (kind) {
    case ObjectKind::String:
      return "str";
    case ObjectKind::BigInt:
      return "int";
    case ObjectKind::Complex:
      return "complex";
    case ObjectKind::Bytes:
      return "bytes";
    case ObjectKind::ByteArray:
      return "bytearray";
    case ObjectKind::MemoryView:
      return "memoryview";
    case ObjectKind::Slice:
      return "slice";
    case ObjectKind::Tuple:
      return "tuple";
    case ObjectKind::List:
      return "list";
    case ObjectKind::Dict:
      return "dict";
    case ObjectKind::MappingProxy:
      return "mappingproxy";
    case ObjectKind::DictKeysView:
      return "dict_keys";
    case ObjectKind::DictValuesView:
      return "dict_values";
    case ObjectKind::DictItemsView:
      return "dict_items";
    case ObjectKind::Set:
      return "set";
    case ObjectKind::Range:
      return "range";
    case ObjectKind::RangeIterator:
    case ObjectKind::SequenceIterator:
    case ObjectKind::DictIterator:
    case ObjectKind::SetIterator:
      return "iterator";
    case ObjectKind::EnumerateIterator:
      return "enumerate";
    case ObjectKind::ZipIterator:
      return "iterator";
    case ObjectKind::MapIterator:
      return "map";
    case ObjectKind::FilterIterator:
      return "filter";
    case ObjectKind::CallableIterator:
      return "callable_iterator";
    case ObjectKind::ChainIterator:
      return "chain";
    case ObjectKind::ProtocolIterator:
      return "iterator";
    case ObjectKind::Generator:
      return "generator";
    case ObjectKind::AsyncGeneratorAwaitable:
      return "async_generator_awaitable";
    case ObjectKind::Module:
      return "module";
    case ObjectKind::Function:
      return "function";
    case ObjectKind::NativeFunction:
      return "builtin_function_or_method";
    case ObjectKind::Code:
      return "code";
    case ObjectKind::Frame:
      return "frame";
    case ObjectKind::Traceback:
      return "traceback";
    case ObjectKind::Class:
      return "type";
    case ObjectKind::Instance:
      return nullptr;
    case ObjectKind::BoundMethod:
      return "method";
    case ObjectKind::StaticMethod:
      return "staticmethod";
    case ObjectKind::ClassMethod:
      return "classmethod";
    case ObjectKind::Super:
      return "super";
    case ObjectKind::SlotDescriptor:
      return "member_descriptor";
    case ObjectKind::Property:
      return "property";
    case ObjectKind::Cell:
      return "cell";
    case ObjectKind::File:
      return "file";
    case ObjectKind::GenericAlias:
      return "GenericAlias";
    case ObjectKind::TypeParam:
      return "type_parameter";
  }
  return "object";
}

const Value* find_builtin_type(Runtime& runtime, const char* name) {
  return name == nullptr ? nullptr : runtime.find_builtin(name);
}

bool call_class_check_hook(
    Runtime& runtime,
    const Value& classinfo,
    const char* hook_name,
    const Value& hook_arg,
    bool& applied,
    bool& out,
    std::string& error) {
  applied = false;
  Value hook;
  std::string hook_error;
  const Value* hook_owner = &classinfo;
  if (auto* klass = value_as_class(classinfo)) {
    hook_owner = &klass->metaclass;
  }
  if (!object_get_attr(*hook_owner, hook_name, hook, hook_error)) {
    return true;
  }

  Value function_value;
  std::vector<Value> leading_args;
  if (auto* bound = value_as_bound_method(hook)) {
    value_assign_fast(function_value, bound->function);
    leading_args.push_back(bound->self);
  } else {
    value_assign_fast(function_value, hook);
    auto* native = value_as_native_function(function_value);
    if (value_as_function(function_value) != nullptr || (native != nullptr && native->bind_as_descriptor)) {
      leading_args.push_back(classinfo);
    }
  }
  leading_args.push_back(hook_arg);

  Value result;
  if (auto* native = value_as_native_function(function_value)) {
    if (native->callback == nullptr ||
        !native->callback(
            runtime,
            leading_args.empty() ? nullptr : leading_args.data(),
            static_cast<uint32_t>(leading_args.size()),
            result,
            error,
            native->user_data)) {
      if (error.empty()) {
        error = std::string(hook_name) + " failed";
      }
      return false;
    }
  } else if (auto* function = value_as_function(function_value)) {
    CallArgsView args;
    args.leading = leading_args.empty() ? nullptr : leading_args.data();
    args.leading_count = static_cast<uint32_t>(leading_args.size());
    Interpreter interpreter(runtime);
    RuntimeResult call_result = interpreter.run_function_value(function, args);
    if (!call_result.errors.empty()) {
      error = call_result.errors.front();
      return false;
    }
    value_assign_fast(result, call_result.value);
  } else {
    return true;
  }

  applied = true;
  out = value_truthy(result);
  return true;
}

bool class_tuple_matches(
    Runtime& runtime,
    const Value& check_subject,
    const Value& actual_type,
    const Value& classinfo,
    bool subclass_check,
    bool& out,
    std::string& error) {
  if (auto* tuple = value_as_tuple(classinfo)) {
    for (const auto& item : tuple->items) {
      bool item_match = false;
      if (!class_tuple_matches(runtime, check_subject, actual_type, item, subclass_check, item_match, error)) {
        return false;
      }
      if (item_match) {
        out = true;
        return true;
      }
    }
    out = false;
    return true;
  }

  auto* expected = value_as_class(classinfo);
  auto* actual = value_as_class(actual_type);
  if (expected == nullptr || actual == nullptr) {
    error = subclass_check ? "issubclass() arg 2 must be a class or tuple of classes"
                           : "isinstance() arg 2 must be a class or tuple of classes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  if (!subclass_check && expected->name == "Iterator" &&
      value_is_functional_iterator(check_subject)) {
    out = true;
    return true;
  }
  if (!subclass_check && check_subject.tag == ValueTag::Object &&
      check_subject.as.obj != nullptr && check_subject.as.obj->kind == ObjectKind::File) {
    const auto* file = reinterpret_cast<const FileObject*>(check_subject.as.obj);
    const std::string& name = expected->name;
    if (name == "IOBase" || name == "_IOBase" ||
        ((name == "TextIOBase" || name == "_TextIOBase") && !file->binary) ||
        ((name == "RawIOBase" || name == "_RawIOBase" || name == "FileIO") && file->binary && file->buffering == 0) ||
        ((name == "BufferedIOBase" || name == "_BufferedIOBase") && file->binary && file->buffering != 0)) {
      out = true;
      return true;
    }
  }

  const char* hook_name = subclass_check ? "__subclasscheck__" : "__instancecheck__";
  const Value& hook_arg = subclass_check ? actual_type : check_subject;
  bool hook_applied = false;
  if (!call_class_check_hook(runtime, classinfo, hook_name, hook_arg, hook_applied, out, error)) {
    return false;
  }
  if (hook_applied) {
    return true;
  }
  out = class_is_subclass(actual, expected);
  return true;
}

bool class_tuple_matches_reported_type(
    Runtime& runtime,
    const Value& actual_type,
    const Value& classinfo,
    bool& out,
    std::string& error) {
  if (auto* tuple = value_as_tuple(classinfo)) {
    for (const auto& item : tuple->items) {
      bool item_match = false;
      if (!class_tuple_matches_reported_type(runtime, actual_type, item, item_match, error)) {
        return false;
      }
      if (item_match) {
        out = true;
        return true;
      }
    }
    out = false;
    return true;
  }

  auto* expected = value_as_class(classinfo);
  auto* actual = value_as_class(actual_type);
  if (expected == nullptr || actual == nullptr) {
    error = "isinstance() arg 2 must be a class or tuple of classes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = class_is_subclass(actual, expected);
  return true;
}

bool builtin_id(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "id() expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  uint64_t id = static_cast<uint64_t>(args[0].tag);
  switch (args[0].tag) {
    case ValueTag::Bool:
      id = (id << 32u) ^ (args[0].as.b ? 1u : 0u);
      break;
    case ValueTag::Int64:
      id = (id << 32u) ^ static_cast<uint64_t>(args[0].as.i64);
      break;
    case ValueTag::Double:
      id = (id << 32u) ^ static_cast<uint64_t>(args[0].as.f64);
      break;
    case ValueTag::Object:
      id = reinterpret_cast<uintptr_t>(args[0].as.obj);
      break;
    case ValueTag::None:
    case ValueTag::Invalid:
      break;
  }
  out = Value::int64(static_cast<int64_t>(id & 0x7fffffffffffffffULL));
  return true;
}

bool builtin_isinstance(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "isinstance() expected 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value actual_type;
  if (!runtime_type_of_value(runtime, args[0], actual_type)) {
    error = "isinstance() could not resolve object type";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool result = false;
  if (!class_tuple_matches(runtime, args[0], actual_type, args[1], false, result, error)) {
    return false;
  }
  if (!result) {
    if (auto* instance = value_as_instance(args[0])) {
      if (auto* klass = value_as_class(instance->klass)) {
        Value class_descriptor;
        std::string descriptor_error;
        if (object_get_class_attr_for_instance(args[0], "__class__", class_descriptor, descriptor_error)) {
          if (auto* property = value_as_property(class_descriptor);
              property != nullptr && property->fget.tag != ValueTag::None && property->fget.tag != ValueTag::Invalid) {
            Value reported_type;
            if (!runtime_call_callable(runtime, property->fget, &args[0], 1, reported_type, error)) {
              return false;
            }
            if (value_as_class(reported_type) != nullptr &&
                !class_tuple_matches_reported_type(runtime, reported_type, args[1], result, error)) {
              return false;
            }
          }
        }
      }
    }
  }
  if (!result) {
    Value reported_type;
    std::string reported_error;
    if (object_get_attr(args[0], "_spec_class", reported_type, reported_error) &&
        value_as_class(reported_type) != nullptr &&
        !class_tuple_matches_reported_type(runtime, reported_type, args[1], result, error)) {
      return false;
    }
  }
  out = Value::boolean(result);
  return true;
}

bool builtin_issubclass(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "issubclass() expected 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "issubclass() arg 1 must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool result = false;
  if (!class_tuple_matches(runtime, args[0], args[0], args[1], true, result, error)) {
    return false;
  }
  out = Value::boolean(result);
  return true;
}

bool builtin_object_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "object.__new__ expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "object.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::instance(args[0]);
  return true;
}

bool builtin_module_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    error = "module.__new__ expected name and optional doc";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "module.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "module name must be a string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::module(string_object_to_string(*name));
  std::string ignored;
  module_set_attr(out, "__doc__", argc == 3 ? args[2] : Value::none(), ignored);
  return true;
}

bool builtin_module_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    error = "module.__init__ expected name and optional doc";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_module(args[0]) == nullptr) {
    error = "module.__init__ self must be a module";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "module name must be a string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  std::string ignored;
  module_set_attr(self, "__name__", args[1], ignored);
  module_set_attr(self, "__doc__", argc == 3 ? args[2] : Value::none(), ignored);
  value_set_none(out);
  return true;
}

bool builtin_method_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 3) {
    error = "method.__new__ expected function and instance";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "method.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::bound_method(args[2], args[1]);
  return true;
}

bool builtin_object_format(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "object.__format__ expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* format_spec = value_as_string(args[1]);
  if (format_spec == nullptr) {
    error = "object.__format__ argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!string_object_to_string(*format_spec).empty()) {
    error = "unsupported format string passed to object.__format__";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return builtin_str_from_value(runtime, args[0], out, error);
}

bool builtin_object_reduce(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "object.__reduce__() takes no arguments (" + std::to_string(argc > 0 ? argc - 1 : 0) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value object_type;
  if (!runtime_type_of_value(runtime, args[0], object_type)) {
    error = "object.__reduce__ could not resolve object type";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value copyreg;
  if (!runtime.import_module("copyreg", copyreg, error)) {
    return false;
  }
  Value newobj;
  std::vector<Value> constructor_args;
  constructor_args.push_back(object_type);
  Value getnewargs_ex;
  std::string getnewargs_ex_error;
  if (object_get_attr(args[0], "__getnewargs_ex__", getnewargs_ex, getnewargs_ex_error)) {
    Value supplied;
    if (!runtime_call_callable(runtime, getnewargs_ex, nullptr, 0, supplied, error)) {
      return false;
    }
    const auto* supplied_pair = value_as_tuple(supplied);
    if (supplied_pair == nullptr || supplied_pair->items.size() != 2 ||
        value_as_tuple(supplied_pair->items[0]) == nullptr ||
        value_as_dict(supplied_pair->items[1]) == nullptr) {
      error = "__getnewargs_ex__ should return a tuple of (args, kwargs)";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (!module_get_attr(copyreg, "__newobj_ex__", newobj, error)) return false;
    constructor_args.push_back(supplied_pair->items[0]);
    constructor_args.push_back(supplied_pair->items[1]);
  } else {
    if (!module_get_attr(copyreg, "__newobj__", newobj, error)) return false;
    Value getnewargs;
    std::string getnewargs_error;
    if (object_get_attr(args[0], "__getnewargs__", getnewargs, getnewargs_error)) {
      Value supplied_args;
      if (!runtime_call_callable(runtime, getnewargs, nullptr, 0, supplied_args, error)) {
        return false;
      }
      const auto* supplied_tuple = value_as_tuple(supplied_args);
      if (supplied_tuple == nullptr) {
        error = "__getnewargs__ should return a tuple, not '" +
            builtin_value_type_name(runtime, supplied_args) + "'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      constructor_args.insert(
          constructor_args.end(), supplied_tuple->items.begin(), supplied_tuple->items.end());
    }
  }

  Value state = Value::none();
  Value getstate;
  std::string attr_error;
  if (object_get_attr(args[0], "__getstate__", getstate, attr_error)) {
    if (!runtime_call_callable(runtime, getstate, nullptr, 0, state, error)) {
      return false;
    }
  } else if (auto* instance = value_as_instance(args[0])) {
    Value dict_state = Value::none();
    std::string dict_error;
    const bool has_dict_state = object_get_attr(args[0], "__dict__", dict_state, dict_error);
    std::vector<std::pair<Value, Value>> slot_entries;
    if (auto* klass = value_as_class(instance->klass)) {
      for (size_t i = 0; i < klass->instance_slot_names.size() && i < instance_slot_count(instance); ++i) {
        const auto& slot_name = klass->instance_slot_names[i];
        const auto& slot_value = instance_slot_at(instance, static_cast<uint32_t>(i));
        if (slot_name != "__dict__" && slot_name != "__weakref__" && slot_value.tag != ValueTag::Invalid) {
          slot_entries.emplace_back(Value::string(slot_name), slot_value);
        }
      }
    }
    if (!slot_entries.empty()) {
      state = Value::tuple({has_dict_state ? dict_state : Value::none(), Value::dict(std::move(slot_entries))});
    } else if (has_dict_state) {
      value_assign_fast(state, dict_state);
    }
  }

  out = Value::tuple({newobj, Value::tuple(std::move(constructor_args)), std::move(state)});
  return true;
}

bool builtin_traceback_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 5 || value_as_class(args[0]) == nullptr ||
      (args[1].tag != ValueTag::None && value_as_traceback(args[1]) == nullptr) ||
      value_as_frame(args[2]) == nullptr || args[3].tag != ValueTag::Int64 ||
      args[4].tag != ValueTag::Int64) {
    error = "traceback() expected (tb_next, frame, lasti, lineno)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::traceback(args[2], args[1], args[4].as.i64, args[3].as.i64);
  return true;
}

bool builtin_traceback_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 5 || value_as_traceback(args[0]) == nullptr) {
    error = "traceback.__init__ expected (tb_next, frame, lasti, lineno)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_object_reduce_ex(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "object.__reduce_ex__() takes exactly one argument (" + std::to_string(argc > 0 ? argc - 1 : 0) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (auto* instance = value_as_instance(args[0])) {
    Value reduce_attr;
    std::string lookup_error;
    const bool has_reduce = object_lookup_class_attr(
        instance->klass, "__reduce__", reduce_attr, lookup_error);
    const auto* native_reduce = value_as_native_function(reduce_attr);
    if (has_reduce && (native_reduce == nullptr || native_reduce->name != "object.__reduce__")) {
      Value reduce_method;
      if (!object_get_attr(args[0], "__reduce__", reduce_method, error)) {
        return false;
      }
      return runtime_call_callable(runtime, reduce_method, nullptr, 0, out, error);
    }
  }
  int64_t protocol = 0;
  if (value_int_like_to_i64(args[1], protocol) && protocol < 2) {
    Value copyreg;
    Value reduce_ex;
    if (!runtime.import_module("copyreg", copyreg, error) ||
        !module_get_attr(copyreg, "_reduce_ex", reduce_ex, error)) {
      return false;
    }
    Value reduce_args[2] = {args[0], args[1]};
    return runtime_call_callable(runtime, reduce_ex, reduce_args, 2, out, error);
  }
  return builtin_object_reduce(runtime, args, 1, out, error, nullptr);
}

bool builtin_object_getstate(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  if (argc != 1) {
    error = "object.__getstate__() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value dict_state;
  std::string ignored;
  if (object_get_attr(args[0], "__dict__", dict_state, ignored)) {
    value_assign_fast(out, dict_state);
  } else {
    value_set_none(out);
  }
  return true;
}

bool builtin_singleton_reduce(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 1 || user_data == nullptr) {
    error = "singleton __reduce__() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::string(static_cast<const char*>(user_data));
  return true;
}

bool builtin_object_subclasses(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "__subclasses__() takes no arguments (" + std::to_string(argc > 0 ? argc - 1 : 0) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!class_get_subclasses(args[0], out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool builtin_str_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "str.__new__ expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* klass = value_as_class(args[0]);
  if (klass == nullptr) {
    error = "str.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 2) {
    if (argc > 4) {
      error = "str.__new__ expected at most value, encoding, and errors";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (value_as_bytes(args[1]) == nullptr && value_as_bytearray(args[1]) == nullptr) {
      error = "str.__new__ encoding without a bytes-like object";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    auto* encoding_string = value_as_string(args[2]);
    if (encoding_string == nullptr) {
      error = "str.__new__ encoding must be a string";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (argc == 4 && value_as_string(args[3]) == nullptr) {
      error = "str.__new__ errors must be a string";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    Value decode;
    if (!object_get_attr(args[1], "decode", decode, error)) return false;
    Value decode_args[2] = {args[2], argc == 4 ? args[3] : Value::string("strict")};
    Value decoded;
    if (!runtime_call_callable(runtime, decode, decode_args, 2, decoded, error)) return false;
    if (value_as_string(decoded) == nullptr) {
      error = "decoder returned a non-string result";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const Value* builtin_str = runtime.find_builtin("str");
    if (builtin_str != nullptr && value_as_class(*builtin_str) == klass) {
      out = std::move(decoded);
      return true;
    }
    out = Value::instance(args[0]);
    std::string ignored;
    (void)object_set_attr(out, "__xlang3_string_value__", decoded, ignored);
    return true;
  }
  Value text = Value::string("");
  if (argc == 2 && !builtin_str_from_value(runtime, args[1], text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* builtin_str = runtime.find_builtin("str");
  if (builtin_str != nullptr && value_as_class(*builtin_str) == klass) {
    value_assign_fast(out, text);
    return true;
  }
  out = Value::instance(args[0]);
  std::string ignored;
  (void)object_set_attr(out, "__xlang3_string_value__", text, ignored);
  return true;
}

bool builtin_str_getnewargs(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "str.__getnewargs__() takes no arguments (" +
        std::to_string(argc > 0 ? argc - 1 : 0) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value text;
  if (value_as_string(args[0]) != nullptr) {
    value_assign_fast(text, args[0]);
  } else {
    std::string ignored;
    if (!object_get_attr(args[0], "__xlang3_string_value__", text, ignored) ||
        value_as_string(text) == nullptr) {
      error = "descriptor '__getnewargs__' requires a 'str' object";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  out = Value::tuple({std::move(text)});
  return true;
}

bool builtin_tuple_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3) {
    error = "tuple.__new__ expected a class, optional iterable, and optional named-field dict";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* klass = value_as_class(args[0]);
  if (klass == nullptr) {
    error = "tuple.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  std::vector<Value> items;
  if (argc >= 2) {
    if (auto* tuple = value_as_tuple(args[1])) {
      items = tuple->items;
    } else if (!runtime_collect_iterable(runtime, args[1], items, error)) {
      return false;
    }
  }
  if (argc == 3 && value_as_dict(args[2]) == nullptr) {
    error = "tuple.__new__ named-field payload must be a dict";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  Value tuple_storage = Value::tuple(std::move(items));
  const Value* builtin_tuple = runtime.find_builtin("tuple");
  if (builtin_tuple != nullptr && builtin_tuple->tag == args[0].tag && builtin_tuple->as.obj == args[0].as.obj) {
    value_assign_fast(out, tuple_storage);
    return true;
  }

  out = Value::instance(args[0]);
  auto* instance = value_as_instance(out);
  if (instance == nullptr) {
    error = "tuple.__new__ could not allocate tuple subclass";
    return false;
  }
  instance->attrs.push_back({"_tuple", Value::invalid()});
  value_assign_fast(instance->attrs.back().second, tuple_storage);
  return true;
}

bool try_int_conversion_protocol(Runtime& runtime, const Value& value, Value& parsed, std::string& error) {
  Value convert_method;
  Value converted;
  std::string call_error;
  if (!(object_get_attr(value, "__int__", convert_method, call_error) ||
        object_get_attr(value, "__index__", convert_method, call_error))) {
    return false;
  }
  if (!runtime_call_callable(runtime, convert_method, nullptr, 0, converted, call_error)) {
    error = call_error;
    return false;
  }
  if (converted.tag == ValueTag::Int64 || value_as_bigint(converted) != nullptr) {
    value_assign_fast(parsed, converted);
    return true;
  }
  if (converted.tag == ValueTag::Bool) {
    parsed = Value::int64(converted.as.b ? 1 : 0);
    return true;
  }
  Value stored;
  std::string ignored;
  if ((object_get_attr(converted, "__xlang3_int_value__", stored, ignored) ||
       object_get_attr(converted, "_value_", stored, ignored)) &&
      (stored.tag == ValueTag::Int64 || value_as_bigint(stored) != nullptr)) {
    value_assign_fast(parsed, stored);
    return true;
  }
  error = "__int__ returned non-int";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool builtin_complex_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3 || value_as_class(args[0]) == nullptr) {
    error = "complex.__new__ expected a type and up to two numeric arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto numeric_part = [&](const Value& value, double& real, double& imag) {
    if (value.tag == ValueTag::Bool) real = value.as.b ? 1.0 : 0.0;
    else if (value.tag == ValueTag::Int64) real = static_cast<double>(value.as.i64);
    else if (value.tag == ValueTag::Double) real = value.as.f64;
    else if (auto* complex = value_as_complex(value)) {
      real = complex->real;
      imag = complex->imag;
    } else return false;
    return true;
  };
  double real = 0.0;
  double imaginary_from_real = 0.0;
  if (argc >= 2 && !numeric_part(args[1], real, imaginary_from_real)) {
    error = "complex() first argument must be a string or a number";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  double imaginary_real = 0.0;
  double imaginary_imag = 0.0;
  if (argc >= 3 && !numeric_part(args[2], imaginary_real, imaginary_imag)) {
    error = "complex() second argument must be a number";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value parsed = Value::complex(real - imaginary_imag, imaginary_from_real + imaginary_real);
  const Value* complex_class = runtime.find_builtin("complex");
  if (complex_class != nullptr && value_is(args[0], *complex_class)) {
    out = std::move(parsed);
  } else {
    out = Value::instance(args[0]);
    if (!object_set_attr(out, "__xlang3_complex_value__", parsed, error)) return false;
  }
  return true;
}

bool builtin_int_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3) {
    error = "int.__new__ expected class, optional value, and optional base";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* klass = value_as_class(args[0]);
  if (klass == nullptr) {
    error = "int.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int base = 10;
  if (argc == 3) {
    if (args[2].tag != ValueTag::Int64) {
      error = "int.__new__ base must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    base = static_cast<int>(args[2].as.i64);
  }
  Value parsed = Value::int64(0);
  if (argc == 1) {
    parsed = Value::int64(0);
  } else if (args[1].tag == ValueTag::Int64) {
    parsed = args[1];
  } else if (value_as_bigint(args[1]) != nullptr) {
    value_assign_fast(parsed, args[1]);
  } else if (args[1].tag == ValueTag::Bool) {
    parsed = Value::int64(args[1].as.b ? 1 : 0);
  } else if (args[1].tag == ValueTag::Double) {
    parsed = Value::int64(static_cast<int64_t>(args[1].as.f64));
  } else {
    Value stored;
    std::string ignored;
    if (argc != 3 && object_get_attr(args[1], "__xlang3_int_value__", stored, ignored) &&
        (stored.tag == ValueTag::Int64 || value_as_bigint(stored) != nullptr)) {
      value_assign_fast(parsed, stored);
    } else if (argc != 3 && object_get_attr(args[1], "_value_", stored, ignored) &&
               (stored.tag == ValueTag::Int64 || value_as_bigint(stored) != nullptr)) {
      value_assign_fast(parsed, stored);
    } else if (auto* text = value_as_string(args[1])) {
      const auto view = string_object_view(*text);
      if (sys_int_string_exceeds_limit(view, base)) {
        error = "Exceeds the limit for integer string conversion";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
      parsed = value_bigint_from_decimal(view, base, error);
      if (parsed.tag == ValueTag::Invalid) {
        error = "invalid literal for int()";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
    } else if (auto* bytes = value_as_bytes(args[1])) {
      const auto view = bytes_object_view(*bytes);
      if (sys_int_string_exceeds_limit(view, base)) {
        error = "Exceeds the limit for integer string conversion";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
      parsed = value_bigint_from_decimal(view, base, error);
      if (parsed.tag == ValueTag::Invalid) {
        error = "invalid literal for int()";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
    } else if (auto* bytearray = value_as_bytearray(args[1])) {
      if (sys_int_string_exceeds_limit(bytearray->value, base)) {
        error = "Exceeds the limit for integer string conversion";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
      parsed = value_bigint_from_decimal(bytearray->value, base, error);
      if (parsed.tag == ValueTag::Invalid) {
        error = "invalid literal for int()";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
    } else {
      std::string protocol_error;
      if (argc != 3 && try_int_conversion_protocol(runtime, args[1], parsed, protocol_error)) {
        // Protocol conversion succeeded.
      } else if (!protocol_error.empty()) {
        error = protocol_error;
        return false;
      } else {
        error = "int.__new__ value must be a string, bytes-like object, number, or bool";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    }
  }

  const Value* builtin_int = runtime.find_builtin("int");
  if (builtin_int != nullptr && value_as_class(*builtin_int) == klass) {
    value_assign_fast(out, parsed);
    return true;
  }
  out = Value::instance(args[0]);
  std::string ignored;
  (void)object_set_attr(out, "__xlang3_int_value__", parsed, ignored);
  return true;
}

bool builtin_type_new_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const std::vector<std::pair<std::string, Value>>& class_keywords,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 4) {
    error = "type.__new__ expected metaclass, name, bases, and namespace";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "type.__new__ metaclass must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* name = value_as_string(args[1]);
  auto* bases = value_as_tuple(args[2]);
  auto* namespace_dict = type_new_namespace_dict(args[3]);
  if (name == nullptr || bases == nullptr || namespace_dict == nullptr) {
    error = "type.__new__ expected str, tuple, and dict";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  std::vector<std::pair<std::string, Value>> attrs;
  attrs.reserve(namespace_dict->entries.size());
  std::vector<std::string> explicit_slots;
  std::vector<std::string> static_attributes;
  bool has_explicit_slots = false;
  for (const auto& entry : namespace_dict->entries) {
    auto* key = value_as_string(entry.first);
    if (key == nullptr) {
      error = "type.__new__ namespace keys must be strings";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const auto key_name = string_object_to_string(*key);
    if (key_name == "__slots__") {
      has_explicit_slots = true;
      if (!collect_type_new_slots(entry.second, explicit_slots)) {
        error = "type.__new__ __slots__ must be a string or iterable of strings";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    }
    if (key_name == "__static_attributes__") {
      (void)collect_type_new_slots(entry.second, static_attributes);
    }
    attrs.push_back({key_name, entry.second});
  }
  if (has_explicit_slots) {
    for (const auto& slot : explicit_slots) {
      for (const auto& attr : attrs) {
        if (attr.first == slot && attr.first != "__doc__") {
          error = "'" + slot + "' in __slots__ conflicts with class variable";
          runtime.raise_class_error("ValueError", error);
          return false;
        }
      }
    }
  }

  Value base = Value::invalid();
  std::vector<Value> resolved_bases;
  if (!resolve_class_bases(runtime, bases, resolved_bases, error)) {
    return false;
  }
  if (resolved_bases.empty()) {
    if (const auto* object_type = runtime.find_builtin("object")) {
      value_assign_fast(base, *object_type);
    }
  } else {
    value_assign_fast(base, resolved_bases[0]);
  }

  std::string class_name = string_object_to_string(*name);
  Value final_marker;
  std::string final_error;
  if (base.tag != ValueTag::Invalid &&
      object_get_attr(base, "__xlang3_final_type__", final_marker, final_error) &&
      value_truthy(final_marker)) {
    auto* final_class = value_as_class(base);
    error = "type '" + std::string(final_class == nullptr ? "object" : final_class->name) +
        "' is not an acceptable base type";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!class_attrs_have(attrs, "__module__")) {
    attrs.push_back({"__module__", Value::string(current_module_name(runtime))});
  }
  if (!class_attrs_have(attrs, "__qualname__")) {
    attrs.push_back({"__qualname__", Value::string(class_name)});
  }

  auto descriptor_attrs = attrs;
  const std::vector<std::string> inferred_slots =
      !has_explicit_slots && resolved_bases.empty() ? static_attributes : std::vector<std::string>{};
  out = Value::class_object(class_name, std::move(attrs), base, inferred_slots, args[0]);
  for (size_t i = 1; i < resolved_bases.size(); ++i) {
    if (!class_set_base(out, resolved_bases[i], error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (!call_set_name_descriptors(runtime, out, descriptor_attrs, error)) {
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  if (metaclass_is_abc_meta(args[0])) {
    if (!object_set_attr(out, "__abstractmethods__", abc_abstract_methods_for_type_new(bases, namespace_dict), error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (!call_init_subclass(runtime, out, bases, class_keywords, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool builtin_type_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  static const std::vector<std::pair<std::string, Value>> empty_keywords;
  return builtin_type_new_impl(runtime, args, argc, empty_keywords, out, error, user_data);
}

bool builtin_type_new_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  std::vector<std::pair<std::string, Value>> class_keywords;
  class_keywords.reserve(kwargc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "type.__new__ keyword metadata is invalid";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    class_keywords.push_back({kwargs[i].name, *kwargs[i].value});
  }
  return builtin_type_new_impl(runtime, args, argc, class_keywords, out, error, user_data);
}

bool builtin_build_class_from_namespace_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 4) {
    error = "__build_class__ expected metaclass, name, bases, and namespace";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  std::vector<std::pair<std::string, Value>> class_keywords;
  class_keywords.reserve(kwargc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "__build_class__ keyword metadata is invalid";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    class_keywords.push_back({kwargs[i].name, *kwargs[i].value});
  }

  auto* original_bases = value_as_tuple(args[2]);
  std::vector<Value> resolved_bases;
  if (!resolve_class_bases(runtime, original_bases, resolved_bases, error)) {
    return false;
  }
  bool bases_changed = original_bases->items.size() != resolved_bases.size();
  if (!bases_changed) {
    for (size_t i = 0; i < resolved_bases.size(); ++i) {
      if (!value_is(original_bases->items[i], resolved_bases[i])) {
        bases_changed = true;
        break;
      }
    }
  }
  Value effective_bases = bases_changed ? Value::tuple(resolved_bases) : args[2];
  Value namespace_value = args[3];
  if (bases_changed) {
    auto* namespace_dict = type_new_namespace_dict(namespace_value);
    if (namespace_dict != nullptr) {
      std::string set_error;
      if (!mapping_set_item(namespace_value, Value::string("__orig_bases__"), args[2], set_error)) {
        error = std::move(set_error);
        return false;
      }
    }
  }
  Value build_args[] = {args[0], args[1], effective_bases, namespace_value};

  if (value_as_class(args[0]) == nullptr) {
    Value call_args[] = {args[1], effective_bases, namespace_value};
    if (!runtime_call_callable_kw(runtime, args[0], call_args, 3, class_keywords, out, error)) {
      if (error.empty()) {
        error = "__build_class__ metaclass is not callable";
      }
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    return true;
  }

  Value constructed;
  Value new_value;
  std::string new_error;
  const bool has_new = object_get_attr(args[0], "__new__", new_value, new_error);
  auto* native_new = has_new ? value_as_native_function(new_value) : nullptr;
  const bool use_default_type_new =
      !has_new ||
      (native_new != nullptr && (native_new->name == "type.__new__" || native_new->name == "object.__new__"));
  if (use_default_type_new) {
    if (!builtin_type_new_impl(runtime, build_args, argc, class_keywords, constructed, error, user_data)) {
      return false;
    }
  } else {
    Value new_args[] = {args[0], args[1], effective_bases, namespace_value};
    if (!runtime_call_callable_kw(runtime, new_value, new_args, 4, class_keywords, constructed, error)) {
      if (error.empty()) {
        error = "__new__ failed";
      }
      return false;
    }
  }

  if (value_as_class(constructed) == nullptr) {
    value_assign_fast(out, constructed);
    return true;
  }

  Value init_value;
  std::string init_error;
  if (object_lookup_class_attr(args[0], "__init__", init_value, init_error)) {
    if (auto* native = value_as_native_function(init_value)) {
      if (native->name == "object.__init__") {
        value_assign_fast(out, constructed);
        return true;
      }
    }
    Value init_args[] = {constructed, args[1], effective_bases, namespace_value};
    Value ignored;
    if (!runtime_call_callable_kw(runtime, init_value, init_args, 4, class_keywords, ignored, error)) {
      if (error.empty()) {
        error = "__init__ failed";
      }
      return false;
    }
  }
  value_assign_fast(out, constructed);
  return true;
}

bool builtin_build_class_from_namespace(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  return builtin_build_class_from_namespace_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool resolve_class_bases(Runtime& runtime, TupleObject* bases, std::vector<Value>& resolved_bases, std::string& error) {
  if (bases == nullptr) {
    error = "bases must be a tuple";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  resolved_bases.clear();
  resolved_bases.reserve(bases->items.size());
  Value original_bases = Value::tuple(bases->items);
  for (const auto& base : bases->items) {
    if (auto* alias = value_as_generic_alias(base)) {
      if (value_as_class(alias->origin) == nullptr) {
        error = "__mro_entries__ resolved base is not a class";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      resolved_bases.push_back(alias->origin);
    } else if (value_as_class(base) != nullptr) {
      resolved_bases.push_back(base);
    } else {
      Value mro_entries;
      std::string attr_error;
      if (!attribute_get(base, "__mro_entries__", mro_entries, attr_error)) {
        error = "bases must be classes";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      Value replacement;
      if (!runtime_call_callable(runtime, mro_entries, &original_bases, 1, replacement, error)) {
        return false;
      }
      auto* replacement_tuple = value_as_tuple(replacement);
      if (replacement_tuple == nullptr) {
        error = "__mro_entries__ must return a tuple";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      for (const auto& replacement_base : replacement_tuple->items) {
        if (value_as_class(replacement_base) == nullptr) {
          error = "__mro_entries__ returned a non-class base";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        resolved_bases.push_back(replacement_base);
      }
    }
  }
  return true;
}

bool builtin_select_metaclass(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "__xlang3_select_metaclass__ expected bases";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* bases = value_as_tuple(args[0]);
  if (bases == nullptr) {
    error = "__xlang3_select_metaclass__ bases must be a tuple";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto* type_type = runtime.find_builtin("type");
  if (type_type == nullptr) {
    error = "type is not initialized";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  std::vector<Value> resolved_bases;
  if (!resolve_class_bases(runtime, bases, resolved_bases, error)) {
    return false;
  }
  value_assign_fast(out, *type_type);
  for (const auto& base : resolved_bases) {
    auto* base_class = value_as_class(base);
    if (!choose_compatible_metaclass(out, base_class->metaclass, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return true;
}

bool builtin_type_prepare(
    Runtime&,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    error = "type.__prepare__ expected name and bases";
    return false;
  }
  out = Value::dict({});
  return true;
}

bool builtin_type_prepare_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void* user_data) {
  return builtin_type_prepare(runtime, args, argc, out, error, user_data);
}

bool builtin_type_init(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 4) {
    error = "type.__init__ expected class or class, name, bases, and namespace";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_type_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void* user_data) {
  return builtin_type_init(runtime, args, argc, out, error, user_data);
}

bool builtin_type_instancecheck(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "type.__instancecheck__ expected class and instance";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value actual_type;
  if (!runtime_type_of_value(runtime, args[1], actual_type)) {
    value_set_bool(out, false);
    return true;
  }
  auto* expected = value_as_class(args[0]);
  auto* actual = value_as_class(actual_type);
  if (expected == nullptr || actual == nullptr) {
    value_set_bool(out, false);
    return true;
  }
  value_set_bool(out, class_is_subclass(actual, expected));
  return true;
}

bool builtin_type_subclasscheck(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "type.__subclasscheck__ expected class and subclass";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* expected = value_as_class(args[0]);
  auto* actual = value_as_class(args[1]);
  if (expected == nullptr || actual == nullptr) {
    value_set_bool(out, false);
    return true;
  }
  value_set_bool(out, class_is_subclass(actual, expected));
  return true;
}

bool builtin_bool_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2 || value_as_class(args[0]) == nullptr) {
    error = "bool.__new__ expected class and optional value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool truth = false;
  if (argc == 2 && !runtime_truthy(runtime, args[1], truth, error)) return false;
  value_set_bool(out, truth);
  return true;
}

bool builtin_bool_and(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "bool.__and__ expected one argument";
    return false;
  }
  return value_bit_and(args[0], args[1], out, error);
}

bool builtin_type_or(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "type.__or__ expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!value_bit_or(args[0], args[1], out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool builtin_type_call(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || value_as_class(args[0]) == nullptr) {
    error = "type.__call__ expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return runtime_call_callable(runtime, args[0], args + 1, argc - 1, out, error);
}

bool builtin_type_annotations_get(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "type.__annotations__ getter expected a class";
    return false;
  }
  auto* klass = value_as_class(args[0]);
  if (klass == nullptr) {
    error = "type.__annotations__ getter expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto module_it = klass->attrs.find("__module__");
  if (module_it != klass->attrs.end()) {
    if (auto* module_name = value_as_string(module_it->second)) {
      if (string_object_to_string(*module_name) == "builtins") {
        error = "type object '" + klass->name + "' has no attribute '__annotations__'";
        runtime.raise_class_error("AttributeError", error);
        return false;
      }
    }
  }
  return object_get_class_annotations(runtime, args[0], out, error);
}

bool builtin_type_mro_get(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1 || value_as_class(args[0]) == nullptr) {
    error = "type.__mro__ getter expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return object_get_attr(args[0], "__mro__", out, error);
}

bool builtin_type_mro(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1 || value_as_class(args[0]) == nullptr) {
    error = "type.mro() expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value mro_tuple;
  if (!object_get_attr(args[0], "__mro__", mro_tuple, error)) {
    return false;
  }
  auto* tuple = value_as_tuple(mro_tuple);
  if (tuple == nullptr) {
    error = "type.mro() internal __mro__ is not a tuple";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  out = Value::list(tuple->items);
  return true;
}

bool builtin_type_dict_get(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1 || value_as_class(args[0]) == nullptr) {
    error = "type.__dict__ getter expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return object_get_attr(args[0], "__dict__", out, error);
}

bool builtin_mappingproxy_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "mappingproxy() expected mapping";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "mappingproxy.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!mapping_is_mapping(args[1])) {
    error = "mappingproxy() argument must be a mapping";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = mapping_proxy(args[1]);
  return true;
}

bool builtin_uninstantiable_iterator_new(
    Runtime& runtime,
    const Value*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "cannot create 'iterator' instances";
  runtime.raise_class_error("TypeError", error);
  return false;
}

void register_builtin_type(Runtime& runtime, const char* name, const Value& object_base) {
  Value metaclass = Value::invalid();
  if (const auto* type_type = runtime.find_builtin("type")) {
    value_assign_fast(metaclass, *type_type);
  }
  Value builtin_type = Value::class_object(
      name,
      {{"__module__", Value::string("builtins")}, {"__qualname__", Value::string(name)}},
      object_base,
      {},
      std::move(metaclass));
  if (std::string_view(name) == "iterator") {
    std::string ignored;
    object_set_attr(builtin_type, "__xlang3_final_type__", Value::boolean(true), ignored);
    object_set_attr(
        builtin_type,
        "__new__",
        Value::static_method(runtime.make_native_function("iterator.__new__", builtin_uninstantiable_iterator_new)),
        ignored);
  }
  runtime.register_builtin(name, std::move(builtin_type));
}

bool builtin_async_generator_awaitable_await(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "async_generator_awaitable.__await__ expected self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool builtin_object_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    if (argc > 1) {
      if (auto* instance = value_as_instance(args[0])) {
        if (auto* klass = value_as_class(instance->klass)) {
          if (class_has_builtin_base_name(klass, "str") ||
              class_has_builtin_base_name(klass, "bytes") ||
              class_has_builtin_base_name(klass, "int") ||
              class_has_builtin_base_name(klass, "float") ||
              class_has_builtin_base_name(klass, "tuple")) {
            value_set_none(out);
            return true;
          }
          Value new_attr;
          std::string new_error;
          if (object_lookup_class_attr(instance->klass, "__new__", new_attr, new_error)) {
            if (auto* method = value_as_static_method(new_attr)) {
              value_assign_fast(new_attr, method->function);
            }
            if (value_as_function(new_attr) != nullptr) {
              value_set_none(out);
              return true;
            }
            if (auto* native = value_as_native_function(new_attr)) {
              if (native->name != "object.__new__") {
                value_set_none(out);
                return true;
              }
            }
          }
        }
      }
    }
    error = "object.__init__ expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_object_init_subclass(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "object.__init_subclass__ expected class";
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_object_init_subclass_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void* user_data) {
  return builtin_object_init_subclass(runtime, args, argc, out, error, user_data);
}

bool builtin_object_subclasshook(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "object.__subclasshook__() takes exactly one argument (" +
            std::to_string(argc > 0 ? argc - 1 : 0) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* not_implemented = runtime.find_builtin("NotImplemented");
  if (not_implemented == nullptr) {
    error = "NotImplemented is unavailable";
    return false;
  }
  value_assign_fast(out, *not_implemented);
  return true;
}

bool builtin_int_new_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (kwargc == 0) return builtin_int_new(runtime, args, argc, out, error, user_data);
  if (kwargc != 1 || kwargs[0].name == nullptr || kwargs[0].value == nullptr ||
      std::string_view(kwargs[0].name) != "base") {
    error = "int.__new__() got an unexpected keyword argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc >= 3) {
    error = "int.__new__() got multiple values for argument 'base'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> positional;
  positional.reserve(argc + 1);
  for (uint32_t i = 0; i < argc; ++i) positional.push_back(args[i]);
  positional.push_back(*kwargs[0].value);
  return builtin_int_new(
      runtime, positional.data(), static_cast<uint32_t>(positional.size()), out, error, user_data);
}

bool builtin_object_repr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "object.__repr__ expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::string(value_to_repr(args[0]));
  return true;
}

bool builtin_object_str(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "object.__str__ expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value repr_method;
  std::string attr_error;
  if (attribute_get(args[0], "__repr__", repr_method, attr_error)) {
    Value result;
    if (!runtime_call_callable(runtime, repr_method, nullptr, 0, result, error)) {
      return false;
    }
    if (value_as_string(result) == nullptr) {
      error = "__repr__ returned non-string";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    out = result;
    return true;
  }
  out = Value::string(value_to_repr(args[0]));
  return true;
}

bool descriptor_callable_arg(const Value& value) {
  if (value_as_function(value) != nullptr ||
      value_as_native_function(value) != nullptr ||
      value_as_bound_method(value) != nullptr ||
      value_as_class(value) != nullptr) {
    return true;
  }
  if (value_as_instance(value) != nullptr) {
    Value call_attr;
    std::string ignored;
    return object_get_class_attr_for_instance(value, "__call__", call_attr, ignored);
  }
  return false;
}

bool descriptor_init_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    const char* type_name) {
  if (argc != 2) {
    error = std::string(type_name) + ".__init__ expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!descriptor_callable_arg(args[1])) {
    error = std::string(type_name) + "() argument must be callable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value self;
  value_assign_fast(self, args[0]);
  if (!object_set_attr(self, "__func__", args[1], error) ||
      !object_set_attr(self, "__wrapped__", args[1], error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool descriptor_init_common_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    const char* type_name) {
  if (argc < 1 || argc > 2) {
    error = std::string(type_name) + ".__init__ expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* callable = argc == 2 ? &args[1] : nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name = kwargs[i].name != nullptr ? std::string_view(kwargs[i].name) : std::string_view();
    if (name != "callable") {
      error = std::string(type_name) + ".__init__ got an unexpected keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (callable != nullptr) {
      error = std::string(type_name) + ".__init__ got multiple values for argument 'callable'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    callable = kwargs[i].value;
  }
  if (callable == nullptr) {
    error = std::string(type_name) + ".__init__ expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value init_args[] = {args[0], *callable};
  return descriptor_init_common(runtime, init_args, 2, out, error, type_name);
}

bool builtin_classmethod_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  return descriptor_init_common(runtime, args, argc, out, error, "classmethod");
}

bool builtin_classmethod_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return descriptor_init_common_kw(runtime, args, argc, kwargs, kwargc, out, error, "classmethod");
}

bool builtin_staticmethod_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  return descriptor_init_common(runtime, args, argc, out, error, "staticmethod");
}

bool builtin_staticmethod_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return descriptor_init_common_kw(runtime, args, argc, kwargs, kwargc, out, error, "staticmethod");
}

bool builtin_object_eq(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "object.__eq__ expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_is(args[0], args[1])) {
    value_set_bool(out, true);
    return true;
  }
  const Value* not_implemented = runtime.find_builtin("NotImplemented");
  if (not_implemented == nullptr) {
    error = "NotImplemented is unavailable";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  value_assign_fast(out, *not_implemented);
  return true;
}

bool builtin_object_ne(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "object.__ne__ expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_is(args[0], args[1])) {
    value_set_bool(out, false);
    return true;
  }
  const Value* not_implemented = runtime.find_builtin("NotImplemented");
  if (not_implemented == nullptr) {
    error = "NotImplemented is unavailable";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  value_assign_fast(out, *not_implemented);
  return true;
}

bool builtin_object_order(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "object comparison expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* not_implemented = runtime.find_builtin("NotImplemented");
  if (not_implemented == nullptr) {
    error = "NotImplemented is unavailable";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  value_assign_fast(out, *not_implemented);
  return true;
}

bool builtin_object_hash(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "object.__hash__ expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  size_t hash = 0;
  if (!value_hash_key(args[0], hash, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::int64(static_cast<int64_t>(hash));
  return true;
}

bool builtin_object_dir(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "__dir__ expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  std::set<std::string> names{"__class__"};
  const auto add_class_names = [&](const ClassObject* root) {
    std::vector<const ClassObject*> pending{root};
    std::set<const ClassObject*> visited;
    while (!pending.empty()) {
      const ClassObject* current = pending.back();
      pending.pop_back();
      if (current == nullptr || !visited.insert(current).second) {
        continue;
      }
      for (const auto& attr : current->attrs) {
        names.insert(attr.first);
      }
      for (const auto& slot : current->instance_slot_names) {
        names.insert(slot);
      }
      for (const auto& base : current->bases) {
        if (auto* base_class = value_as_class(base)) {
          pending.push_back(base_class);
        }
      }
    }
  };

  if (auto* instance = value_as_instance(args[0])) {
    for (const auto& attr : instance->attrs) {
      names.insert(attr.first);
    }
    add_class_names(value_as_class(instance->klass));
  } else if (auto* klass = value_as_class(args[0])) {
    add_class_names(klass);
  } else if (auto* module = value_as_module(args[0])) {
    for (const auto& attr : module->name_to_slot) {
      if (attr.second < module->slots.size() && module->slots[attr.second].tag != ValueTag::Invalid) {
        names.insert(attr.first);
      }
    }
    names.insert("__name__");
  } else {
    Value type;
    if (runtime_type_of_value(runtime, args[0], type)) {
      add_class_names(value_as_class(type));
    }
  }

  std::vector<Value> result;
  result.reserve(names.size());
  for (const auto& name : names) {
    result.push_back(Value::string(name));
  }
  out = Value::list(std::move(result));
  return true;
}

bool builtin_object_getattribute(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "object.__getattribute__ expected 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "attribute name must be string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!attribute_get(args[0], string_object_to_string(*name), out, error)) {
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  return true;
}

bool builtin_object_setattr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 3) {
    error = "object.__setattr__ expected 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "attribute name must be string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target = args[0];
  Value descriptor;
  std::string descriptor_error;
  const bool descriptor_found = object_get_class_attr_for_instance(
      target, string_object_to_string(*name), descriptor, descriptor_error);
  if (descriptor_found) {
    Value setter;
    if (attribute_get(descriptor, "__set__", setter, descriptor_error)) {
      Value setter_args[2] = {target, args[2]};
      if (!runtime_call_callable(runtime, setter, setter_args, 2, out, error)) {
        return false;
      }
      value_set_none(out);
      return true;
    }
  }
  if (!object_set_attr(target, string_object_to_string(*name), args[2], error)) {
    error = "object.__setattr__ target " + value_to_string(args[0]) + ": " + error;
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_object_delattr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "object.__delattr__ expected 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "attribute name must be string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target = args[0];
  Value descriptor;
  std::string descriptor_error;
  if (object_get_class_attr_for_instance(target, string_object_to_string(*name), descriptor, descriptor_error)) {
    Value deleter;
    if (attribute_get(descriptor, "__delete__", deleter, descriptor_error)) {
      if (!runtime_call_callable(runtime, deleter, &target, 1, out, error)) {
        return false;
      }
      value_set_none(out);
      return true;
    }
  }
  if (!object_delete_attr(target, string_object_to_string(*name), error)) {
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_generic_alias_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc == 4 && value_as_string(args[1]) != nullptr && value_as_tuple(args[2]) != nullptr &&
      type_new_namespace_dict(args[3]) != nullptr) {
    auto* bases = value_as_tuple(args[2]);
    std::vector<Value> resolved_bases;
    resolved_bases.reserve(bases->items.size());
    for (const auto& base : bases->items) {
      if (auto* alias = value_as_generic_alias(base)) {
        resolved_bases.push_back(alias->origin);
      } else {
        resolved_bases.push_back(base);
      }
    }
    Value metaclass;
    if (const auto* type_value = runtime.find_builtin("type")) {
      value_assign_fast(metaclass, *type_value);
    } else {
      value_assign_fast(metaclass, args[0]);
    }
    Value resolved_args[] = {metaclass, args[1], Value::tuple(std::move(resolved_bases)), args[3]};
    return builtin_type_new(runtime, resolved_args, 4, out, error, nullptr);
  }
  if (argc != 3) {
    error = "GenericAlias expected origin and args";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value alias_args;
  if (value_as_tuple(args[2]) != nullptr) {
    value_assign_fast(alias_args, args[2]);
  } else {
    alias_args = Value::tuple({args[2]});
  }
  out = Value::generic_alias(args[1], std::move(alias_args));
  if (auto* alias = value_as_generic_alias(out)) {
    if (auto* requested_class = value_as_class(args[0]);
        requested_class != nullptr && requested_class->name != "GenericAlias") {
      value_assign_fast(alias->klass, args[0]);
    }
  }
  return true;
}

} // namespace

bool runtime_type_of_value(Runtime& runtime, const Value& value, Value& out) {
  switch (value.tag) {
    case ValueTag::None:
      if (const auto* type = find_builtin_type(runtime, "NoneType")) {
        value_assign_fast(out, *type);
        return true;
      }
      break;
    case ValueTag::Bool:
      if (const auto* type = find_builtin_type(runtime, "bool")) {
        value_assign_fast(out, *type);
        return true;
      }
      break;
    case ValueTag::Int64:
      if (const auto* type = find_builtin_type(runtime, "int")) {
        value_assign_fast(out, *type);
        return true;
      }
      break;
    case ValueTag::Double:
      if (const auto* type = find_builtin_type(runtime, "float")) {
        value_assign_fast(out, *type);
        return true;
      }
      break;
    case ValueTag::Object: {
      if (value.as.obj == nullptr) {
        break;
      }
      if (auto* instance = value_as_instance(value)) {
        value_assign_fast(out, instance->klass);
        return true;
      }
      if (auto* alias = value_as_generic_alias(value);
          alias != nullptr && value_as_class(alias->klass) != nullptr) {
        value_assign_fast(out, alias->klass);
        return true;
      }
      if (auto* klass = value_as_class(value)) {
        if (klass->metaclass.tag != ValueTag::Invalid) {
          value_assign_fast(out, klass->metaclass);
          return true;
        }
      }
      if (value.as.obj->kind == ObjectKind::MapIterator) {
        if (const auto* type = runtime.find_builtin("__xlang3_map_type__")) {
          value_assign_fast(out, *type);
          return true;
        }
      }
      if (value.as.obj->kind == ObjectKind::ProtocolIterator) {
        auto* iterator = reinterpret_cast<ProtocolIteratorObject*>(value.as.obj);
        if (auto* inner = value_as_instance(iterator->iterator)) {
          value_assign_fast(out, inner->klass);
          return true;
        }
      }
      const char* type_name = builtin_type_name_for_kind(value.as.obj->kind);
      if (auto* native = value_as_native_function(value)) {
        if (native->bind_as_descriptor) {
          const auto dot = native->name.rfind('.');
          const std::string_view method_name = dot == std::string::npos
              ? std::string_view(native->name)
              : std::string_view(native->name).substr(dot + 1);
          const bool wrapper_descriptor =
              method_name.size() >= 4 && method_name.substr(0, 2) == "__" &&
              method_name.substr(method_name.size() - 2) == "__" &&
              method_name != "__instancecheck__" && method_name != "__subclasscheck__";
          type_name = wrapper_descriptor
              ? "wrapper_descriptor"
              : "method_descriptor";
        } else {
          type_name = "builtin_function_or_method";
        }
      }
      if (auto* method = value_as_class_method(value)) {
        if (value_as_native_function(method->function) != nullptr) {
          type_name = "classmethod_descriptor";
        }
      }
      if (auto* bound = value_as_bound_method(value)) {
        if (auto* native = value_as_native_function(bound->function)) {
          const auto dot = native->name.rfind('.');
          const std::string_view method_name = dot == std::string::npos
              ? std::string_view(native->name)
              : std::string_view(native->name).substr(dot + 1);
          const bool method_wrapper =
              method_name.size() >= 4 && method_name.substr(0, 2) == "__" &&
              method_name.substr(method_name.size() - 2) == "__" &&
              method_name != "__instancecheck__" && method_name != "__subclasscheck__";
          type_name = method_wrapper
              ? "method-wrapper"
              : "builtin_function_or_method";
        }
      }
      if (auto* generator = value_as_generator(value)) {
        type_name = generator->is_coroutine ? "coroutine" : (generator->is_async ? "async_generator" : "generator");
      }
      if (value.as.obj->kind == ObjectKind::Set) {
        if (auto* set = value_as_set(value); set != nullptr && set->frozen) {
          type_name = "frozenset";
        }
      }
      if (const auto* type = find_builtin_type(runtime, type_name)) {
        if (value_as_class(*type) != nullptr) {
          value_assign_fast(out, *type);
          return true;
        }
      }
      break;
    }
    case ValueTag::Invalid:
      break;
  }
  if (const auto* object_type = find_builtin_type(runtime, "object")) {
    value_assign_fast(out, *object_type);
    return true;
  }
  return false;
}

bool builtin_function_descriptor_get(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3 || value_as_function(args[0]) == nullptr) {
    error = "descriptor '__get__' requires a function object";
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

bool builtin_native_descriptor_get(
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

bool builtin_descriptor_instance_payload(
    const Value& value,
    std::string_view builtin_base_name,
    Value& out) {
  auto* instance = value_as_instance(value);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr || !class_has_builtin_base_name(klass, builtin_base_name)) {
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

bool builtin_numeric_repr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "numeric __repr__ expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value numeric = args[0];
  if (value_as_instance(numeric) != nullptr) {
    Value stored;
    std::string ignored;
    if ((object_get_attr(numeric, "__xlang3_int_value__", stored, ignored) ||
         object_get_attr(numeric, "__xlang3_float_value__", stored, ignored) ||
         object_get_attr(numeric, "_value_", stored, ignored)) &&
        (stored.tag == ValueTag::Int64 || stored.tag == ValueTag::Double ||
         value_as_bigint(stored) != nullptr)) {
      numeric = std::move(stored);
    }
  }
  if (numeric.tag != ValueTag::Int64 && numeric.tag != ValueTag::Double &&
      value_as_bigint(numeric) == nullptr) {
    error = "numeric descriptor requires a number";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::string(value_to_repr(numeric));
  return true;
}

bool builtin_descriptor_method_proxy(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1 || user_data == nullptr) {
    error = "descriptor method requires a descriptor";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string_view method_name(static_cast<const char*>(user_data));
  if (method_name == "__get__") {
    Value function;
    if (builtin_descriptor_instance_payload(args[0], "staticmethod", function)) {
      if (argc < 2 || argc > 3) {
        error = "staticmethod.__get__ expected an instance and optional owner";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(out, function);
      return true;
    }
    if (builtin_descriptor_instance_payload(args[0], "classmethod", function)) {
      if (argc < 2 || argc > 3) {
        error = "classmethod.__get__ expected an instance and optional owner";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      Value owner;
      if (argc == 3 && args[2].tag != ValueTag::None) {
        value_assign_fast(owner, args[2]);
      } else if (!runtime_type_of_value(runtime, args[1], owner)) {
        error = "classmethod.__get__ could not resolve owner";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      out = Value::bound_method(std::move(owner), std::move(function));
      return true;
    }
  }
  Value method;
  if (!object_get_attr(args[0], std::string(method_name), method, error)) {
    return false;
  }
  return runtime_call_callable(runtime, method, args + 1, argc - 1, out, error);
}

bool builtin_float_getformat(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "float.__getformat__() argument 1 must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string kind = string_object_to_string(*value_as_string(args[0]));
  if (kind != "double" && kind != "float") {
    error = "__getformat__() argument 1 must be 'double' or 'float'";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::string("IEEE, little-endian");
  return true;
}

void register_object_type_builtins(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> object_attrs;
  object_attrs.push_back({"__module__", Value::string("builtins")});
  object_attrs.push_back({"__qualname__", Value::string("object")});
  object_attrs.push_back({"__type_params__", Value::tuple({})});
  object_attrs.push_back({"__text_signature__", Value::string("()")});
  Value object_new = Value::native_function(0, "object.__new__", builtin_object_new);
  builtin_method_set_text_signature(object_new, "(*args, **kwargs)");
  object_attrs.push_back({"__new__", std::move(object_new)});
  Value object_init = Value::native_function(0, "object.__init__", builtin_object_init);
  builtin_method_set_text_signature(object_init, "($self, /, *args, **kwargs)");
  object_attrs.push_back({"__init__", std::move(object_init)});
  object_attrs.push_back({"__init_subclass__", Value::class_method(runtime.make_native_function(
                                                "object.__init_subclass__",
                                                builtin_object_init_subclass,
                                                nullptr,
                                                nullptr,
                                                nullptr,
                                                false,
                                                builtin_object_init_subclass_kw))});
  object_attrs.push_back({"__subclasshook__", Value::class_method(Value::native_function(
                                               0, "object.__subclasshook__", builtin_object_subclasshook))});
  object_attrs.push_back({"__eq__", Value::native_function(0, "object.__eq__", builtin_object_eq)});
  object_attrs.push_back({"__ne__", Value::native_function(0, "object.__ne__", builtin_object_ne)});
  object_attrs.push_back({"__lt__", Value::native_function(0, "object.__lt__", builtin_object_order)});
  object_attrs.push_back({"__le__", Value::native_function(0, "object.__le__", builtin_object_order)});
  object_attrs.push_back({"__gt__", Value::native_function(0, "object.__gt__", builtin_object_order)});
  object_attrs.push_back({"__ge__", Value::native_function(0, "object.__ge__", builtin_object_order)});
  object_attrs.push_back({"__hash__", Value::native_function(0, "object.__hash__", builtin_object_hash)});
  object_attrs.push_back({"__repr__", Value::native_function(0, "object.__repr__", builtin_object_repr)});
  object_attrs.push_back({"__str__", Value::native_function(0, "object.__str__", builtin_object_str)});
  object_attrs.push_back({"__format__", Value::native_function(0, "object.__format__", builtin_object_format)});
  object_attrs.push_back({"__dir__", Value::native_function(0, "object.__dir__", builtin_object_dir)});
  object_attrs.push_back({"__reduce__", Value::native_function(0, "object.__reduce__", builtin_object_reduce)});
  object_attrs.push_back({"__reduce_ex__", Value::native_function(0, "object.__reduce_ex__", builtin_object_reduce_ex)});
  object_attrs.push_back({"__getstate__", Value::native_function(0, "object.__getstate__", builtin_object_getstate)});
  object_attrs.push_back({
      "__subclasses__",
      Value::class_method(Value::native_function(0, "object.__subclasses__", builtin_object_subclasses))});
  object_attrs.push_back({"__getattribute__", Value::native_function(0, "object.__getattribute__", builtin_object_getattribute)});
  object_attrs.push_back({"__setattr__", Value::native_function(0, "object.__setattr__", builtin_object_setattr)});
  object_attrs.push_back({"__delattr__", Value::native_function(0, "object.__delattr__", builtin_object_delattr)});
  Value object_type = Value::class_object("object", std::move(object_attrs));
  runtime.register_builtin("object", object_type);

  Value type_mro_descriptor = slot_descriptor("type", "__mro__", 0);
  Value type_dict_descriptor = slot_descriptor("type", "__dict__", 1);
  Value type_annotations_descriptor = slot_descriptor("type", "__annotations__", 2);
  Value type_type = Value::class_object(
      "type",
      {
          {"__module__", Value::string("builtins")},
          {"__qualname__", Value::string("type")},
          {"__new__", Value::native_function(
                          0,
                          "type.__new__",
                          builtin_type_new,
                          nullptr,
                          nullptr,
                          nullptr,
                          false,
                          builtin_type_new_kw)},
          {"__init__", Value::native_function(
                             0,
                             "type.__init__",
                             builtin_type_init,
                             nullptr,
                             nullptr,
                              nullptr,
                              false,
                              builtin_type_init_kw)},
          {"__instancecheck__", Value::native_function(0, "type.__instancecheck__", builtin_type_instancecheck)},
          {"__subclasscheck__", Value::native_function(0, "type.__subclasscheck__", builtin_type_subclasscheck)},
          {"__dir__", Value::native_function(0, "type.__dir__", builtin_object_dir)},
          {"mro", Value::native_function(0, "type.mro", builtin_type_mro)},
          {"__prepare__", Value::native_function(
                              0,
                              "type.__prepare__",
                              builtin_type_prepare,
                              nullptr,
                              nullptr,
                              nullptr,
                              false,
                              builtin_type_prepare_kw)},
          {"__mro__", type_mro_descriptor},
          {"__dict__", type_dict_descriptor},
          {"__annotations__", type_annotations_descriptor},
      },
      object_type);
  if (auto* object_class = value_as_class(object_type)) {
    value_assign_fast(object_class->metaclass, type_type);
  }
  if (auto* type_class = value_as_class(type_type)) {
    value_assign_fast(type_class->metaclass, type_type);
    Value type_or = Value::native_function(0, "type.__or__", builtin_type_or);
    builtin_method_set_text_signature(type_or, "($self, value, /)");
    type_class->attrs["__or__"] = std::move(type_or);
    Value type_call = Value::native_function(0, "type.__call__", builtin_type_call);
    builtin_method_set_text_signature(type_call, "($self, /, *args, **kwargs)");
    type_class->attrs["__call__"] = std::move(type_call);
    builtin_method_set_text_signature(type_class->attrs["__subclasscheck__"], "($self, subclass, /)");
    slot_descriptor_set_owner_class(type_class->attrs["__mro__"], type_type);
    slot_descriptor_set_owner_class(type_class->attrs["__dict__"], type_type);
    slot_descriptor_set_owner_class(type_class->attrs["__annotations__"], type_type);
  }
  runtime.register_builtin("type", type_type);
  runtime.register_builtin("__debug__", Value::boolean(true));
  runtime.register_builtin(
      "__xlang3_build_class_from_namespace__",
      runtime.make_native_function(
          "__xlang3_build_class_from_namespace__",
          builtin_build_class_from_namespace,
          nullptr,
          nullptr,
          nullptr,
          false,
          builtin_build_class_from_namespace_kw));
  runtime.register_builtin(
      "__xlang3_select_metaclass__",
      runtime.make_native_function("__xlang3_select_metaclass__", builtin_select_metaclass));

  register_builtin_type(runtime, "NoneType", object_type);
  register_builtin_type(runtime, "int", object_type);
  if (const auto* int_value = runtime.find_builtin("int")) {
    if (auto* int_class = value_as_class(*int_value)) {
      int_class->attrs["__new__"] = Value::static_method(Value::native_function(
          0, "int.__new__", builtin_int_new, nullptr, nullptr, nullptr, false, builtin_int_new_kw));
      int_install_class_methods(runtime, *int_class);
      int_class->attrs["__repr__"] = Value::native_function(0, "int.__repr__", builtin_numeric_repr);
      int_class->attrs["__str__"] = Value::native_function(0, "int.__str__", builtin_numeric_repr);
      ++int_class->version;
    }
  }
  const Value* int_type = runtime.find_builtin("int");
  register_builtin_type(runtime, "bool", int_type != nullptr ? *int_type : object_type);
  if (const auto* bool_value = runtime.find_builtin("bool")) {
    if (auto* bool_class = value_as_class(*bool_value)) {
      Value bool_new = Value::native_function(0, "bool.__new__", builtin_bool_new);
      builtin_method_set_text_signature(bool_new, "($type, object=False, /)");
      bool_class->attrs["__new__"] = Value::static_method(std::move(bool_new));
      bool_class->attrs["__and__"] = Value::native_function(0, "bool.__and__", builtin_bool_and);
      ++bool_class->version;
    }
  }
  register_builtin_type(runtime, "float", object_type);
  if (const auto* float_value = runtime.find_builtin("float")) {
    if (auto* float_class = value_as_class(*float_value)) {
      float_class->attrs["__getformat__"] = Value::native_function(0, "float.__getformat__", builtin_float_getformat);
      float_class->attrs["__repr__"] = Value::native_function(0, "float.__repr__", builtin_numeric_repr);
      float_class->attrs["__str__"] = Value::native_function(0, "float.__str__", builtin_numeric_repr);
      ++float_class->version;
    }
  }
  register_builtin_type(runtime, "complex", object_type);
  if (const auto* complex_value = runtime.find_builtin("complex")) {
    if (auto* complex_class = value_as_class(*complex_value)) {
      complex_class->attrs["__new__"] = Value::static_method(
          Value::native_function(0, "complex.__new__", builtin_complex_new));
      ++complex_class->version;
    }
  }
  register_builtin_type(runtime, "str", object_type);
  if (const auto* str_value = runtime.find_builtin("str")) {
    if (auto* str_class = value_as_class(*str_value)) {
      str_class->attrs["__new__"] = Value::native_function(0, "str.__new__", builtin_str_new);
      str_class->attrs["__getnewargs__"] =
          Value::native_function(0, "str.__getnewargs__", builtin_str_getnewargs);
      string_install_class_methods(runtime, *str_class);
    }
  }
  register_builtin_type(runtime, "bytes", object_type);
  if (const auto* bytes_value = runtime.find_builtin("bytes")) {
    if (auto* bytes_class = value_as_class(*bytes_value)) {
      bytes_install_class_methods(runtime, *bytes_class);
    }
  }
  register_builtin_type(runtime, "bytearray", object_type);
  if (const auto* bytearray_value = runtime.find_builtin("bytearray")) {
    if (auto* bytearray_class = value_as_class(*bytearray_value)) {
      bytes_install_class_methods(runtime, *bytearray_class);
    }
  }
  register_builtin_type(runtime, "memoryview", object_type);
  register_builtin_type(runtime, "slice", object_type);
  register_builtin_type(runtime, "tuple", object_type);
  if (auto* tuple_class = value_as_class(*runtime.find_builtin("tuple"))) {
    tuple_class->attrs["__new__"] = Value::native_function(0, "tuple.__new__", builtin_tuple_new);
    tuple_install_class_methods(runtime, *tuple_class);
  }
  register_builtin_type(runtime, "list", object_type);
  if (const auto* list_value = runtime.find_builtin("list")) {
    if (auto* list_class = value_as_class(*list_value)) {
      list_class->attrs["__init__"] = runtime.make_native_function("list.__init__", builtin_list_init);
      list_install_class_methods(runtime, *list_class);
    }
  }
  if (const auto* list_value = runtime.find_builtin("list")) {
    if (auto* list_class = value_as_class(*list_value)) {
      list_class->attrs["__hash__"] = Value::none();
      ++list_class->version;
    }
  }
  register_builtin_type(runtime, "dict", object_type);
  if (const auto* dict_value = runtime.find_builtin("dict")) {
    if (auto* dict_class = value_as_class(*dict_value)) {
      dict_class->attrs["__init__"] =
          runtime.make_native_function("dict.__init__", builtin_dict_init, nullptr, nullptr, nullptr, false, builtin_dict_init_kw);
      dict_class->attrs["fromkeys"] = make_dict_fromkeys_classmethod();
      dict_class->attrs["__hash__"] = Value::none();
      dict_install_class_methods(runtime, *dict_class);
      ++dict_class->version;
    }
  }
  register_builtin_type(runtime, "dict_keys", object_type);
  register_builtin_type(runtime, "dict_values", object_type);
  register_builtin_type(runtime, "dict_items", object_type);
  register_builtin_type(runtime, "mappingproxy", object_type);
  if (const auto* mappingproxy_value = runtime.find_builtin("mappingproxy")) {
    if (auto* mappingproxy_class = value_as_class(*mappingproxy_value)) {
      mappingproxy_class->attrs["__new__"] = Value::native_function(0, "mappingproxy.__new__", builtin_mappingproxy_new);
      ++mappingproxy_class->version;
    }
  }
  register_builtin_type(runtime, "set", object_type);
  if (const auto* set_value = runtime.find_builtin("set")) {
    if (auto* set_class = value_as_class(*set_value)) {
      set_class->attrs["__hash__"] = Value::none();
      set_install_class_methods(runtime, *set_class);
      ++set_class->version;
    }
  }
  register_builtin_type(runtime, "frozenset", object_type);
  const auto install_concrete_repr = [&](const char* type_name) {
    const Value* type_value = runtime.find_builtin(type_name);
    auto* type_class = type_value == nullptr ? nullptr : value_as_class(*type_value);
    if (type_class == nullptr) return;
    type_class->attrs["__repr__"] = Value::native_function(
        0, std::string(type_name) + ".__repr__", builtin_object_repr);
    ++type_class->version;
  };
  for (const char* type_name : {
           "str", "bytes", "bytearray", "tuple", "list", "dict",
           "mappingproxy", "set", "frozenset"}) {
    install_concrete_repr(type_name);
  }
  register_builtin_type(runtime, "range", object_type);
  register_builtin_type(runtime, "iterator", object_type);
  register_builtin_type(runtime, "enumerate", object_type);
  register_builtin_type(runtime, "zip", object_type);
  register_builtin_type(runtime, "map", object_type);
  register_builtin_type(runtime, "filter", object_type);
  for (const char* type_name : {"enumerate", "zip", "map", "filter"}) {
    if (const auto* type_value = runtime.find_builtin(type_name)) {
      runtime.register_builtin(std::string("__xlang3_") + type_name + "_type__", *type_value);
    }
  }
  register_builtin_type(runtime, "generator", object_type);
  register_builtin_type(runtime, "async_generator_awaitable", object_type);
  if (const auto* awaitable_value = runtime.find_builtin("async_generator_awaitable")) {
    if (auto* awaitable_class = value_as_class(*awaitable_value)) {
      awaitable_class->attrs["__await__"] =
          runtime.make_native_function("async_generator_awaitable.__await__", builtin_async_generator_awaitable_await);
      awaitable_class->attrs["__iter__"] =
          runtime.make_native_function("async_generator_awaitable.__iter__", builtin_async_generator_awaitable_await);
      ++awaitable_class->version;
    }
  }
  register_builtin_type(runtime, "module", object_type);
  if (const auto* module_value = runtime.find_builtin("module")) {
    if (auto* module_class = value_as_class(*module_value)) {
      module_class->attrs["__new__"] = Value::native_function(0, "module.__new__", builtin_module_new);
      module_class->attrs["__init__"] = Value::native_function(0, "module.__init__", builtin_module_init);
      ++module_class->version;
    }
  }
  register_builtin_type(runtime, "function", object_type);
  if (const auto* function_value = runtime.find_builtin("function")) {
    if (auto* function_class = value_as_class(*function_value)) {
      function_class->attrs["__code__"] = slot_descriptor("function", "__code__", 0);
      function_class->attrs["__globals__"] = slot_descriptor("function", "__globals__", 1);
      function_class->attrs["__get__"] = Value::native_function(
          0, "function.__get__", builtin_function_descriptor_get,
          nullptr, nullptr, nullptr, false, nullptr, false);
      function_class->has_descriptors = true;
      ++function_class->version;
    }
  }
  register_builtin_type(runtime, "builtin_function_or_method", object_type);
  register_builtin_type(runtime, "wrapper_descriptor", object_type);
  if (const auto* descriptor_value = runtime.find_builtin("wrapper_descriptor")) {
    if (auto* descriptor_class = value_as_class(*descriptor_value)) {
      descriptor_class->attrs["__get__"] = Value::native_function(
          0, "wrapper_descriptor.__get__", builtin_native_descriptor_get);
      descriptor_class->has_descriptors = true;
      ++descriptor_class->version;
    }
  }
  register_builtin_type(runtime, "method_descriptor", object_type);
  if (const auto* descriptor_value = runtime.find_builtin("method_descriptor")) {
    if (auto* descriptor_class = value_as_class(*descriptor_value)) {
      descriptor_class->attrs["__get__"] = Value::native_function(
          0, "method_descriptor.__get__", builtin_native_descriptor_get);
      descriptor_class->has_descriptors = true;
      ++descriptor_class->version;
    }
  }
  register_builtin_type(runtime, "classmethod_descriptor", object_type);
  if (const auto* descriptor_value = runtime.find_builtin("classmethod_descriptor")) {
    if (auto* descriptor_class = value_as_class(*descriptor_value)) {
      descriptor_class->attrs["__get__"] = runtime.make_native_function(
          "classmethod_descriptor.__get__", builtin_descriptor_method_proxy, const_cast<char*>("__get__"));
      descriptor_class->has_descriptors = true;
      ++descriptor_class->version;
    }
  }
  register_builtin_type(runtime, "method-wrapper", object_type);
  register_builtin_type(runtime, "method", object_type);
  if (const auto* method_value = runtime.find_builtin("method")) {
    if (auto* method_class = value_as_class(*method_value)) {
      method_class->attrs["__new__"] = Value::native_function(0, "method.__new__", builtin_method_new);
      ++method_class->version;
    }
  }
  register_builtin_type(runtime, "member_descriptor", object_type);
  if (const auto* descriptor_value = runtime.find_builtin("member_descriptor")) {
    if (auto* descriptor_class = value_as_class(*descriptor_value)) {
      descriptor_class->attrs["__get__"] = runtime.make_native_function(
          "member_descriptor.__get__", builtin_descriptor_method_proxy, const_cast<char*>("__get__"));
      descriptor_class->attrs["__set__"] = runtime.make_native_function(
          "member_descriptor.__set__", builtin_descriptor_method_proxy, const_cast<char*>("__set__"));
      descriptor_class->attrs["__delete__"] = runtime.make_native_function(
          "member_descriptor.__delete__", builtin_descriptor_method_proxy, const_cast<char*>("__delete__"));
      descriptor_class->has_descriptors = true;
      ++descriptor_class->version;
    }
  }
  register_builtin_type(runtime, "property", object_type);
  if (const auto* property_value = runtime.find_builtin("property")) {
    if (auto* property_class = value_as_class(*property_value)) {
      property_install_class_methods(runtime, *property_class);
    }
  }
  register_builtin_type(runtime, "classmethod", object_type);
  register_builtin_type(runtime, "staticmethod", object_type);
  if (const auto* classmethod_value = runtime.find_builtin("classmethod")) {
    if (auto* classmethod_class = value_as_class(*classmethod_value)) {
      classmethod_class->attrs["__text_signature__"] = Value::string("(function, /)");
      classmethod_class->attrs["__init__"] = runtime.make_native_function(
          "classmethod.__init__", builtin_classmethod_init, nullptr, nullptr, nullptr, false, builtin_classmethod_init_kw);
      classmethod_class->attrs["__get__"] = runtime.make_native_function(
          "classmethod.__get__", builtin_descriptor_method_proxy, const_cast<char*>("__get__"));
      ++classmethod_class->version;
    }
  }
  if (const auto* staticmethod_value = runtime.find_builtin("staticmethod")) {
    if (auto* staticmethod_class = value_as_class(*staticmethod_value)) {
      staticmethod_class->attrs["__text_signature__"] = Value::string("(function, /)");
      staticmethod_class->attrs["__init__"] = runtime.make_native_function(
          "staticmethod.__init__", builtin_staticmethod_init, nullptr, nullptr, nullptr, false, builtin_staticmethod_init_kw);
      staticmethod_class->attrs["__get__"] = runtime.make_native_function(
          "staticmethod.__get__", builtin_descriptor_method_proxy, const_cast<char*>("__get__"));
      ++staticmethod_class->version;
    }
  }
  register_builtin_type(runtime, "code", object_type);
  if (const auto* code_value = runtime.find_builtin("code")) {
    if (auto* code_class = value_as_class(*code_value)) {
      static constexpr const char* code_members[] = {
          "co_argcount", "co_posonlyargcount", "co_kwonlyargcount", "co_nlocals",
          "co_stacksize", "co_flags", "co_code", "co_consts", "co_names",
          "co_varnames", "co_filename", "co_name", "co_qualname", "co_firstlineno",
          "co_linetable", "co_exceptiontable", "co_freevars", "co_cellvars"};
      for (uint32_t i = 0; i < std::size(code_members); ++i) {
        code_class->attrs[code_members[i]] = slot_descriptor("code", code_members[i], i);
      }
      code_class->has_descriptors = true;
      ++code_class->version;
    }
  }
  register_builtin_type(runtime, "frame", object_type);
  if (const auto* frame_value = runtime.find_builtin("frame")) {
    if (auto* frame_class = value_as_class(*frame_value)) {
      frame_class->attrs["f_locals"] = slot_descriptor("frame", "f_locals", 0);
      frame_class->attrs["f_globals"] = slot_descriptor("frame", "f_globals", 1);
      frame_class->attrs["f_code"] = slot_descriptor("frame", "f_code", 2);
      frame_class->attrs["f_back"] = slot_descriptor("frame", "f_back", 3);
      frame_class->has_descriptors = true;
      ++frame_class->version;
    }
  }
  register_builtin_type(runtime, "coroutine", object_type);
  register_builtin_type(runtime, "async_generator", object_type);
  register_builtin_type(runtime, "traceback", object_type);
  if (const auto* traceback_value = runtime.find_builtin("traceback")) {
    if (auto* traceback_class = value_as_class(*traceback_value)) {
      traceback_class->attrs["__new__"] =
          Value::native_function(0, "traceback.__new__", builtin_traceback_new);
      traceback_class->attrs["__init__"] =
          Value::native_function(0, "traceback.__init__", builtin_traceback_init);
      ++traceback_class->version;
    }
  }
  register_builtin_type(runtime, "cell", object_type);
  register_builtin_type(runtime, "file", object_type);
  if (const auto* file_value = runtime.find_builtin("file")) {
    if (auto* file_class = value_as_class(*file_value)) {
      file_class->attrs["name"] = slot_descriptor("file", "name", 0);
      file_class->attrs["mode"] = slot_descriptor("file", "mode", 1);
      file_class->attrs["closed"] = slot_descriptor("file", "closed", 2);
      file_class->attrs["encoding"] = slot_descriptor("file", "encoding", 3);
      file_class->attrs["errors"] = slot_descriptor("file", "errors", 4);
      file_class->attrs["newlines"] = slot_descriptor("file", "newlines", 5);
      for (auto& attr : file_class->attrs) {
        slot_descriptor_set_owner_class(attr.second, *file_value);
      }
      file_class->has_descriptors = true;
      ++file_class->version;
    }
  }
  register_builtin_type(runtime, "GenericAlias", object_type);
  if (const auto* generic_alias_value = runtime.find_builtin("GenericAlias")) {
    if (auto* generic_alias_class = value_as_class(*generic_alias_value)) {
      generic_alias_class->attrs["__module__"] = Value::string("types");
      generic_alias_class->attrs["__new__"] = Value::native_function(0, "types.GenericAlias.__new__", builtin_generic_alias_new);
      ++generic_alias_class->version;
    }
  }
  register_builtin_type(runtime, "type_parameter", object_type);
  register_builtin_type(runtime, "ellipsis", object_type);
  if (const auto* ellipsis_type = runtime.find_builtin("ellipsis")) {
    if (auto* ellipsis_class = value_as_class(*ellipsis_type)) {
      ellipsis_class->attrs["__reduce__"] = runtime.make_native_function(
          "ellipsis.__reduce__", builtin_singleton_reduce, const_cast<char*>("Ellipsis"));
      ++ellipsis_class->version;
    }
    Value ellipsis = Value::instance(*ellipsis_type);
    std::string ignored;
    object_set_attr(ellipsis, "__xlang3_string_value__", Value::string("Ellipsis"), ignored);
    runtime.register_builtin("Ellipsis", std::move(ellipsis));
  }
  register_builtin_type(runtime, "NotImplementedType", object_type);
  if (const auto* not_implemented_type = runtime.find_builtin("NotImplementedType")) {
    if (auto* not_implemented_class = value_as_class(*not_implemented_type)) {
      not_implemented_class->attrs["__reduce__"] = runtime.make_native_function(
          "NotImplementedType.__reduce__",
          builtin_singleton_reduce,
          const_cast<char*>("NotImplemented"));
      ++not_implemented_class->version;
    }
    Value not_implemented = Value::instance(*not_implemented_type);
    std::string ignored;
    object_set_attr(not_implemented, "__xlang3_string_value__", Value::string("NotImplemented"), ignored);
    runtime.register_builtin("NotImplemented", std::move(not_implemented));
  }

  runtime.register_native_builtin("id", builtin_id);
  runtime.register_native_builtin("isinstance", builtin_isinstance);
  runtime.register_native_builtin("issubclass", builtin_issubclass);
}

} // namespace xlang3
