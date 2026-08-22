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

#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

namespace {

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
      return "zip";
    case ObjectKind::MapIterator:
      return "map";
    case ObjectKind::FilterIterator:
      return "filter";
    case ObjectKind::Generator:
      return "generator";
    case ObjectKind::Module:
      return "module";
    case ObjectKind::Function:
    case ObjectKind::NativeFunction:
      return "function";
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
    case ObjectKind::Property:
      return "property";
    case ObjectKind::Cell:
      return "cell";
    case ObjectKind::File:
      return "file";
  }
  return "object";
}

const Value* find_builtin_type(Runtime& runtime, const char* name) {
  return name == nullptr ? nullptr : runtime.find_builtin(name);
}

bool class_tuple_matches(
    Runtime& runtime,
    const Value& actual_type,
    const Value& classinfo,
    bool subclass_check,
    bool& out,
    std::string& error) {
  if (auto* tuple = value_as_tuple(classinfo)) {
    for (const auto& item : tuple->items) {
      bool item_match = false;
      if (!class_tuple_matches(runtime, actual_type, item, subclass_check, item_match, error)) {
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
  if (!class_tuple_matches(runtime, actual_type, args[1], false, result, error)) {
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
  if (!class_tuple_matches(runtime, args[0], args[1], true, result, error)) {
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
  auto* namespace_dict = value_as_dict(args[3]);
  if (name == nullptr || bases == nullptr || namespace_dict == nullptr) {
    error = "type.__new__ expected str, tuple, and dict";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  std::vector<std::pair<std::string, Value>> attrs;
  attrs.reserve(namespace_dict->entries.size());
  for (const auto& entry : namespace_dict->entries) {
    auto* key = value_as_string(entry.first);
    if (key == nullptr) {
      error = "type.__new__ namespace keys must be strings";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    attrs.push_back({string_object_to_string(*key), entry.second});
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

  out = Value::class_object(string_object_to_string(*name), std::move(attrs), base);
  for (size_t i = 1; i < bases->items.size(); ++i) {
    if (!class_set_base(out, bases->items[i], error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return true;
}

void register_builtin_type(Runtime& runtime, const char* name, const Value& object_base) {
  runtime.register_builtin(name, Value::class_object(name, {}, object_base));
}

bool builtin_object_init(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "object.__init__ expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
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
    case ValueTag::Object:
      if (value.as.obj == nullptr) {
        break;
      }
      if (auto* instance = value_as_instance(value)) {
        value_assign_fast(out, instance->klass);
        return true;
      }
      if (const auto* type = find_builtin_type(runtime, builtin_type_name_for_kind(value.as.obj->kind))) {
        value_assign_fast(out, *type);
        return true;
      }
      break;
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
  object_attrs.push_back({"__new__", Value::native_function(0, "object.__new__", builtin_object_new)});
  object_attrs.push_back({"__init__", Value::native_function(0, "object.__init__", builtin_object_init)});
  object_attrs.push_back({"__getattribute__", Value::native_function(0, "object.__getattribute__", builtin_object_getattribute)});
  object_attrs.push_back({"__setattr__", Value::native_function(0, "object.__setattr__", builtin_object_setattr)});
  object_attrs.push_back({"__delattr__", Value::native_function(0, "object.__delattr__", builtin_object_delattr)});
  Value object_type = Value::class_object("object", std::move(object_attrs));
  runtime.register_builtin("object", object_type);

  Value type_type = Value::class_object(
      "type",
      {{"__new__", Value::native_function(0, "type.__new__", builtin_type_new)}},
      object_type);
  runtime.register_builtin("type", type_type);

  register_builtin_type(runtime, "NoneType", object_type);
  register_builtin_type(runtime, "int", object_type);
  const Value* int_type = runtime.find_builtin("int");
  register_builtin_type(runtime, "bool", int_type != nullptr ? *int_type : object_type);
  register_builtin_type(runtime, "float", object_type);
  register_builtin_type(runtime, "complex", object_type);
  register_builtin_type(runtime, "str", object_type);
  register_builtin_type(runtime, "bytes", object_type);
  register_builtin_type(runtime, "bytearray", object_type);
  register_builtin_type(runtime, "memoryview", object_type);
  register_builtin_type(runtime, "slice", object_type);
  register_builtin_type(runtime, "tuple", object_type);
  register_builtin_type(runtime, "list", object_type);
  register_builtin_type(runtime, "dict", object_type);
  register_builtin_type(runtime, "dict_keys", object_type);
  register_builtin_type(runtime, "dict_values", object_type);
  register_builtin_type(runtime, "dict_items", object_type);
  register_builtin_type(runtime, "set", object_type);
  register_builtin_type(runtime, "range", object_type);
  register_builtin_type(runtime, "iterator", object_type);
  register_builtin_type(runtime, "enumerate", object_type);
  register_builtin_type(runtime, "zip", object_type);
  register_builtin_type(runtime, "map", object_type);
  register_builtin_type(runtime, "filter", object_type);
  register_builtin_type(runtime, "generator", object_type);
  register_builtin_type(runtime, "module", object_type);
  register_builtin_type(runtime, "function", object_type);
  register_builtin_type(runtime, "method", object_type);
  register_builtin_type(runtime, "property", object_type);
  register_builtin_type(runtime, "classmethod", object_type);
  register_builtin_type(runtime, "staticmethod", object_type);
  register_builtin_type(runtime, "code", object_type);
  register_builtin_type(runtime, "frame", object_type);
  register_builtin_type(runtime, "traceback", object_type);
  register_builtin_type(runtime, "cell", object_type);
  register_builtin_type(runtime, "file", object_type);

  runtime.register_native_builtin("id", builtin_id);
  runtime.register_native_builtin("isinstance", builtin_isinstance);
  runtime.register_native_builtin("issubclass", builtin_issubclass);
}

} // namespace xlang3
