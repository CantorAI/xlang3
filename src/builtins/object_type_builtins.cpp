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

#include "xlang3/builtin_methods.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cstdint>
#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

Value make_dict_fromkeys_classmethod();

namespace {

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
    if (!object_get_attr(attr.second, "__set_name__", set_name, attr_error)) {
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
  return object_get_attr(value, "__isabstractmethod__", marker, ignored) && value_truthy(marker);
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
  if (!object_get_attr(classinfo, hook_name, hook, hook_error)) {
    return true;
  }

  Value function_value;
  std::vector<Value> leading_args;
  if (auto* bound = value_as_bound_method(hook)) {
    value_assign_fast(function_value, bound->function);
    leading_args.push_back(bound->self);
  } else {
    value_assign_fast(function_value, hook);
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
  out = Value::string(object_model_to_string(args[0]));
  return true;
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
  out = Value::tuple({object_type, Value::tuple({})});
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
  return builtin_object_reduce(runtime, args, 1, out, error, nullptr);
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
    std::string_view bytes_view;
    if (auto* bytes = value_as_bytes(args[1])) {
      bytes_view = bytes_object_view(*bytes);
    } else if (auto* bytearray = value_as_bytearray(args[1])) {
      bytes_view = std::string_view(bytearray->value.data(), bytearray->value.size());
    } else {
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
    std::string encoding = string_object_to_string(*encoding_string);
    std::transform(encoding.begin(), encoding.end(), encoding.begin(), [](unsigned char ch) {
      return ch == '-' ? '_' : static_cast<char>(std::tolower(ch));
    });
    if (encoding == "latin1") {
      encoding = "latin_1";
    }
    if (encoding != "latin_1" && encoding != "utf_8" && encoding != "utf8" && encoding != "ascii") {
      error = "only utf-8/ascii/latin-1 encoding is supported";
      runtime.raise_class_error("LookupError", error);
      return false;
    }
    if (encoding == "latin_1") {
      std::string decoded;
      decoded.reserve(bytes_view.size() * 2);
      for (unsigned char ch : bytes_view) {
        if (ch <= 0x7f) {
          decoded.push_back(static_cast<char>(ch));
        } else if (ch <= 0x7ff) {
          decoded.push_back(static_cast<char>(0xc0 | (ch >> 6)));
          decoded.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
        }
      }
      out = Value::string(std::move(decoded));
    } else {
      out = Value::string(std::string(bytes_view.data(), bytes_view.size()));
    }
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
  int64_t parsed = 0;
  if (argc == 1) {
    parsed = 0;
  } else if (args[1].tag == ValueTag::Int64) {
    parsed = args[1].as.i64;
  } else if (args[1].tag == ValueTag::Bool) {
    parsed = args[1].as.b ? 1 : 0;
  } else if (args[1].tag == ValueTag::Double) {
    parsed = static_cast<int64_t>(args[1].as.f64);
  } else if (auto* text = value_as_string(args[1])) {
    try {
      parsed = std::stoll(string_object_to_string(*text), nullptr, base);
    } catch (const std::exception&) {
      error = "invalid literal for int()";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  } else if (auto* bytes = value_as_bytes(args[1])) {
    try {
      const auto view = bytes_object_view(*bytes);
      parsed = std::stoll(std::string(view.data(), view.size()), nullptr, base);
    } catch (const std::exception&) {
      error = "invalid literal for int()";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  } else if (auto* bytearray = value_as_bytearray(args[1])) {
    try {
      parsed = std::stoll(bytearray->value, nullptr, base);
    } catch (const std::exception&) {
      error = "invalid literal for int()";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  } else {
    error = "int.__new__ value must be a string, bytes-like object, number, or bool";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  const Value* builtin_int = runtime.find_builtin("int");
  if (builtin_int != nullptr && value_as_class(*builtin_int) == klass) {
    out = Value::int64(parsed);
    return true;
  }
  out = Value::instance(args[0]);
  std::string ignored;
  (void)object_set_attr(out, "__xlang3_int_value__", Value::int64(parsed), ignored);
  return true;
}

bool builtin_type_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
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
    attrs.push_back({key_name, entry.second});
  }
  if (has_explicit_slots) {
    for (const auto& slot : explicit_slots) {
      for (const auto& attr : attrs) {
        if (attr.first == slot) {
          error = "'" + slot + "' in __slots__ conflicts with class variable";
          runtime.raise_class_error("ValueError", error);
          return false;
        }
      }
    }
  }

  Value base = Value::invalid();
  if (bases->items.empty()) {
    if (const auto* object_type = runtime.find_builtin("object")) {
      value_assign_fast(base, *object_type);
    }
  } else {
    for (const auto& item : bases->items) {
      if (value_as_class(item) == nullptr) {
        error = "type.__new__ bases must be classes";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    }
    value_assign_fast(base, bases->items[0]);
  }

  std::string class_name = string_object_to_string(*name);
  if (!class_attrs_have(attrs, "__module__")) {
    attrs.push_back({"__module__", Value::string(current_module_name(runtime))});
  }
  if (!class_attrs_have(attrs, "__qualname__")) {
    attrs.push_back({"__qualname__", Value::string(class_name)});
  }

  auto descriptor_attrs = attrs;
  out = Value::class_object(class_name, std::move(attrs), base, {}, args[0]);
  for (size_t i = 1; i < bases->items.size(); ++i) {
    if (!class_set_base(out, bases->items[i], error)) {
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

void register_builtin_type(Runtime& runtime, const char* name, const Value& object_base) {
  Value metaclass = Value::invalid();
  if (const auto* type_type = runtime.find_builtin("type")) {
    value_assign_fast(metaclass, *type_type);
  }
  runtime.register_builtin(
      name,
      Value::class_object(
          name,
          {{"__module__", Value::string("builtins")}, {"__qualname__", Value::string(name)}},
          object_base,
          {},
          std::move(metaclass)));
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
  value_set_bool(out, value_is(args[0], args[1]));
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
  value_set_bool(out, !value_is(args[0], args[1]));
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
  if (!object_get_attr(args[0], string_object_to_string(*name), out, error)) {
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
      if (auto* klass = value_as_class(value)) {
        if (klass->metaclass.tag != ValueTag::Invalid) {
          value_assign_fast(out, klass->metaclass);
          return true;
        }
      }
      const char* type_name = builtin_type_name_for_kind(value.as.obj->kind);
      if (auto* bound = value_as_bound_method(value)) {
        if (value_as_native_function(bound->function) != nullptr) {
          type_name = "builtin_function_or_method";
        }
      }
      if (value.as.obj->kind == ObjectKind::Set) {
        if (auto* set = value_as_set(value); set != nullptr && set->frozen) {
          type_name = "frozenset";
        }
      }
      if (const auto* type = find_builtin_type(runtime, type_name)) {
        value_assign_fast(out, *type);
        return true;
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

void register_object_type_builtins(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> object_attrs;
  object_attrs.push_back({"__module__", Value::string("builtins")});
  object_attrs.push_back({"__qualname__", Value::string("object")});
  object_attrs.push_back({"__new__", Value::native_function(0, "object.__new__", builtin_object_new)});
  object_attrs.push_back({"__init__", Value::native_function(0, "object.__init__", builtin_object_init)});
  object_attrs.push_back({"__eq__", Value::native_function(0, "object.__eq__", builtin_object_eq)});
  object_attrs.push_back({"__ne__", Value::native_function(0, "object.__ne__", builtin_object_ne)});
  object_attrs.push_back({"__repr__", Value::native_function(0, "object.__repr__", builtin_object_repr)});
  object_attrs.push_back({"__str__", Value::native_function(0, "object.__str__", builtin_object_repr)});
  object_attrs.push_back({"__format__", Value::native_function(0, "object.__format__", builtin_object_format)});
  object_attrs.push_back({"__reduce__", Value::native_function(0, "object.__reduce__", builtin_object_reduce)});
  object_attrs.push_back({"__reduce_ex__", Value::native_function(0, "object.__reduce_ex__", builtin_object_reduce_ex)});
  object_attrs.push_back({"__getattribute__", Value::native_function(0, "object.__getattribute__", builtin_object_getattribute)});
  object_attrs.push_back({"__setattr__", Value::native_function(0, "object.__setattr__", builtin_object_setattr)});
  object_attrs.push_back({"__delattr__", Value::native_function(0, "object.__delattr__", builtin_object_delattr)});
  Value object_type = Value::class_object("object", std::move(object_attrs));
  runtime.register_builtin("object", object_type);

  Value type_type = Value::class_object(
      "type",
      {
          {"__module__", Value::string("builtins")},
          {"__qualname__", Value::string("type")},
          {"__new__", Value::native_function(0, "type.__new__", builtin_type_new)},
          {"__prepare__", Value::native_function(0, "type.__prepare__", builtin_type_prepare)},
      },
      object_type);
  if (auto* object_class = value_as_class(object_type)) {
    value_assign_fast(object_class->metaclass, type_type);
  }
  if (auto* type_class = value_as_class(type_type)) {
    value_assign_fast(type_class->metaclass, type_type);
  }
  runtime.register_builtin("type", type_type);

  register_builtin_type(runtime, "NoneType", object_type);
  register_builtin_type(runtime, "int", object_type);
  if (const auto* int_value = runtime.find_builtin("int")) {
    if (auto* int_class = value_as_class(*int_value)) {
      int_class->attrs["__new__"] = Value::native_function(0, "int.__new__", builtin_int_new);
      ++int_class->version;
    }
  }
  const Value* int_type = runtime.find_builtin("int");
  register_builtin_type(runtime, "bool", int_type != nullptr ? *int_type : object_type);
  register_builtin_type(runtime, "float", object_type);
  register_builtin_type(runtime, "complex", object_type);
  register_builtin_type(runtime, "str", object_type);
  if (const auto* str_value = runtime.find_builtin("str")) {
    if (auto* str_class = value_as_class(*str_value)) {
      str_class->attrs["__new__"] = Value::native_function(0, "str.__new__", builtin_str_new);
      string_install_class_methods(runtime, *str_class);
    }
  }
  register_builtin_type(runtime, "bytes", object_type);
  register_builtin_type(runtime, "bytearray", object_type);
  register_builtin_type(runtime, "memoryview", object_type);
  register_builtin_type(runtime, "slice", object_type);
  register_builtin_type(runtime, "tuple", object_type);
  register_builtin_type(runtime, "list", object_type);
  register_builtin_type(runtime, "dict", object_type);
  if (const auto* dict_value = runtime.find_builtin("dict")) {
    if (auto* dict_class = value_as_class(*dict_value)) {
      dict_class->attrs["__init__"] =
          runtime.make_native_function("dict.__init__", builtin_dict_init, nullptr, nullptr, nullptr, false, builtin_dict_init_kw);
      dict_class->attrs["fromkeys"] = make_dict_fromkeys_classmethod();
      dict_install_class_methods(runtime, *dict_class);
      ++dict_class->version;
    }
  }
  register_builtin_type(runtime, "dict_keys", object_type);
  register_builtin_type(runtime, "dict_values", object_type);
  register_builtin_type(runtime, "dict_items", object_type);
  register_builtin_type(runtime, "set", object_type);
  register_builtin_type(runtime, "frozenset", object_type);
  register_builtin_type(runtime, "range", object_type);
  register_builtin_type(runtime, "iterator", object_type);
  register_builtin_type(runtime, "enumerate", object_type);
  register_builtin_type(runtime, "zip", object_type);
  register_builtin_type(runtime, "map", object_type);
  register_builtin_type(runtime, "filter", object_type);
  register_builtin_type(runtime, "generator", object_type);
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
      function_class->has_descriptors = true;
      ++function_class->version;
    }
  }
  register_builtin_type(runtime, "builtin_function_or_method", object_type);
  register_builtin_type(runtime, "method", object_type);
  if (const auto* method_value = runtime.find_builtin("method")) {
    if (auto* method_class = value_as_class(*method_value)) {
      method_class->attrs["__new__"] = Value::native_function(0, "method.__new__", builtin_method_new);
      ++method_class->version;
    }
  }
  register_builtin_type(runtime, "member_descriptor", object_type);
  register_builtin_type(runtime, "property", object_type);
  if (const auto* property_value = runtime.find_builtin("property")) {
    if (auto* property_class = value_as_class(*property_value)) {
      property_install_class_methods(runtime, *property_class);
    }
  }
  register_builtin_type(runtime, "classmethod", object_type);
  register_builtin_type(runtime, "staticmethod", object_type);
  register_builtin_type(runtime, "code", object_type);
  register_builtin_type(runtime, "frame", object_type);
  register_builtin_type(runtime, "traceback", object_type);
  register_builtin_type(runtime, "cell", object_type);
  register_builtin_type(runtime, "file", object_type);
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
    Value ellipsis = Value::instance(*ellipsis_type);
    std::string ignored;
    object_set_attr(ellipsis, "__xlang3_string_value__", Value::string("Ellipsis"), ignored);
    runtime.register_builtin("Ellipsis", std::move(ellipsis));
  }
  register_builtin_type(runtime, "NotImplementedType", object_type);
  if (const auto* not_implemented_type = runtime.find_builtin("NotImplementedType")) {
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
