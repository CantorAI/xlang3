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
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

bool property_getter_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "property.getter", error)) {
    return false;
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    error = "property.getter target is not a property";
    return false;
  }
  out = Value::property(args[1], property->fset, property->fdel, property->doc_from_getter ? Value::none() : property->doc, property->is_abstract);
  return true;
}

bool property_setter_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "property.setter", error)) {
    return false;
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    error = "property.setter target is not a property";
    return false;
  }
  out = Value::property(property->fget, args[1], property->fdel, property->doc, property->is_abstract);
  return true;
}

bool property_deleter_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "property.deleter", error)) {
    return false;
  }
  auto* property = value_as_property(args[0]);
  if (property == nullptr) {
    error = "property.deleter target is not a property";
    return false;
  }
  out = Value::property(property->fget, property->fset, args[1], property->doc, property->is_abstract);
  return true;
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

  static constexpr BuiltinMethodSpec methods[] = {
      {"getter", "property.getter", property_getter_method},
      {"setter", "property.setter", property_setter_method},
      {"deleter", "property.deleter", property_deleter_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
