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

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

bool enum_identity(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "enum decorator expected an object";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool enum_auto(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "auto() expected no arguments";
    return false;
  }
  value_set_int64(out, 0);
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
  return Value::class_object(name, {{"__module__", Value::string("enum")}}, std::move(base));
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

  builder.value("Enum", enum_class)
      .value("EnumMeta", enum_type_class)
      .value("EnumType", enum_type_class)
      .value("EnumDict", enum_dict_class)
      .value("IntEnum", int_enum_class)
      .value("IntFlag", int_flag_class)
      .value("Flag", flag_class)
      .value("StrEnum", str_enum_class)
      .value("ReprEnum", repr_enum_class)
      .function("unique", enum_identity)
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
