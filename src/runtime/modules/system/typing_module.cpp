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

#include <string>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kTypingAliasNativeType = "typing._Alias";
constexpr const char* kTypingTypeVarNativeType = "typing.TypeVar";

bool typing_alias_getitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "typing alias expected one subscript";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool typing_class_getitem(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "typing class expected one subscript";
    return false;
  }
  auto* klass = static_cast<Value*>(user_data);
  if (klass == nullptr) {
    error = "invalid typing class";
    return false;
  }
  value_assign_fast(out, *klass);
  return true;
}

bool typing_identity_decorator(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "typing decorator expected one function";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

Value make_alias(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("typing")});
  attrs.push_back({"__getitem__", runtime.make_native_function("typing._Alias.__getitem__", typing_alias_getitem)});
  Value klass = Value::class_object("_Alias", std::move(attrs));
  Value instance = Value::instance(klass);
  std::string error;
  object_set_attr(instance, "__name__", Value::string(name), error);
  instance_set_native_data(instance, kTypingAliasNativeType, reinterpret_cast<void*>(1), nullptr, error);
  return instance;
}

Value make_typing_marker_class(Runtime& runtime, const char* name) {
  Value klass = Value::class_object(name, {});
  auto* class_data = new Value(klass);
  std::string error;
  object_set_attr(
      klass,
      "__getitem__",
      runtime.make_native_function(
          std::string("typing.") + name + ".__getitem__",
          typing_class_getitem,
          class_data,
          [](void* data) { delete static_cast<Value*>(data); }),
      error);
  return klass;
}

bool type_check_only(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "type_check_only() expected optional function";
    return false;
  }
  if (argc == 1) {
    value_assign_fast(out, args[0]);
    return true;
  }
  out = runtime.make_native_function("typing.type_check_only.<decorator>", typing_identity_decorator);
  return true;
}

bool typevar_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "TypeVar() expected a name";
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "TypeVar() name must be a string";
    return false;
  }
  std::vector<Value> constraints;
  constraints.reserve(argc > 2 ? argc - 2 : 0);
  for (uint32_t i = 2; i < argc; ++i) {
    constraints.push_back(args[i]);
  }
  Value& self = const_cast<Value&>(args[0]);
  object_set_attr(self, "__name__", Value::string(string_object_to_string(*name)), error);
  object_set_attr(self, "__bound__", Value::none(), error);
  object_set_attr(self, "__constraints__", Value::tuple(std::move(constraints)), error);
  object_set_attr(self, "__default__", Value::none(), error);
  object_set_attr(self, "__covariant__", Value::boolean(false), error);
  object_set_attr(self, "__contravariant__", Value::boolean(false), error);
  instance_set_native_data(self, kTypingTypeVarNativeType, reinterpret_cast<void*>(1), nullptr, error);
  value_set_none(out);
  return true;
}

bool typevar_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!typevar_init(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      error = "TypeVar() received invalid keyword argument";
      return false;
    }
    if (name == "bound") {
      object_set_attr(self, "__bound__", *kwargs[i].value, error);
    } else if (name == "default") {
      object_set_attr(self, "__default__", *kwargs[i].value, error);
    } else if (name == "covariant") {
      object_set_attr(self, "__covariant__", Value::boolean(value_truthy(*kwargs[i].value)), error);
    } else if (name == "contravariant") {
      object_set_attr(self, "__contravariant__", Value::boolean(value_truthy(*kwargs[i].value)), error);
    } else {
      error = "TypeVar() got an unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  return true;
}

bool newtype_call(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "NewType callable expected one value";
    return false;
  }
  value_assign_fast(out, args[1]);
  return true;
}

bool newtype_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "NewType() expected name and supertype";
    return false;
  }
  auto* name = value_as_string(args[0]);
  if (name == nullptr) {
    error = "NewType() name must be a string";
    return false;
  }
  std::string type_name = string_object_to_string(*name);
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__call__", runtime.make_native_function("typing.NewType.__call__", newtype_call)});
  Value klass = Value::class_object("NewType", std::move(attrs));
  out = Value::instance(klass);
  object_set_attr(out, "__name__", Value::string(type_name), error);
  object_set_attr(out, "__supertype__", args[1], error);
  object_set_attr(out, "__module__", Value::string("typing"), error);
  return true;
}

bool identity_decorator_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "typing decorator expected one object";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool cast_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "cast() expected type and value";
    return false;
  }
  value_assign_fast(out, args[1]);
  return true;
}

Value make_typevar_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("typing.TypeVar.__init__", typevar_init, nullptr, nullptr, nullptr, false, typevar_init_kw)});
  return Value::class_object("TypeVar", std::move(attrs));
}

} // namespace

void register_typing_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "typing");
  builder.value("Any", make_alias(runtime, "Any"))
      .value("Annotated", make_alias(runtime, "Annotated"))
      .value("ClassVar", make_alias(runtime, "ClassVar"))
      .value("Literal", make_alias(runtime, "Literal"))
      .value("Final", make_alias(runtime, "Final"))
      .value("Optional", make_alias(runtime, "Optional"))
      .value("Tuple", make_alias(runtime, "Tuple"))
      .value("TextIO", make_alias(runtime, "TextIO"))
      .value("Union", make_alias(runtime, "Union"))
      .value("Callable", make_alias(runtime, "Callable"))
      .value("Dict", make_alias(runtime, "Dict"))
      .value("List", make_alias(runtime, "List"))
      .value("Set", make_alias(runtime, "Set"))
      .value("FrozenSet", make_alias(runtime, "FrozenSet"))
      .value("Iterable", make_alias(runtime, "Iterable"))
      .value("Iterator", make_alias(runtime, "Iterator"))
      .value("Sequence", make_alias(runtime, "Sequence"))
      .value("Mapping", make_alias(runtime, "Mapping"))
      .value("MutableMapping", make_alias(runtime, "MutableMapping"))
      .value("MutableSequence", make_alias(runtime, "MutableSequence"))
      .value("NoReturn", make_alias(runtime, "NoReturn"))
      .value("Self", make_alias(runtime, "Self"))
      .value("Type", make_alias(runtime, "Type"))
      .value("Generic", make_typing_marker_class(runtime, "Generic"))
      .value("Protocol", make_typing_marker_class(runtime, "Protocol"))
      .value("TypeVar", make_typevar_class(runtime))
      .value("TYPE_CHECKING", Value::boolean(false))
      .function("NewType", newtype_entry)
      .function("cast", cast_entry)
      .function("final", identity_decorator_entry)
      .function("no_type_check", identity_decorator_entry)
      .function("override", identity_decorator_entry)
      .function("runtime_checkable", identity_decorator_entry)
      .function("type_check_only", type_check_only);
  runtime.register_module("typing", builder.finish());
}

} // namespace xlang3
