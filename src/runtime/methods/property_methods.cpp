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

bool property_getter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return property_method_argc_error(runtime, "getter", argc, error);
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    return property_method_receiver_error(runtime, args[0], "getter", error);
  }
  out = Value::property(args[1], property->fset, property->fdel, property->doc, property->is_abstract, property->doc_from_getter);
  if (!property->name_from_getter && property->has_name) {
    auto* cloned = value_as_property(out);
    value_assign_fast(cloned->name, property->name);
    cloned->has_name = true;
    cloned->name_from_getter = false;
  }
  return true;
}

bool property_getter_kw(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t, Value&, std::string& error, void*) {
  return property_method_keyword_error(runtime, "getter", error);
}

bool property_setter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return property_method_argc_error(runtime, "setter", argc, error);
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    return property_method_receiver_error(runtime, args[0], "setter", error);
  }
  out = Value::property(property->fget, args[1], property->fdel, property->doc, property->is_abstract, property->doc_from_getter);
  if (!property->name_from_getter && property->has_name) {
    auto* cloned = value_as_property(out);
    value_assign_fast(cloned->name, property->name);
    cloned->has_name = true;
    cloned->name_from_getter = false;
  }
  return true;
}

bool property_setter_kw(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t, Value&, std::string& error, void*) {
  return property_method_keyword_error(runtime, "setter", error);
}

bool property_deleter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return property_method_argc_error(runtime, "deleter", argc, error);
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    return property_method_receiver_error(runtime, args[0], "deleter", error);
  }
  out = Value::property(property->fget, property->fset, args[1], property->doc, property->is_abstract, property->doc_from_getter);
  if (!property->name_from_getter && property->has_name) {
    auto* cloned = value_as_property(out);
    value_assign_fast(cloned->name, property->name);
    cloned->has_name = true;
    cloned->name_from_getter = false;
  }
  return true;
}

bool property_deleter_kw(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t, Value&, std::string& error, void*) {
  return property_method_keyword_error(runtime, "deleter", error);
}

bool property_descriptor_get_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "property.__get__ expected 1 or 2 arguments";
    return false;
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    return property_method_receiver_error(runtime, args[0], "__get__", error);
  }
  if (args[1].tag == ValueTag::None) {
    value_assign_fast(out, args[0]);
    return true;
  }
  if (property->fget.tag == ValueTag::None || property->fget.tag == ValueTag::Invalid) {
    error = "unreadable attribute";
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  return runtime_call_callable(runtime, property->fget, &args[1], 1, out, error);
}

bool property_descriptor_set_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "property.__set__ expected 2 arguments";
    return false;
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    return property_method_receiver_error(runtime, args[0], "__set__", error);
  }
  if (property->fset.tag == ValueTag::None || property->fset.tag == ValueTag::Invalid) {
    error = "can't set attribute";
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  Value call_args[2] = {args[1], args[2]};
  return runtime_call_callable(runtime, property->fset, call_args, 2, out, error);
}

bool property_descriptor_delete_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "property.__delete__ expected 1 argument";
    return false;
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    return property_method_receiver_error(runtime, args[0], "__delete__", error);
  }
  if (property->fdel.tag == ValueTag::None || property->fdel.tag == ValueTag::Invalid) {
    error = "can't delete attribute";
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  return runtime_call_callable(runtime, property->fdel, &args[1], 1, out, error);
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
