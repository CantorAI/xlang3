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

#include <cmath>

namespace xlang3 {

namespace {

bool require_number_arg(const Value& value, const char* name, double& out, std::string& error) {
  if (value.tag == ValueTag::Int64) {
    out = static_cast<double>(value.as.i64);
    return true;
  }
  if (value.tag == ValueTag::Double) {
    out = value.as.f64;
    return true;
  }
  error = std::string(name) + "() argument must be a number";
  return false;
}

bool unary_math(const char* name, double (*fn)(double), const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (argc != 1) {
    error = std::string(name) + "() expected 1 argument";
    return false;
  }
  double value = 0.0;
  if (!require_number_arg(args[0], name, value, error)) {
    return false;
  }
  value_set_number(out, fn(value));
  return true;
}

bool math_sqrt(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error) {
  (void)runtime;
  return unary_math("sqrt", std::sqrt, args, argc, out, error);
}

bool math_sin(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error) {
  (void)runtime;
  return unary_math("sin", std::sin, args, argc, out, error);
}

bool math_cos(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error) {
  (void)runtime;
  return unary_math("cos", std::cos, args, argc, out, error);
}

} // namespace

void register_math_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "math");
  builder.value("pi", Value::number(3.14159265358979323846))
      .function("sqrt", math_sqrt)
      .function("sin", math_sin)
      .function("cos", math_cos);
  runtime.register_module("math", builder.finish());
}

} // namespace xlang3
