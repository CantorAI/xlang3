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
    case ObjectKind::Set:
      return "set";
    case ObjectKind::Range:
      return "range";
    case ObjectKind::RangeIterator:
    case ObjectKind::SequenceIterator:
    case ObjectKind::DictIterator:
    case ObjectKind::SetIterator:
      return "iterator";
    case ObjectKind::Generator:
      return "generator";
    case ObjectKind::Module:
      return "module";
    case ObjectKind::Function:
    case ObjectKind::NativeFunction:
      return "function";
    case ObjectKind::Class:
      return "type";
    case ObjectKind::Instance:
      return nullptr;
    case ObjectKind::BoundMethod:
      return "method";
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

void register_builtin_type(Runtime& runtime, const char* name, const Value& object_base) {
  runtime.register_builtin(name, Value::class_object(name, {}, object_base));
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
  Value object_type = Value::class_object("object", {});
  runtime.register_builtin("object", object_type);

  Value type_type = Value::class_object("type", {}, object_type);
  runtime.register_builtin("type", type_type);

  register_builtin_type(runtime, "NoneType", object_type);
  register_builtin_type(runtime, "int", object_type);
  const Value* int_type = runtime.find_builtin("int");
  register_builtin_type(runtime, "bool", int_type != nullptr ? *int_type : object_type);
  register_builtin_type(runtime, "float", object_type);
  register_builtin_type(runtime, "str", object_type);
  register_builtin_type(runtime, "bytes", object_type);
  register_builtin_type(runtime, "bytearray", object_type);
  register_builtin_type(runtime, "memoryview", object_type);
  register_builtin_type(runtime, "slice", object_type);
  register_builtin_type(runtime, "tuple", object_type);
  register_builtin_type(runtime, "list", object_type);
  register_builtin_type(runtime, "dict", object_type);
  register_builtin_type(runtime, "set", object_type);
  register_builtin_type(runtime, "range", object_type);
  register_builtin_type(runtime, "iterator", object_type);
  register_builtin_type(runtime, "generator", object_type);
  register_builtin_type(runtime, "module", object_type);
  register_builtin_type(runtime, "function", object_type);
  register_builtin_type(runtime, "method", object_type);
  register_builtin_type(runtime, "property", object_type);
  register_builtin_type(runtime, "cell", object_type);
  register_builtin_type(runtime, "file", object_type);

  runtime.register_native_builtin("id", builtin_id);
  runtime.register_native_builtin("isinstance", builtin_isinstance);
  runtime.register_native_builtin("issubclass", builtin_issubclass);
}

} // namespace xlang3
