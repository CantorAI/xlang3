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
#include "xlang3/attribute.h"

#include "xlang3/builtin_methods.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

bool get_builtin_method(const Value& object, const std::string& name, Value& out) {
  return list_get_method(object, name, out) ||
         tuple_get_method(object, name, out) ||
         dict_get_method(object, name, out) ||
         file_get_method(object, name, out) ||
         int_get_method(object, name, out) ||
         set_get_method(object, name, out) ||
         string_get_method(object, name, out) ||
         bytes_get_method(object, name, out) ||
         bytearray_get_method(object, name, out) ||
         memoryview_get_method(object, name, out) ||
         generator_get_method(object, name, out) ||
         property_get_method(object, name, out);
}

} // namespace

bool attribute_get(const Value& object, const std::string& name, Value& out, std::string& error) {
  if (value_as_module(object) != nullptr) {
    return module_get_attr(object, name, out, error);
  }
  if (auto* function = value_as_function(object)) {
    if (name == "__annotations__") {
      if (function->annotations.tag == ValueTag::Invalid) {
        value_assign_fast(function->annotations, Value::dict({}));
      }
      value_assign_fast(out, function->annotations);
      return true;
    }
    return object_get_attr(object, name, out, error);
  }
  if (value_as_native_function(object) != nullptr || value_as_bound_method(object) != nullptr ||
      value_as_code(object) != nullptr || value_as_frame(object) != nullptr ||
      value_as_traceback(object) != nullptr || value_as_class(object) != nullptr ||
      value_as_instance(object) != nullptr || value_as_super(object) != nullptr ||
      value_as_slot_descriptor(object) != nullptr || value_as_type_param(object) != nullptr) {
    return object_get_attr(object, name, out, error);
  }
  if (get_builtin_method(object, name, out)) {
    return true;
  }
  error = "object has no attribute '" + name + "'";
  return false;
}

bool attribute_set(Value& object, const std::string& name, const Value& value, std::string& error) {
  if (value_as_module(object) != nullptr) {
    return module_set_attr(object, name, value, error);
  }
  return object_set_attr(object, name, value, error);
}

} // namespace xlang3
