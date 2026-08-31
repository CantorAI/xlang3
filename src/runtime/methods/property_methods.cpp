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
#include "xlang3/builtin_methods.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

namespace xlang3 {

namespace {

std::string property_receiver_type_name(const Value& value) {
  switch (value.tag) {
    case ValueTag::None:
      return "NoneType";
    case ValueTag::Bool:
      return "bool";
    case ValueTag::Int64:
      return "int";
    case ValueTag::Double:
      return "float";
    case ValueTag::Object:
      if (auto* klass = value_as_class(value)) {
        return klass->name;
      }
      if (auto* instance = value_as_instance(value)) {
        if (auto* klass = value_as_class(instance->klass)) {
          return klass->name;
        }
      }
      break;
    case ValueTag::Invalid:
      break;
  }
  return value_to_string(value);
}

bool property_method_argc_error(Runtime& runtime, const char* method, uint32_t argc, std::string& error) {
  if (argc == 0) {
    error = "unbound method property." + std::string(method) + "() needs an argument";
  } else {
    error = "property." + std::string(method) + "() takes exactly one argument (" + std::to_string(argc - 1) + " given)";
  }
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool property_method_receiver_error(Runtime& runtime, const Value& self, const char* method, std::string& error) {
  error = "descriptor '" + std::string(method) + "' for 'property' objects doesn't apply to a '" +
      property_receiver_type_name(self) + "' object";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool property_method_keyword_error(Runtime& runtime, const char* method, std::string& error) {
  error = "property." + std::string(method) + "() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool property_instance_get_field(const Value& object, const char* name, Value& out) {
  auto* instance = value_as_instance(object);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  if (klass == nullptr || !class_has_builtin_base_name(klass, "property")) {
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

bool property_get_field(const Value& object, const char* name, Value& out) {
  if (auto* property = value_as_property(object)) {
    if (std::string_view(name) == "fget") {
      value_assign_fast(out, property->fget);
      return true;
    }
    if (std::string_view(name) == "fset") {
      value_assign_fast(out, property->fset);
      return true;
    }
    if (std::string_view(name) == "fdel") {
      value_assign_fast(out, property->fdel);
      return true;
    }
    if (std::string_view(name) == "__doc__") {
      value_assign_fast(out, property->doc);
      return true;
    }
  }
  return property_instance_get_field(object, name, out);
}

bool property_bool_field(const Value& object, const char* name) {
  if (auto* property = value_as_property(object)) {
    if (std::string_view(name) == "__xlang3_doc_from_getter__") {
      return property->doc_from_getter;
    }
    if (std::string_view(name) == "__xlang3_name_from_getter__") {
      return property->name_from_getter;
    }
  }
  Value value;
  return property_instance_get_field(object, name, value) && value_truthy(value);
}

Value property_callable_attr_or_none(const Value& callable, const char* name) {
  Value result;
  if (callable.tag != ValueTag::None && callable.tag != ValueTag::Invalid) {
    std::string ignored;
    if (object_get_attr(callable, name, result, ignored)) {
      return result;
    }
  }
  return Value::none();
}

bool property_clone_from(
    const Value& source,
    Value fget,
    Value fset,
    Value fdel,
    Value explicit_doc,
    Value& out) {
  Value doc;
  if (explicit_doc.tag != ValueTag::Invalid) {
    value_assign_fast(doc, explicit_doc);
  } else if (property_bool_field(source, "__xlang3_doc_from_getter__")) {
    doc = property_callable_attr_or_none(fget, "__doc__");
  } else if (!property_get_field(source, "__doc__", doc)) {
    value_set_none(doc);
  }
  const bool doc_from_getter =
      explicit_doc.tag == ValueTag::Invalid && property_bool_field(source, "__xlang3_doc_from_getter__");
  out = Value::property(std::move(fget), std::move(fset), std::move(fdel), std::move(doc), false, doc_from_getter);
  if (auto* cloned = value_as_property(out)) {
    Value name;
    if (property_bool_field(source, "__xlang3_name_from_getter__")) {
      name = property_callable_attr_or_none(cloned->fget, "__name__");
      if (name.tag != ValueTag::None && name.tag != ValueTag::Invalid) {
        value_assign_fast(cloned->name, name);
        cloned->has_name = true;
        cloned->name_from_getter = true;
      }
    } else if (property_get_field(source, "__name__", name)) {
      value_assign_fast(cloned->name, name);
      cloned->has_name = true;
      cloned->name_from_getter = false;
    }
  }
  return true;
}

bool property_getter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return property_method_argc_error(runtime, "getter", argc, error);
  }
  Value fset;
  Value fdel;
  if (!property_get_field(args[0], "fset", fset) || !property_get_field(args[0], "fdel", fdel)) {
    return property_method_receiver_error(runtime, args[0], "getter", error);
  }
  Value invalid_doc;
  value_set_invalid(invalid_doc);
  return property_clone_from(args[0], args[1], std::move(fset), std::move(fdel), std::move(invalid_doc), out);
}

bool property_getter_kw(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t, Value&, std::string& error, void*) {
  return property_method_keyword_error(runtime, "getter", error);
}

bool property_setter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return property_method_argc_error(runtime, "setter", argc, error);
  }
  Value fget;
  Value fdel;
  if (!property_get_field(args[0], "fget", fget) || !property_get_field(args[0], "fdel", fdel)) {
    return property_method_receiver_error(runtime, args[0], "setter", error);
  }
  Value invalid_doc;
  value_set_invalid(invalid_doc);
  return property_clone_from(args[0], std::move(fget), args[1], std::move(fdel), std::move(invalid_doc), out);
}

bool property_setter_kw(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t, Value&, std::string& error, void*) {
  return property_method_keyword_error(runtime, "setter", error);
}

bool property_deleter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return property_method_argc_error(runtime, "deleter", argc, error);
  }
  Value fget;
  Value fset;
  if (!property_get_field(args[0], "fget", fget) || !property_get_field(args[0], "fset", fset)) {
    return property_method_receiver_error(runtime, args[0], "deleter", error);
  }
  Value invalid_doc;
  value_set_invalid(invalid_doc);
  return property_clone_from(args[0], std::move(fget), std::move(fset), args[1], std::move(invalid_doc), out);
}

bool property_deleter_kw(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t, Value&, std::string& error, void*) {
  return property_method_keyword_error(runtime, "deleter", error);
}

bool property_descriptor_get_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "property.__get__ expected 1 or 2 arguments";
    return false;
  }
  Value fget;
  if (!property_get_field(args[0], "fget", fget)) {
    return property_method_receiver_error(runtime, args[0], "__get__", error);
  }
  if (args[1].tag == ValueTag::None) {
    value_assign_fast(out, args[0]);
    return true;
  }
  if (fget.tag == ValueTag::None || fget.tag == ValueTag::Invalid) {
    error = "unreadable attribute";
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  return runtime_call_callable(runtime, fget, &args[1], 1, out, error);
}

bool property_descriptor_set_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "property.__set__ expected 2 arguments";
    return false;
  }
  Value fset;
  if (!property_get_field(args[0], "fset", fset)) {
    return property_method_receiver_error(runtime, args[0], "__set__", error);
  }
  if (fset.tag == ValueTag::None || fset.tag == ValueTag::Invalid) {
    error = "can't set attribute";
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  Value call_args[2] = {args[1], args[2]};
  return runtime_call_callable(runtime, fset, call_args, 2, out, error);
}

bool property_descriptor_delete_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "property.__delete__ expected 1 argument";
    return false;
  }
  Value fdel;
  if (!property_get_field(args[0], "fdel", fdel)) {
    return property_method_receiver_error(runtime, args[0], "__delete__", error);
  }
  if (fdel.tag == ValueTag::None || fdel.tag == ValueTag::Invalid) {
    error = "can't delete attribute";
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  return runtime_call_callable(runtime, fdel, &args[1], 1, out, error);
}

bool property_init_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    const NativeKeywordArg* kwargs = nullptr,
    uint32_t kwargc = 0) {
  if (argc < 1 || argc > 5) {
    error = "property.__init__ expected at most 4 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  static constexpr const char* kNames[] = {"fget", "fset", "fdel", "doc"};
  Value values[] = {Value::none(), Value::none(), Value::none(), Value::none()};
  bool explicit_doc = argc >= 5;
  for (uint32_t i = 1; i < argc; ++i) {
    value_assign_fast(values[i - 1], args[i]);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name = kwargs[i].name != nullptr ? std::string_view(kwargs[i].name) : std::string_view();
    bool matched = false;
    for (uint32_t j = 0; j < 4; ++j) {
      if (name == kNames[j]) {
        if (j + 1 < argc) {
          error = "property.__init__ got multiple values for argument '" + std::string(kNames[j]) + "'";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        if (j == 3) {
          explicit_doc = true;
        }
        values[j] = *kwargs[i].value;
        matched = true;
        break;
      }
    }
    if (!matched) {
      error = "property.__init__ got an unexpected keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  Value self;
  value_assign_fast(self, args[0]);
  const bool doc_from_getter = !explicit_doc && values[0].tag != ValueTag::None && values[0].tag != ValueTag::Invalid;
  if (doc_from_getter) {
    values[3] = property_callable_attr_or_none(values[0], "__doc__");
  }
  if (!object_set_attr(self, "fget", values[0], error) ||
      !object_set_attr(self, "fset", values[1], error) ||
      !object_set_attr(self, "fdel", values[2], error) ||
      !object_set_attr(self, "__doc__", values[3], error) ||
      !object_set_attr(self, "__xlang3_doc_from_getter__", Value::boolean(doc_from_getter), error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value name;
  if (values[0].tag != ValueTag::None && object_get_attr(values[0], "__name__", name, error)) {
    std::string ignored;
    object_set_attr(self, "__name__", name, ignored);
    object_set_attr(self, "__xlang3_name_from_getter__", Value::boolean(true), ignored);
  }
  value_set_none(out);
  return true;
}

bool property_init_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return property_init_common(runtime, args, argc, out, error);
}

bool property_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return property_init_common(runtime, args, argc, out, error, kwargs, kwargc);
}

} // namespace

bool property_get_method(const Value& object, const std::string& name, Value& out) {
  auto* property = value_as_property(object);
  if (property == nullptr) {
    return false;
  }
  if (name == "__isabstractmethod__") {
    if (property->is_abstract) {
      value_set_bool(out, true);
      return true;
    }
    for (const Value* accessor : {&property->fget, &property->fset, &property->fdel}) {
      if (accessor->tag == ValueTag::None || accessor->tag == ValueTag::Invalid) {
        continue;
      }
      Value marker;
      std::string ignored;
      if (object_get_attr(*accessor, "__isabstractmethod__", marker, ignored) && value_truthy(marker)) {
        value_set_bool(out, true);
        return true;
      }
    }
    value_set_bool(out, false);
    return true;
  }
  if (name == "fget") {
    value_assign_fast(out, property->fget);
    return true;
  }
  if (name == "fset") {
    value_assign_fast(out, property->fset);
    return true;
  }
  if (name == "fdel") {
    value_assign_fast(out, property->fdel);
    return true;
  }
  if (name == "__doc__") {
    value_assign_fast(out, property->doc);
    return true;
  }
  if (name == "__name__" && property->has_name) {
    value_assign_fast(out, property->name);
    return true;
  }

  static constexpr BuiltinMethodSpec methods[] = {
      {"__get__", "property.__get__", property_descriptor_get_method},
      {"__set__", "property.__set__", property_descriptor_set_method},
      {"__delete__", "property.__delete__", property_descriptor_delete_method},
      {"getter", "property.getter", property_getter_method, nullptr, false, property_getter_kw},
      {"setter", "property.setter", property_setter_method, nullptr, false, property_setter_kw},
      {"deleter", "property.deleter", property_deleter_method, nullptr, false, property_deleter_kw},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

bool property_install_class_methods(Runtime& runtime, ClassObject& property_class) {
  property_class.attrs["__init__"] =
      runtime.make_native_function("property.__init__", property_init_method, nullptr, nullptr, nullptr, false, property_init_kw);
  property_class.attrs["__get__"] = Value::native_function(0, "property.__get__", property_descriptor_get_method);
  property_class.attrs["__set__"] = Value::native_function(0, "property.__set__", property_descriptor_set_method);
  property_class.attrs["__delete__"] = Value::native_function(0, "property.__delete__", property_descriptor_delete_method);
  property_class.attrs["getter"] =
      runtime.make_native_function("property.getter", property_getter_method, nullptr, nullptr, nullptr, false, property_getter_kw);
  property_class.attrs["setter"] =
      runtime.make_native_function("property.setter", property_setter_method, nullptr, nullptr, nullptr, false, property_setter_kw);
  property_class.attrs["deleter"] =
      runtime.make_native_function("property.deleter", property_deleter_method, nullptr, nullptr, nullptr, false, property_deleter_kw);
  ++property_class.version;
  return true;
}

} // namespace xlang3
