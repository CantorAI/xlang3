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

void add_numeric_base(Runtime& runtime, const char* name, const Value& number_class) {
  auto* klass = runtime.find_builtin(name);
  if (klass == nullptr) {
    return;
  }
  std::string ignored;
  class_set_base(*klass, number_class, ignored);
}

} // namespace

void register_numbers_module(Runtime& runtime) {
  const Value object_base = runtime.find_builtin("object") != nullptr ? *runtime.find_builtin("object") : Value::invalid();
  Value number_class = Value::class_object("Number", {}, object_base);
  Value complex_class = Value::class_object("Complex", {}, number_class);
  Value real_class = Value::class_object("Real", {}, complex_class);
  Value rational_class = Value::class_object("Rational", {}, real_class);
  Value integral_class = Value::class_object("Integral", {}, rational_class);

  add_numeric_base(runtime, "int", integral_class);
  add_numeric_base(runtime, "bool", integral_class);
  add_numeric_base(runtime, "float", real_class);

  NativeModuleBuilder builder(runtime, "numbers");
  builder.value("Number", number_class)
      .value("Complex", complex_class)
      .value("Real", real_class)
      .value("Rational", rational_class)
      .value("Integral", integral_class);
  runtime.register_module("numbers", builder.finish());
}

} // namespace xlang3
