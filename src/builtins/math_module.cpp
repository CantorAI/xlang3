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

XLANG3_HOT_INLINE const Value& fast_arg(
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t index) {
  if (index < leading_count) {
    return leading[index];
  }
  return registers[register_args[index - leading_count]];
}

XLANG3_HOT_INLINE bool fast_number_arg(
    const char* name,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    double& out,
    std::string& error) {
  if (leading_count + register_arg_count != 1) {
    error = std::string(name) + "() expected 1 argument";
    return false;
  }
  return require_number_arg(fast_arg(leading, leading_count, registers, register_args, 0), name, out, error);
}

bool math_sqrt(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math("sqrt", std::sqrt, args, argc, out, error);
}

bool math_sqrt_fast(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  double value = 0.0;
  if (!fast_number_arg("sqrt", leading, leading_count, registers, register_args, register_arg_count, value, error)) {
    return false;
  }
  value_set_number(out, std::sqrt(value));
  return true;
}

bool math_sin(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math("sin", std::sin, args, argc, out, error);
}

bool math_sin_fast(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  double value = 0.0;
  if (!fast_number_arg("sin", leading, leading_count, registers, register_args, register_arg_count, value, error)) {
    return false;
  }
  value_set_number(out, std::sin(value));
  return true;
}

bool math_cos(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math("cos", std::cos, args, argc, out, error);
}

bool math_cos_fast(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  double value = 0.0;
  if (!fast_number_arg("cos", leading, leading_count, registers, register_args, register_arg_count, value, error)) {
    return false;
  }
  value_set_number(out, std::cos(value));
  return true;
}

} // namespace

void register_math_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "math");
  builder.value("pi", Value::number(3.14159265358979323846))
      .function("sqrt", math_sqrt, math_sqrt_fast)
      .function("sin", math_sin, math_sin_fast)
      .function("cos", math_cos, math_cos_fast);
  runtime.register_module("math", builder.finish());
}

} // namespace xlang3
