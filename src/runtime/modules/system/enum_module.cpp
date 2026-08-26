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

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3 {

namespace {

bool enum_member_attr(const Value& member, const char* name, Value& out) {
  std::string ignored;
  return object_get_attr(member, name, out, ignored);
}

bool enum_member_value_int(const Value& member, int64_t& out) {
  Value value;
  if (!enum_member_attr(member, "value", value)) {
    return false;
  }
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  if (value.tag != ValueTag::Int64) {
    return false;
  }
  out = value.as.i64;
  return true;
}

bool enum_member_name(const Value& member, std::string& out) {
  Value name;
  if (!enum_member_attr(member, "name", name)) {
    return false;
  }
  auto* string = value_as_string(name);
  if (string == nullptr) {
    return false;
  }
  out = string_object_to_string(*string);
  return true;
}

bool enum_member_class(const Value& member, Value& out) {
  auto* instance = value_as_instance(member);
  if (instance == nullptr) {
    return false;
  }
  value_assign_fast(out, instance->klass);
  return value_as_class(out) != nullptr;
}

bool enum_member_type_name(const Value& member, std::string& out) {
  Value klass;
  if (!enum_member_class(member, klass)) {
    return false;
  }
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    return false;
  }
  out = klass_obj->name;
  return true;
}

bool enum_make_named_member(
    const Value& klass,
    const std::string& name,
    int64_t numeric_value,
    Value& out,
    std::string& error) {
  Value existing;
  if (class_try_enum_value_lookup(klass, Value::int64(numeric_value), existing)) {
    value_assign_fast(out, existing);
    return true;
  }
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    error = "enum result class is invalid";
    return false;
  }
  out = Value::instance(klass);
  auto* instance = value_as_instance(out);
  if (instance == nullptr) {
    error = "enum member allocation failed";
    return false;
  }
  instance->attrs.push_back({"name", Value::string(name)});
  instance->attrs.push_back({"value", Value::int64(numeric_value)});
  instance->attrs.push_back({"__xlang3_string_value__", Value::string(klass_obj->name + "." + name)});
  return true;
}

std::string flag_name_for_value(const Value& klass, int64_t numeric_value) {
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    return std::to_string(numeric_value);
  }
  auto it = klass_obj->attrs.find("_member_list_");
  if (it == klass_obj->attrs.end()) {
    return std::to_string(numeric_value);
  }
  auto* members = value_as_list(it->second);
  if (members == nullptr) {
    return std::to_string(numeric_value);
  }

  int64_t covered = 0;
  std::string name;
  for (const auto& member : members->items) {
    int64_t member_value = 0;
    std::string member_name;
    if (!enum_member_value_int(member, member_value) || !enum_member_name(member, member_name) || member_value == 0) {
      continue;
    }
    if ((numeric_value & member_value) == member_value) {
      if (!name.empty()) {
        name += "|";
      }
      name += member_name;
      covered |= member_value;
    }
  }
  if (!name.empty() && covered == numeric_value) {
    return name;
  }
  return std::to_string(numeric_value);
}

bool enum_str(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Enum.__str__() expected no arguments";
    return false;
  }
  std::string type_name;
  std::string member_name;
  if (!enum_member_type_name(args[0], type_name) || !enum_member_name(args[0], member_name)) {
    error = "Enum.__str__() called on non-enum member";
    return false;
  }
  out = Value::string(type_name + "." + member_name);
  return true;
}

bool enum_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Enum.__repr__() expected no arguments";
    return false;
  }
  std::string type_name;
  std::string member_name;
  int64_t numeric_value = 0;
  Value value;
  if (!enum_member_type_name(args[0], type_name) ||
      !enum_member_name(args[0], member_name) ||
      !enum_member_attr(args[0], "value", value)) {
    error = "Enum.__repr__() called on non-enum member";
    return false;
  }
  std::string value_text = value_to_string(value);
  if (enum_member_value_int(args[0], numeric_value)) {
    value_text = std::to_string(numeric_value);
  }
  out = Value::string("<" + type_name + "." + member_name + ": " + value_text + ">");
  return true;
}

enum class FlagBinaryOp : uintptr_t {
  And,
  Or,
  Xor,
};

bool flag_binary(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "Flag binary operation expected one argument";
    return false;
  }
  Value klass;
  if (!enum_member_class(args[0], klass)) {
    error = "Flag operation called on non-enum member";
    return false;
  }
  int64_t left = 0;
  int64_t right = 0;
  if (!enum_member_value_int(args[0], left)) {
    error = "Flag operation requires integer values";
    return false;
  }
  if (!enum_member_value_int(args[1], right)) {
    if (args[1].tag == ValueTag::Int64) {
      right = args[1].as.i64;
    } else if (args[1].tag == ValueTag::Bool) {
      right = args[1].as.b ? 1 : 0;
    } else {
      error = "Flag operation requires another flag or int";
      return false;
    }
  }

  const auto op = static_cast<FlagBinaryOp>(reinterpret_cast<uintptr_t>(user_data));
  int64_t result = 0;
  switch (op) {
    case FlagBinaryOp::And:
      result = left & right;
      break;
    case FlagBinaryOp::Or:
      result = left | right;
      break;
    case FlagBinaryOp::Xor:
      result = left ^ right;
      break;
  }
  return enum_make_named_member(klass, flag_name_for_value(klass, result), result, out, error);
}

bool flag_invert(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Flag.__invert__() expected no arguments";
    return false;
  }
  Value klass;
  if (!enum_member_class(args[0], klass)) {
    error = "Flag.__invert__() called on non-enum member";
    return false;
  }
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    error = "Flag result class is invalid";
    return false;
  }
  int64_t value = 0;
  if (!enum_member_value_int(args[0], value)) {
    error = "Flag.__invert__() requires integer values";
    return false;
  }
  int64_t mask = 0;
  auto it = klass_obj->attrs.find("_member_list_");
  if (it != klass_obj->attrs.end()) {
    if (auto* members = value_as_list(it->second)) {
      for (const auto& member : members->items) {
        int64_t member_value = 0;
        if (enum_member_value_int(member, member_value)) {
          mask |= member_value;
        }
      }
    }
  }
  const int64_t result = (~value) & mask;
  return enum_make_named_member(klass, flag_name_for_value(klass, result), result, out, error);
}

bool enum_identity(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "enum decorator expected an object";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool enum_unique(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "unique() expected an enum class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value members;
  std::string attr_error;
  if (object_lookup_class_attr(args[0], "__members__", members, attr_error)) {
    if (auto* dict = value_as_dict(members)) {
      for (const auto& entry : dict->entries) {
        Value member_name;
        std::string member_error;
        if (!object_get_attr(entry.second, "name", member_name, member_error)) {
          continue;
        }
        if (value_to_string(entry.first) != value_to_string(member_name)) {
          error = "duplicate values found in enum";
          runtime.raise_class_error("ValueError", error);
          return false;
        }
      }
    }
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool enum_auto(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "auto() expected no arguments";
    return false;
  }
  out = Value::instance(Value::class_object("_auto", {}));
  auto* instance = value_as_instance(out);
  if (instance == nullptr) {
    error = "auto() failed";
    return false;
  }
  instance->attrs.push_back({"__xlang3_enum_auto__", Value::boolean(true)});
  return true;
}

bool enum_global_str(Runtime&, const Value* args, uint32_t argc, Value& out, std::string&, void*) {
  if (argc >= 1) {
    out = Value::string(value_to_string(args[0]));
    return true;
  }
  out = Value::string("");
  return true;
}

Value enum_base_class(Runtime& runtime, const char* name, const char* builtin_base_name) {
  Value base = Value::invalid();
  if (const auto* builtin = runtime.find_builtin(builtin_base_name)) {
    value_assign_fast(base, *builtin);
  }
  return Value::class_object(
      name,
      {
          {"__module__", Value::string("enum")},
          {"__xlang3_enum_marker__", Value::boolean(true)},
          {"__str__", runtime.make_native_function(std::string("enum.") + name + ".__str__", enum_str)},
          {"__repr__", runtime.make_native_function(std::string("enum.") + name + ".__repr__", enum_repr)},
      },
      std::move(base));
}

void add_flag_methods(Runtime& runtime, Value& klass, const std::string& name) {
  std::string ignored;
  object_set_attr(klass, "__xlang3_flag_marker__", Value::boolean(true), ignored);
  object_set_attr(
      klass,
      "__and__",
      runtime.make_native_function("enum." + name + ".__and__", flag_binary, reinterpret_cast<void*>(static_cast<uintptr_t>(FlagBinaryOp::And))),
      ignored);
  object_set_attr(
      klass,
      "__or__",
      runtime.make_native_function("enum." + name + ".__or__", flag_binary, reinterpret_cast<void*>(static_cast<uintptr_t>(FlagBinaryOp::Or))),
      ignored);
  object_set_attr(
      klass,
      "__xor__",
      runtime.make_native_function("enum." + name + ".__xor__", flag_binary, reinterpret_cast<void*>(static_cast<uintptr_t>(FlagBinaryOp::Xor))),
      ignored);
  object_set_attr(klass, "__invert__", runtime.make_native_function("enum." + name + ".__invert__", flag_invert), ignored);
}

} // namespace

void register_enum_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "enum");

  Value enum_class = enum_base_class(runtime, "Enum", "object");
  Value int_enum_class = enum_base_class(runtime, "IntEnum", "int");
  Value int_flag_class = enum_base_class(runtime, "IntFlag", "int");
  Value str_enum_class = enum_base_class(runtime, "StrEnum", "str");
  Value flag_class = enum_base_class(runtime, "Flag", "int");
  Value repr_enum_class = enum_base_class(runtime, "ReprEnum", "object");
  Value enum_type_class = enum_base_class(runtime, "EnumType", "type");
  Value enum_dict_class = enum_base_class(runtime, "EnumDict", "dict");
  add_flag_methods(runtime, int_flag_class, "IntFlag");
  add_flag_methods(runtime, flag_class, "Flag");

  builder.value("Enum", enum_class)
      .value("EnumMeta", enum_type_class)
      .value("EnumType", enum_type_class)
      .value("EnumDict", enum_dict_class)
      .value("IntEnum", int_enum_class)
      .value("IntFlag", int_flag_class)
      .value("Flag", flag_class)
      .value("StrEnum", str_enum_class)
      .value("ReprEnum", repr_enum_class)
      .function("unique", enum_unique)
      .function("member", enum_identity)
      .function("nonmember", enum_identity)
      .function("global_enum", enum_identity)
      .function("global_flag_repr", enum_global_str)
      .function("global_enum_repr", enum_global_str)
      .function("global_str", enum_global_str)
      .function("pickle_by_global_name", enum_identity)
      .function("pickle_by_enum_name", enum_identity)
      .function("auto", enum_auto)
      .value("STRICT", Value::int64(0))
      .value("CONFORM", Value::int64(1))
      .value("EJECT", Value::int64(2))
      .value("KEEP", Value::int64(3))
      .value("CONTINUOUS", Value::int64(0))
      .value("NAMED_FLAGS", Value::int64(1))
      .value("UNIQUE", Value::int64(2))
      .value("EnumCheck", enum_base_class(runtime, "EnumCheck", "int"))
      .value("FlagBoundary", enum_base_class(runtime, "FlagBoundary", "int"))
      .value("__all__", Value::list({
                            Value::string("Enum"),
                            Value::string("EnumMeta"),
                            Value::string("EnumType"),
                            Value::string("IntEnum"),
                            Value::string("IntFlag"),
                            Value::string("Flag"),
                            Value::string("StrEnum"),
                            Value::string("ReprEnum"),
                            Value::string("auto"),
                            Value::string("unique"),
                        }));

  runtime.register_module("enum", builder.finish());
}

} // namespace xlang3
