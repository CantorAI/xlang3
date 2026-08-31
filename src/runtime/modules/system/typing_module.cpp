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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/value.h"

namespace xlang3 {

namespace {

bool typing_value_is_callable(const Value& value) {
  if (value_as_function(value) != nullptr ||
      value_as_native_function(value) != nullptr ||
      value_as_bound_method(value) != nullptr ||
      value_as_class(value) != nullptr) {
    return true;
  }
  Value call;
  std::string ignored;
  return object_get_attr(value, "__call__", call, ignored);
}

Value typing_no_default(Runtime& runtime) {
  if (const Value* value = runtime.find_builtin("NoDefault")) {
    return *value;
  }
  return Value::none();
}

bool kw_bool(const NativeKeywordArg* kwargs, uint32_t kwargc, const char* name, bool fallback) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string_view(kwargs[i].name) == name && kwargs[i].value != nullptr) {
      return value_truthy(*kwargs[i].value);
    }
  }
  return fallback;
}

Value kw_value(const NativeKeywordArg* kwargs, uint32_t kwargc, const char* name, Value fallback) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string_view(kwargs[i].name) == name && kwargs[i].value != nullptr) {
      return *kwargs[i].value;
    }
  }
  return fallback;
}

bool set_instance_attr(Value& self, const char* name, const Value& value, std::string& error) {
  return object_set_attr(self, name, value, error);
}

bool typing_idfunc(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_typing._idfunc expected one argument";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool type_parameter_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "type parameter __repr__ expected self";
    return false;
  }
  Value name;
  if (!object_get_attr(args[0], "__name__", name, error)) {
    return false;
  }
  std::string prefix;
  Value covariant;
  Value contravariant;
  std::string ignored;
  if (object_get_attr(args[0], "__covariant__", covariant, ignored) && value_truthy(covariant)) {
    prefix = "+";
  } else if (object_get_attr(args[0], "__contravariant__", contravariant, ignored) && value_truthy(contravariant)) {
    prefix = "-";
  }
  auto* text = value_as_string(name);
  out = Value::string(prefix + (text == nullptr ? std::string("?") : string_object_to_string(*text)));
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
    void*) {
  if (argc < 2) {
    error = "TypeVar expected a name";
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "TypeVar name must be a string";
    return false;
  }
  Value bound = kw_value(kwargs, kwargc, "bound", Value::none());
  Value default_value = kw_value(kwargs, kwargc, "default", typing_no_default(runtime));
  const bool covariant = kw_bool(kwargs, kwargc, "covariant", false);
  const bool contravariant = kw_bool(kwargs, kwargc, "contravariant", false);
  const bool infer_variance = kw_bool(kwargs, kwargc, "infer_variance", false);
  if (covariant && contravariant) {
    error = "Bivariant types are not supported.";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::vector<Value> constraints;
  for (uint32_t i = 2; i < argc; ++i) {
    constraints.push_back(args[i]);
  }
  if (!constraints.empty() && bound.tag != ValueTag::None) {
    error = "Constraints cannot be combined with bound=...";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value self = args[0];
  if (!set_instance_attr(self, "__name__", args[1], error) ||
      !set_instance_attr(self, "__module__", Value::string("typing"), error) ||
      !set_instance_attr(self, "__bound__", bound, error) ||
      !set_instance_attr(self, "__constraints__", Value::tuple(std::move(constraints)), error) ||
      !set_instance_attr(self, "__covariant__", Value::boolean(covariant), error) ||
      !set_instance_attr(self, "__contravariant__", Value::boolean(contravariant), error) ||
      !set_instance_attr(self, "__infer_variance__", Value::boolean(infer_variance), error) ||
      !set_instance_attr(self, "__default__", default_value, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool typevar_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return typevar_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool paramspec_args_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ParamSpecArgs expected origin";
    return false;
  }
  Value self = args[0];
  if (!set_instance_attr(self, "__origin__", args[1], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool paramspec_kwargs_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ParamSpecKwargs expected origin";
    return false;
  }
  Value self = args[0];
  if (!set_instance_attr(self, "__origin__", args[1], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool paramspec_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "ParamSpec expected a name";
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "ParamSpec name must be a string";
    return false;
  }
  Value self = args[0];
  Value default_value = kw_value(kwargs, kwargc, "default", typing_no_default(runtime));
  Value args_class = runtime.find_builtin("ParamSpecArgs") != nullptr ? *runtime.find_builtin("ParamSpecArgs") : Value::invalid();
  Value kwargs_class = runtime.find_builtin("ParamSpecKwargs") != nullptr ? *runtime.find_builtin("ParamSpecKwargs") : Value::invalid();
  Value args_instance = args_class.tag == ValueTag::Invalid ? Value::none() : Value::instance(args_class);
  Value kwargs_instance = kwargs_class.tag == ValueTag::Invalid ? Value::none() : Value::instance(kwargs_class);
  if (args_instance.tag != ValueTag::None) {
    Value ps_args[] = {args_instance, self};
    Value ignored;
    if (!paramspec_args_init(runtime, ps_args, 2, ignored, error, nullptr)) {
      return false;
    }
  }
  if (kwargs_instance.tag != ValueTag::None) {
    Value ps_kwargs[] = {kwargs_instance, self};
    Value ignored;
    if (!paramspec_kwargs_init(runtime, ps_kwargs, 2, ignored, error, nullptr)) {
      return false;
    }
  }
  if (!set_instance_attr(self, "__name__", args[1], error) ||
      !set_instance_attr(self, "__module__", Value::string("typing"), error) ||
      !set_instance_attr(self, "__bound__", Value::none(), error) ||
      !set_instance_attr(self, "__constraints__", Value::tuple({}), error) ||
      !set_instance_attr(self, "__covariant__", Value::boolean(false), error) ||
      !set_instance_attr(self, "__contravariant__", Value::boolean(false), error) ||
      !set_instance_attr(self, "__infer_variance__", Value::boolean(false), error) ||
      !set_instance_attr(self, "__default__", default_value, error) ||
      !set_instance_attr(self, "args", args_instance, error) ||
      !set_instance_attr(self, "kwargs", kwargs_instance, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool paramspec_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return paramspec_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool typevartuple_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "TypeVarTuple expected a name";
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "TypeVarTuple name must be a string";
    return false;
  }
  Value self = args[0];
  Value default_value = kw_value(kwargs, kwargc, "default", typing_no_default(runtime));
  if (!set_instance_attr(self, "__name__", args[1], error) ||
      !set_instance_attr(self, "__module__", Value::string("typing"), error) ||
      !set_instance_attr(self, "__default__", default_value, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool typevartuple_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return typevartuple_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool type_alias_type_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 4) {
    error = "TypeAliasType expected name, value, and optional type_params";
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "TypeAliasType name must be a string";
    return false;
  }
  Value self = args[0];
  Value params = argc == 4 ? args[3] : Value::tuple({});
  if (!set_instance_attr(self, "__name__", args[1], error) ||
      !set_instance_attr(self, "__module__", Value::string("typing"), error) ||
      !set_instance_attr(self, "__value__", args[2], error) ||
      !set_instance_attr(self, "__type_params__", params, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool no_default_repr(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "NoDefault.__repr__ expected self";
    return false;
  }
  out = Value::string("typing.NoDefault");
  return true;
}

Value make_typing_class(Runtime& runtime,
                        const char* name,
                        NativeFunctionCallback init = nullptr,
                        NativeKeywordFunctionCallback init_kw = nullptr) {
  Value base = runtime.find_builtin("object") != nullptr ? *runtime.find_builtin("object") : Value::invalid();
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("typing")});
  attrs.push_back({"__qualname__", Value::string(name)});
  if (init != nullptr) {
    attrs.push_back({"__init__", runtime.make_native_function(
        std::string("_typing.") + name + ".__init__",
        init,
        nullptr,
        nullptr,
        nullptr,
        false,
        init_kw)});
  }
  const std::string_view class_name(name);
  if (class_name == "TypeVar" || class_name == "ParamSpec" || class_name == "TypeVarTuple") {
    attrs.push_back({"__repr__", runtime.make_native_function(std::string("_typing.") + name + ".__repr__", type_parameter_repr)});
  }
  return Value::class_object(name, std::move(attrs), std::move(base));
}

} // namespace

void register_typing_module(Runtime& runtime) {
  Value no_default_type = make_typing_class(runtime, "NoDefaultType");
  if (auto* klass = value_as_class(no_default_type)) {
    klass->attrs["__repr__"] = runtime.make_native_function("_typing.NoDefaultType.__repr__", no_default_repr);
    klass->attrs["__str__"] = runtime.make_native_function("_typing.NoDefaultType.__repr__", no_default_repr);
    ++klass->version;
  }
  Value no_default = Value::instance(no_default_type);
  runtime.register_builtin("NoDefault", no_default);

  Value typevar = make_typing_class(runtime, "TypeVar", typevar_init, typevar_init_kw);
  Value paramspec_args = make_typing_class(runtime, "ParamSpecArgs", paramspec_args_init);
  Value paramspec_kwargs = make_typing_class(runtime, "ParamSpecKwargs", paramspec_kwargs_init);
  runtime.register_builtin("ParamSpecArgs", paramspec_args);
  runtime.register_builtin("ParamSpecKwargs", paramspec_kwargs);
  Value paramspec = make_typing_class(runtime, "ParamSpec", paramspec_init, paramspec_init_kw);
  Value typevartuple = make_typing_class(runtime, "TypeVarTuple", typevartuple_init, typevartuple_init_kw);
  Value type_alias_type = make_typing_class(runtime, "TypeAliasType", type_alias_type_init);
  Value generic = make_typing_class(runtime, "Generic");
  Value union_class = make_typing_class(runtime, "Union");

  runtime.register_builtin("TypeVar", typevar);
  runtime.register_builtin("ParamSpec", paramspec);
  runtime.register_builtin("TypeVarTuple", typevartuple);
  runtime.register_builtin("TypeAliasType", type_alias_type);
  runtime.register_builtin("Generic", generic);
  runtime.register_builtin("Union", union_class);

  NativeModuleBuilder builder(runtime, "_typing");
  builder.function("_idfunc", typing_idfunc);
  builder.value("TypeVar", typevar);
  builder.value("ParamSpec", paramspec);
  builder.value("TypeVarTuple", typevartuple);
  builder.value("ParamSpecArgs", paramspec_args);
  builder.value("ParamSpecKwargs", paramspec_kwargs);
  builder.value("TypeAliasType", type_alias_type);
  builder.value("Generic", generic);
  builder.value("Union", union_class);
  builder.value("NoDefault", no_default);
  runtime.register_module("_typing", builder.finish());
}

} // namespace xlang3
