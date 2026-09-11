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
#include "xlang3/functional_iterators.h"
#include "xlang3/sequence.h"

#include <cmath>
#include <limits>
#include <numeric>

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
  if (auto* instance = value_as_instance(value)) {
    auto* klass = value_as_class(instance->klass);
    if (klass != nullptr && (class_has_builtin_base_name(klass, "int") ||
                             class_has_builtin_base_name(klass, "float"))) {
      Value stored;
      std::string ignored;
      if ((object_get_attr(value, "__xlang3_int_value__", stored, ignored) ||
           object_get_attr(value, "__xlang3_float_value__", stored, ignored) ||
           object_get_attr(value, "_value_", stored, ignored))) {
        if (stored.tag == ValueTag::Int64) {
          out = static_cast<double>(stored.as.i64);
          return true;
        }
        if (stored.tag == ValueTag::Double) {
          out = stored.as.f64;
          return true;
        }
      }
    }
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

bool math_modf(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "modf() expected 1 argument";
    return false;
  }
  double value = 0.0;
  if (!require_number_arg(args[0], "modf", value, error)) {
    return false;
  }
  double integral = 0.0;
  const double fractional = std::modf(value, &integral);
  out = Value::tuple({Value::number(fractional), Value::number(integral)});
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

bool math_hypot(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  double result = 0.0;
  for (uint32_t i = 0; i < argc; ++i) {
    double value = 0.0;
    if (!require_number_arg(args[i], "hypot", value, error)) {
      return false;
    }
    result = std::hypot(result, value);
  }
  value_set_number(out, result);
  return true;
}

bool math_erfc(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math("erfc", std::erfc, args, argc, out, error);
}

bool math_tan(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math("tan", std::tan, args, argc, out, error);
}

bool math_cosh(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math("cosh", std::cosh, args, argc, out, error);
}

bool math_asin(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math("asin", std::asin, args, argc, out, error);
}

bool math_atan(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math("atan", std::atan, args, argc, out, error);
}

bool math_fsum(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "fsum() expected 1 argument";
    return false;
  }
  double total = 0.0;
  Value iterator;
  if (!runtime_get_iter(runtime, args[0], iterator, error)) return false;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) return false;
    if (done) break;
    double value = 0.0;
    if (!require_number_arg(item, "fsum", value, error)) return false;
    total += value;
  }
  value_set_number(out, total);
  return true;
}

bool math_sumprod(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "sumprod() expected 2 arguments";
    return false;
  }
  Value left_iterator;
  Value right_iterator;
  if (!runtime_get_iter(runtime, args[0], left_iterator, error) || !runtime_get_iter(runtime, args[1], right_iterator, error)) return false;
  double total = 0.0;
  for (;;) {
    bool left_done = false;
    bool right_done = false;
    Value left;
    Value right;
    if (!sequence_iter_next(left_iterator, left_done, left, error) ||
        !sequence_iter_next(right_iterator, right_done, right, error)) return false;
    if (left_done || right_done) {
      if (left_done != right_done) {
        error = "sumprod() arguments must have equal length";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
      break;
    }
    double lhs = 0.0;
    double rhs = 0.0;
    if (!require_number_arg(left, "sumprod", lhs, error) || !require_number_arg(right, "sumprod", rhs, error)) return false;
    total += lhs * rhs;
  }
  value_set_number(out, total);
  return true;
}

bool math_isnan(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math_bool("isnan", [](double value) { return std::isnan(value); }, args, argc, out, error);
}

bool math_isnan_fast(Runtime&, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void*) {
  return fast_unary_math_bool("isnan", [](double value) { return std::isnan(value); }, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_isinf(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math_bool("isinf", [](double value) { return std::isinf(value); }, args, argc, out, error);
}

bool math_isinf_fast(Runtime&, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void*) {
  return fast_unary_math_bool("isinf", [](double value) { return std::isinf(value); }, leading, leading_count, registers, register_args, register_arg_count, out, error);
}

bool math_copysign(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "copysign() expected 2 arguments";
    return false;
  }
  double magnitude = 0.0;
  double sign = 0.0;
  if (!require_number_arg(args[0], "copysign", magnitude, error) ||
      !require_number_arg(args[1], "copysign", sign, error)) {
    return false;
  }
  value_set_number(out, std::copysign(magnitude, sign));
  return true;
}

bool math_ldexp(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "ldexp() expected a number and an integer";
    return false;
  }
  double value = 0.0;
  if (!require_number_arg(args[0], "ldexp", value, error)) return false;
  value_set_number(out, std::ldexp(value, static_cast<int>(args[1].as.i64)));
  return true;
}

bool math_gcd(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  uint64_t result = 0;
  for (uint32_t index = 0; index < argc; ++index) {
    int64_t value = 0;
    if (!value_int_like_to_i64(args[index], value)) {
      error = "math.gcd() arguments must be integers";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const uint64_t magnitude = value < 0
        ? static_cast<uint64_t>(-(value + 1)) + 1u
        : static_cast<uint64_t>(value);
    result = std::gcd(result, magnitude);
  }
  if (result > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    error = "math.gcd() result exceeds the compact integer range";
    return false;
  }
  out = Value::int64(static_cast<int64_t>(result));
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
      .function("isnan", math_isnan, math_isnan_fast)
      .function("isinf", math_isinf, math_isinf_fast)
      .function("copysign", math_copysign)
      .function("ldexp", math_ldexp)
      .function("lgamma", math_lgamma, math_lgamma_fast)
      .function("fabs", math_fabs, math_fabs_fast)
      .function("log2", math_log2, math_log2_fast)
      .function("sqrt", math_sqrt, math_sqrt_fast)
      .function("hypot", math_hypot)
      .function("erfc", math_erfc)
      .function("tan", math_tan)
      .function("cosh", math_cosh)
      .function("asin", math_asin)
      .function("atan", math_atan)
      .function("fsum", math_fsum)
      .function("sumprod", math_sumprod)
      .function("modf", math_modf)
      .function("sin", math_sin, math_sin_fast)
      .function("cos", math_cos, math_cos_fast);
  builder.function("gcd", math_gcd);
  runtime.register_module("math", builder.finish());
}

} // namespace xlang3
