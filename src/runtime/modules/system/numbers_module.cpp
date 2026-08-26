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
#include "xlang3/sequence.h"

namespace xlang3 {

namespace {

Value abc_class(Runtime& runtime) {
  Value abc_module;
  std::string error;
  if (!runtime.import_module("abc", abc_module, error)) {
    return Value::invalid();
  }
  Value abc;
  return module_get_attr(abc_module, "ABC", abc, error) ? abc : Value::invalid();
}

Value abc_meta(Runtime& runtime) {
  Value abc_module;
  std::string error;
  if (!runtime.import_module("abc", abc_module, error)) {
    return Value::invalid();
  }
  Value meta;
  return module_get_attr(abc_module, "ABCMeta", meta, error) ? meta : Value::invalid();
}

Value make_number_abc(std::string name, Value base, const Value& metaclass) {
  return Value::class_object(std::move(name), {}, std::move(base), {}, metaclass);
}

void register_virtual_numeric(Value abc, const Value* builtin_type) {
  if (builtin_type == nullptr) {
    return;
  }
  Value registry;
  std::string error;
  if (!object_get_attr(abc, "__xlang3_abc_registry__", registry, error) || value_as_list(registry) == nullptr) {
    registry = Value::list({});
    object_set_attr(abc, "__xlang3_abc_registry__", registry, error);
  }
  auto* list = value_as_list(registry);
  if (list == nullptr) {
    return;
  }
  for (const auto& item : list->items) {
    if (value_is(item, *builtin_type)) {
      return;
    }
  }
  list->items.push_back(*builtin_type);
}

} // namespace

void register_numbers_module(Runtime& runtime) {
  Value root_base = abc_class(runtime);
  if (root_base.tag == ValueTag::Invalid) {
    root_base = runtime.find_builtin("object") != nullptr ? *runtime.find_builtin("object") : Value::invalid();
  }
  const Value meta = abc_meta(runtime);
  Value number_class = make_number_abc("Number", root_base, meta);
  Value complex_class = make_number_abc("Complex", number_class, meta);
  Value real_class = make_number_abc("Real", complex_class, meta);
  Value rational_class = make_number_abc("Rational", real_class, meta);
  Value integral_class = make_number_abc("Integral", rational_class, meta);

  const Value* int_type = runtime.find_builtin("int");
  const Value* bool_type = runtime.find_builtin("bool");
  const Value* float_type = runtime.find_builtin("float");
  const Value* complex_type = runtime.find_builtin("complex");
  for (Value* abc : {&number_class, &complex_class, &real_class, &rational_class, &integral_class}) {
    register_virtual_numeric(*abc, int_type);
    register_virtual_numeric(*abc, bool_type);
  }
  for (Value* abc : {&number_class, &complex_class, &real_class}) {
    register_virtual_numeric(*abc, float_type);
  }
  for (Value* abc : {&number_class, &complex_class}) {
    register_virtual_numeric(*abc, complex_type);
  }

  NativeModuleBuilder builder(runtime, "numbers");
  builder.value("Number", number_class)
      .value("Complex", complex_class)
      .value("Real", real_class)
      .value("Rational", rational_class)
      .value("Integral", integral_class);
  runtime.register_module("numbers", builder.finish());
}

} // namespace xlang3
