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

bool unary_math_int(const char* name, double (*fn)(double), const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (argc != 1) {
    error = std::string(name) + "() expected 1 argument";
    return false;
  }
  double value = 0.0;
  if (!require_number_arg(args[0], name, value, error)) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(fn(value)));
  return true;
}

bool unary_math_bool(const char* name, bool (*fn)(double), const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (argc != 1) {
    error = std::string(name) + "() expected 1 argument";
    return false;
  }
  double value = 0.0;
  if (!require_number_arg(args[0], name, value, error)) {
    return false;
  }
  out = Value::boolean(fn(value));
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

XLANG3_HOT_INLINE bool fast_number_arg_at(
    const char* name,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    uint32_t index,
    double& out,
    std::string& error) {
  if (index >= leading_count + register_arg_count) {
    error = std::string(name) + "() missing argument";
    return false;
  }
  return require_number_arg(fast_arg(leading, leading_count, registers, register_args, index), name, out, error);
}

XLANG3_HOT_INLINE bool fast_unary_math(
    const char* name,
    double (*fn)(double),
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error) {
  double value = 0.0;
  if (!fast_number_arg(name, leading, leading_count, registers, register_args, register_arg_count, value, error)) {
    return false;
  }
  value_set_number(out, fn(value));
  return true;
}

XLANG3_HOT_INLINE bool fast_unary_math_int(
    const char* name,
    double (*fn)(double),
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error) {
  double value = 0.0;
  if (!fast_number_arg(name, leading, leading_count, registers, register_args, register_arg_count, value, error)) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(fn(value)));
  return true;
}

XLANG3_HOT_INLINE bool fast_unary_math_bool(
    const char* name,
    bool (*fn)(double),
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error) {
  double value = 0.0;
  if (!fast_number_arg(name, leading, leading_count, registers, register_args, register_arg_count, value, error)) {
    return false;
  }
  out = Value::boolean(fn(value));
  return true;
}

bool math_log(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc < 1 || argc > 2) {
    error = "log() expected 1 or 2 arguments";
    return false;
  }
  double value = 0.0;
  if (!require_number_arg(args[0], "log", value, error)) {
    return false;
  }
  double result = std::log(value);
  if (argc == 2) {
    double base = 0.0;
    if (!require_number_arg(args[1], "log", base, error)) {
      return false;
    }
    result /= std::log(base);
  }
  value_set_number(out, result);
  return true;
}

bool math_log_fast(
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
  const uint32_t argc = leading_count + register_arg_count;
  if (argc < 1 || argc > 2) {
    error = "log() expected 1 or 2 arguments";
    return false;
  }
  double value = 0.0;
  if (!fast_number_arg_at("log", leading, leading_count, registers, register_args, register_arg_count, 0, value, error)) {
    return false;
  }
  double result = std::log(value);
  if (argc == 2) {
    double base = 0.0;
    if (!fast_number_arg_at("log", leading, leading_count, registers, register_args, register_arg_count, 1, base, error)) {
      return false;
    }
    result /= std::log(base);
  }
  value_set_number(out, result);
  return true;
}

bool math_exp(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math("exp", std::exp, args, argc, out, error);
}

bool math_exp_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return fast_unary_math("exp", std::exp, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_acos(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math("acos", std::acos, args, argc, out, error);
}

bool math_acos_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return fast_unary_math("acos", std::acos, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_floor(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math_int("floor", std::floor, args, argc, out, error);
}

bool math_floor_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return fast_unary_math_int("floor", std::floor, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_ceil(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math_int("ceil", std::ceil, args, argc, out, error);
}

bool math_ceil_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return fast_unary_math_int("ceil", std::ceil, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_isfinite(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math_bool("isfinite", [](double value) { return std::isfinite(value); }, args, argc, out, error);
}

bool math_isfinite_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return fast_unary_math_bool("isfinite", [](double value) { return std::isfinite(value); }, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_lgamma(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math("lgamma", std::lgamma, args, argc, out, error);
}

bool math_lgamma_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return fast_unary_math("lgamma", std::lgamma, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_fabs(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math("fabs", std::fabs, args, argc, out, error);
}

bool math_fabs_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return fast_unary_math("fabs", std::fabs, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_log2(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return unary_math("log2", std::log2, args, argc, out, error);
}

bool math_log2_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return fast_unary_math("log2", std::log2, leading, leading_count, registers, register_args, register_arg_count, out, error);
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
      .value("e", Value::number(2.71828182845904523536))
      .value("tau", Value::number(6.28318530717958647692))
      .function("log", math_log, math_log_fast)
      .function("exp", math_exp, math_exp_fast)
      .function("acos", math_acos, math_acos_fast)
      .function("floor", math_floor, math_floor_fast)
      .function("ceil", math_ceil, math_ceil_fast)
      .function("isfinite", math_isfinite, math_isfinite_fast)
      .function("lgamma", math_lgamma, math_lgamma_fast)
      .function("fabs", math_fabs, math_fabs_fast)
      .function("log2", math_log2, math_log2_fast)
      .function("sqrt", math_sqrt, math_sqrt_fast)
      .function("sin", math_sin, math_sin_fast)
      .function("cos", math_cos, math_cos_fast);
  runtime.register_module("math", builder.finish());
}

} // namespace xlang3
