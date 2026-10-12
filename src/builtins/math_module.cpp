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
#include <utility>
#include <vector>

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

bool math_log_value(const Value& value, double& out, std::string& error) {
  if (value_as_bigint(value) != nullptr) {
    bool negative = false;
    const uint32_t* limbs = nullptr;
    uint32_t count = 0;
    if (!value_bigint_limb_view(value, negative, limbs, count) || count == 0 || negative) {
      error = "math domain error";
      return false;
    }
    uint32_t high_bits = 0;
    for (uint32_t high_limb = limbs[count - 1]; high_limb != 0; high_limb >>= 1u) {
      ++high_bits;
    }
    const uint64_t bit_count = static_cast<uint64_t>(count - 1) * 32u + high_bits;
    const uint32_t kept_bits = static_cast<uint32_t>(bit_count < 53u ? bit_count : 53u);
    const uint64_t shift = bit_count - kept_bits;
    uint64_t leading = 0;
    // CPython's math.log accepts arbitrary-size integers. Read only the top
    // 53 bits needed for a double mantissa; converting the full BigInt through
    // a decimal string would allocate and scan every digit on this native path.
    for (uint64_t bit = bit_count; bit > shift;) {
      --bit;
      leading = (leading << 1u) | ((limbs[bit / 32u] >> (bit % 32u)) & 1u);
    }
    const double mantissa = std::ldexp(
        static_cast<double>(leading), -static_cast<int>(kept_bits - 1u));
    constexpr double kLn2 = 0.693147180559945309417232121458176568;
    out = std::log(mantissa) + static_cast<double>(bit_count - 1u) * kLn2;
    return true;
  }

  double number = 0.0;
  if (!require_number_arg(value, "log", number, error)) return false;
  if (number <= 0.0) {
    error = "math domain error";
    return false;
  }
  out = std::log(number);
  return true;
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

bool math_frexp(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "frexp() expected 1 argument";
    return false;
  }
  double value = 0.0;
  if (!require_number_arg(args[0], "frexp", value, error)) return false;
  int exponent = 0;
  const double mantissa = std::frexp(value, &exponent);
  out = Value::tuple({Value::number(mantissa), Value::int64(exponent)});
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
  (void)user_data;
  if (argc < 1 || argc > 2) {
    error = "log() expected 1 or 2 arguments";
    return false;
  }
  double result = 0.0;
  if (!math_log_value(args[0], result, error)) {
    if (error == "math domain error") runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (argc == 2) {
    double log_base = 0.0;
    if (!math_log_value(args[1], log_base, error)) {
      if (error == "math domain error") runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (log_base == 0.0) {
      error = "math domain error";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    result /= log_base;
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
  (void)user_data;
  const uint32_t argc = leading_count + register_arg_count;
  if (argc < 1 || argc > 2) {
    error = "log() expected 1 or 2 arguments";
    return false;
  }
  double result = 0.0;
  if (!math_log_value(
          fast_arg(leading, leading_count, registers, register_args, 0), result, error)) {
    if (error == "math domain error") runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (argc == 2) {
    double log_base = 0.0;
    if (!math_log_value(
            fast_arg(leading, leading_count, registers, register_args, 1), log_base, error)) {
      if (error == "math domain error") runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (log_base == 0.0) {
      error = "math domain error";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    result /= log_base;
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

bool unary_math_bool_protocol(Runtime& runtime, const char* name,
                              bool (*fn)(double), const Value* args,
                              uint32_t argc, Value& out, std::string& error) {
  if (argc != 1) {
    error = std::string(name) + "() expected 1 argument";
    return false;
  }
  double value = 0.0;
  if (!require_number_arg(args[0], name, value, error)) {
    Value method;
    std::string lookup_error;
    if (!object_get_attr(args[0], "__float__", method, lookup_error)) {
      Value type;
      std::string type_name = value_binary_type_name(args[0]);
      if (runtime_type_of_value(runtime, args[0], type)) {
        if (auto* klass = value_as_class(type)) type_name = klass->name;
      }
      error = "must be real number, not " + type_name;
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    Value converted;
    error.clear();
    if (!runtime_call_callable(runtime, method, nullptr, 0, converted, error))
      return false;
    if (!require_number_arg(converted, name, value, error)) {
      error = "__float__ returned non-float";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  out = Value::boolean(fn(value));
  return true;
}

bool math_trunc(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "trunc() expected 1 argument";
    return false;
  }
  if (args[0].tag == ValueTag::Int64 || value_as_bigint(args[0]) != nullptr) {
    value_assign_fast(out, args[0]);
    return true;
  }
  if (args[0].tag == ValueTag::Double) {
    const Value* integer = runtime.find_builtin("int");
    if (integer == nullptr) {
      error = "int builtin is unavailable";
      return false;
    }
    return runtime_call_callable(runtime, *integer, args, 1, out, error);
  }
  Value trunc_method;
  std::string attr_error;
  if (!object_get_attr(args[0], "__trunc__", trunc_method, attr_error)) {
    error = "type doesn't define __trunc__ method";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return runtime_call_callable(runtime, trunc_method, nullptr, 0, out, error);
}

bool math_isfinite(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)user_data;
  return unary_math_bool_protocol(runtime, "isfinite", [](double value) { return std::isfinite(value); }, args, argc, out, error);
}

bool math_isfinite_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void* user_data) {
  (void)user_data;
  if (leading_count + register_arg_count != 1) { error = "isfinite() expected 1 argument"; return false; }
  const Value& arg = fast_arg(leading, leading_count, registers, register_args, 0);
  return unary_math_bool_protocol(runtime, "isfinite", [](double value) { return std::isfinite(value); }, &arg, 1, out, error);
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

bool math_log10(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math("log10", std::log10, args, argc, out, error);
}

bool math_log10_fast(Runtime&, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void*) {
  return fast_unary_math("log10", std::log10, leading, leading_count, registers, register_args, register_arg_count, out, error);
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

bool math_erf(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math("erf", std::erf, args, argc, out, error);
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

bool math_isnan(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math_bool_protocol(runtime, "isnan", [](double value) { return std::isnan(value); }, args, argc, out, error);
}

bool math_isnan_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void*) {
  if (leading_count + register_arg_count != 1) { error = "isnan() expected 1 argument"; return false; }
  const Value& arg = fast_arg(leading, leading_count, registers, register_args, 0);
  return unary_math_bool_protocol(runtime, "isnan", [](double value) { return std::isnan(value); }, &arg, 1, out, error);
}

bool math_isinf(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unary_math_bool_protocol(runtime, "isinf", [](double value) { return std::isinf(value); }, args, argc, out, error);
}

bool math_isinf_fast(Runtime& runtime, const Value* leading, uint32_t leading_count, const Value* registers, const uint32_t* register_args, uint32_t register_arg_count, Value& out, std::string& error, void*) {
  if (leading_count + register_arg_count != 1) { error = "isinf() expected 1 argument"; return false; }
  const Value& arg = fast_arg(leading, leading_count, registers, register_args, 0);
  return unary_math_bool_protocol(runtime, "isinf", [](double value) { return std::isinf(value); }, &arg, 1, out, error);
}

bool math_isclose_kw(Runtime& runtime, const Value* args, uint32_t argc,
                     const NativeKeywordArg* kwargs, uint32_t kwargc,
                     Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "isclose() expected 2 positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  double left = 0.0, right = 0.0, rel_tol = 1e-9, abs_tol = 0.0;
  if (!require_number_arg(args[0], "isclose", left, error) ||
      !require_number_arg(args[1], "isclose", right, error)) return false;
  bool have_rel = false, have_abs = false;
  for (uint32_t index = 0; index < kwargc; ++index) {
    const std::string name = kwargs[index].name == nullptr ? "" : kwargs[index].name;
    double* target = nullptr;
    bool* present = nullptr;
    if (name == "rel_tol") { target = &rel_tol; present = &have_rel; }
    else if (name == "abs_tol") { target = &abs_tol; present = &have_abs; }
    else {
      error = "isclose() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (*present || kwargs[index].value == nullptr ||
        !require_number_arg(*kwargs[index].value, "isclose", *target, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    *present = true;
  }
  if (rel_tol < 0.0 || abs_tol < 0.0) {
    error = "tolerances must be non-negative";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  bool close = left == right;
  if (!close && std::isfinite(left) && std::isfinite(right)) {
    const double difference = std::fabs(left - right);
    close = difference <= std::fabs(rel_tol * right) ||
            difference <= std::fabs(rel_tol * left) || difference <= abs_tol;
  }
  value_set_bool(out, close);
  return true;
}

bool math_isclose(Runtime& runtime, const Value* args, uint32_t argc,
                  Value& out, std::string& error, void* data) {
  return math_isclose_kw(runtime, args, argc, nullptr, 0, out, error, data);
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
  Value result = Value::int64(0);
  auto absolute_integer = [&](const Value& value, Value& absolute) -> bool {
    if (value.tag == ValueTag::Int64) {
      if (value.as.i64 == std::numeric_limits<int64_t>::min()) absolute = value_bigint_from_u64(uint64_t{1} << 63u);
      else absolute = Value::int64(value.as.i64 < 0 ? -value.as.i64 : value.as.i64);
      return true;
    }
    bool negative = false;
    const uint32_t* limbs = nullptr;
    uint32_t count = 0;
    if (!value_bigint_limb_view(value, negative, limbs, count)) return false;
    if (!negative) { value_assign_fast(absolute, value); return true; }
    return value_bigint_from_binary_limbs(limbs, static_cast<size_t>(count) * sizeof(uint32_t), false, absolute, error);
  };
  for (uint32_t index = 0; index < argc; ++index) {
    Value divisor;
    if (!absolute_integer(args[index], divisor)) {
      error = "math.gcd() arguments must be integers";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    while ((divisor.tag == ValueTag::Int64 && divisor.as.i64 != 0) || value_bigint_truthy(divisor)) {
      Value remainder;
      if (!value_mod(result, divisor, remainder, error)) return false;
      result = std::move(divisor);
      divisor = std::move(remainder);
    }
  }
  out = std::move(result);
  return true;
}

bool math_integer_arg(Runtime& runtime, const Value& value, Value& out,
                      std::string& error) {
  int64_t small = 0;
  if (value_int_like_to_i64(value, small)) {
    out = Value::int64(small);
    return true;
  }
  if (value_as_bigint(value) != nullptr) {
    value_assign_fast(out, value);
    return true;
  }
  Value index_method;
  std::string lookup_error;
  if (object_get_attr(value, "__index__", index_method, lookup_error)) {
    Value indexed;
    if (!runtime_call_callable(runtime, index_method, nullptr, 0, indexed, error))
      return false;
    if (value_int_like_to_i64(indexed, small)) {
      out = Value::int64(small);
      return true;
    }
    if (value_as_bigint(indexed) != nullptr) {
      out = std::move(indexed);
      return true;
    }
    error = "__index__ returned non-int";
  } else {
    error = "'" + std::string(value_binary_type_name(value)) +
        "' object cannot be interpreted as an integer";
  }
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool math_nonnegative(Runtime& runtime, const Value& value, const char* message,
                      std::string& error) {
  Value negative;
  if (!value_compare("<", value, Value::int64(0), negative, error)) return false;
  if (value_truthy(negative)) {
    error = message;
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool math_iteration_count(Runtime& runtime, const Value& value, uint64_t& count,
                          std::string& error) {
  if (value.tag == ValueTag::Int64 && value.as.i64 >= 0) {
    count = static_cast<uint64_t>(value.as.i64);
    return true;
  }
  if (value_bigint_to_u64(value, count)) return true;
  error = "math argument is too large to compute";
  runtime.raise_class_error("OverflowError", error);
  return false;
}

bool math_factorial_owned_limbs(uint32_t count, Value& out, std::string& error) {
  // math.factorial is a native CPython function, so its XLang3 counterpart
  // can compute directly on private limbs. The former Value multiply loop
  // cloned both operands and published a new bigint at every step; factorial
  // has no observable intermediate results or Python arithmetic callbacks.
  // Keep this accumulator private and publish only the final immutable Value.
  std::vector<uint32_t> limbs;
  limbs.reserve(8);
  limbs.push_back(1);
  auto multiply_chunk = [&limbs](uint32_t factor) {
    uint64_t carry = 0;
    for (uint32_t& limb : limbs) {
      const uint64_t product = static_cast<uint64_t>(limb) * factor + carry;
      limb = static_cast<uint32_t>(product);
      carry = product >> 32u;
    }
    if (carry != 0) limbs.push_back(static_cast<uint32_t>(carry));
  };

  // Batch consecutive factors while their product fits a 32-bit multiplier.
  // The test product fits uint64_t because each operand is at most UINT32_MAX;
  // it avoids a division in each iteration and reduces full-limb traversals.
  uint64_t chunk = 1;
  for (uint64_t factor = 2; factor <= count; ++factor) {
    const uint64_t next = chunk * factor;
    if (next > std::numeric_limits<uint32_t>::max()) {
      multiply_chunk(static_cast<uint32_t>(chunk));
      chunk = factor;
    } else {
      chunk = next;
    }
  }
  multiply_chunk(static_cast<uint32_t>(chunk));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  // The existing bigint import API takes little-endian limb bytes, even on
  // a big-endian host; it converts those bytes back to native limb storage.
  for (uint32_t& limb : limbs) {
    limb = (limb << 24u) | ((limb & 0x0000ff00u) << 8u) |
        ((limb & 0x00ff0000u) >> 8u) | (limb >> 24u);
  }
#endif
  return value_bigint_from_binary_limbs(limbs.data(), limbs.size() * sizeof(uint32_t),
                                        false, out, error);
}

bool math_factorial(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "factorial() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value n;
  if (!math_integer_arg(runtime, args[0], n, error) ||
      !math_nonnegative(runtime, n, "factorial() not defined for negative values", error))
    return false;
  uint64_t count = 0;
  if (!math_iteration_count(runtime, n, count, error)) return false;
  // CPython uses the platform C long range before doing any factorial work.
  // In particular, Windows must reject 2**31 rather than enter a huge loop.
  if (count > static_cast<uint64_t>(std::numeric_limits<long>::max())) {
    error = "factorial() argument should not exceed " +
        std::to_string(std::numeric_limits<long>::max());
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  static constexpr int64_t small_factorials[] = {
      1, 1, 2, 6, 24, 120, 720, 5040, 40320, 362880, 3628800, 39916800,
      479001600, 6227020800LL, 87178291200LL, 1307674368000LL,
      20922789888000LL, 355687428096000LL, 6402373705728000LL,
      121645100408832000LL, 2432902008176640000LL};
  if (count <= 20) {
    out = Value::int64(small_factorials[count]);
    return true;
  }
  if (count <= std::numeric_limits<uint32_t>::max())
    return math_factorial_owned_limbs(static_cast<uint32_t>(count), out, error);
  // On platforms with 64-bit C long, preserve the wider accepted input range.
  Value result = Value::int64(1);
  for (uint64_t i = 2; i <= count; ++i) {
    Value product;
    if (!value_mul(result, value_bigint_from_u64(i), product, error)) return false;
    result = std::move(product);
  }
  out = std::move(result);
  return true;
}

bool math_comb_or_perm(Runtime& runtime, const Value* args, uint32_t argc,
                       Value& out, std::string& error, bool combination) {
  const char* name = combination ? "comb" : "perm";
  if (argc < 1 || argc > 2 || (combination && argc != 2)) {
    error = std::string(name) + "() requires n and " +
        (combination ? "k" : "an optional k");
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value n;
  if (!math_integer_arg(runtime, args[0], n, error) ||
      !math_nonnegative(runtime, n, "n must be a non-negative integer", error))
    return false;
  if (!combination && (argc == 1 || args[1].tag == ValueTag::None)) {
    return math_factorial(runtime, &n, 1, out, error, nullptr);
  }
  Value k;
  if (!math_integer_arg(runtime, args[1], k, error) ||
      !math_nonnegative(runtime, k, "k must be a non-negative integer", error))
    return false;
  Value greater;
  if (!value_compare(">", k, n, greater, error)) return false;
  if (value_truthy(greater)) {
    out = Value::int64(0);
    return true;
  }
  if (combination) {
    Value complement;
    Value smaller;
    if (!value_sub(n, k, complement, error) ||
        !value_compare("<", complement, k, smaller, error)) return false;
    if (value_truthy(smaller)) k = std::move(complement);
  }
  uint64_t count = 0;
  if (!math_iteration_count(runtime, k, count, error)) return false;
  Value result = Value::int64(1);
  for (uint64_t i = 0; i < count; ++i) {
    Value factor;
    Value product;
    if (!value_sub(n, value_bigint_from_u64(i), factor, error) ||
        !value_mul(result, factor, product, error)) return false;
    if (combination) {
      Value quotient;
      if (!value_floor_div(product, value_bigint_from_u64(i + 1), quotient, error))
        return false;
      result = std::move(quotient);
    } else {
      result = std::move(product);
    }
  }
  out = std::move(result);
  return true;
}

bool math_comb(Runtime& runtime, const Value* args, uint32_t argc,
               Value& out, std::string& error, void*) {
  return math_comb_or_perm(runtime, args, argc, out, error, true);
}

bool math_perm(Runtime& runtime, const Value* args, uint32_t argc,
               Value& out, std::string& error, void*) {
  return math_comb_or_perm(runtime, args, argc, out, error, false);
}

bool math_log1p(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "log1p() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  double number = 0.0;
  if (!require_number_arg(args[0], "log1p", number, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (number <= -1.0) {
    error = "math domain error";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  value_set_number(out, std::log1p(number));
  return true;
}

bool math_isqrt(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "isqrt() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value n;
  if (!math_integer_arg(runtime, args[0], n, error) ||
      !math_nonnegative(runtime, n, "isqrt() argument must be nonnegative", error))
    return false;
  Value zero;
  if (!value_compare("==", n, Value::int64(0), zero, error)) return false;
  if (value_truthy(zero)) {
    out = Value::int64(0);
    return true;
  }
  uint64_t bits = 0;
  if (n.tag == ValueTag::Int64) {
    uint64_t magnitude = static_cast<uint64_t>(n.as.i64);
    while (magnitude != 0) { ++bits; magnitude >>= 1u; }
  } else {
    bool negative = false;
    const uint32_t* limbs = nullptr;
    uint32_t count = 0;
    if (!value_bigint_limb_view(n, negative, limbs, count) || count == 0) {
      error = "isqrt() argument must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    bits = static_cast<uint64_t>(count - 1) * 32u;
    uint32_t top = limbs[count - 1];
    while (top != 0) { ++bits; top >>= 1u; }
  }
  Value guess;
  if (!value_shift_left(Value::int64(1),
                        value_bigint_from_u64((bits + 1) / 2), guess, error)) return false;
  for (;;) {
    Value quotient, sum, next, stable;
    if (!value_floor_div(n, guess, quotient, error) ||
        !value_add(guess, quotient, sum, error) ||
        !value_floor_div(sum, Value::int64(2), next, error) ||
        !value_compare(">=", next, guess, stable, error)) return false;
    if (value_truthy(stable)) {
      out = std::move(guess);
      return true;
    }
    guess = std::move(next);
  }
}

bool math_prod_kw(Runtime& runtime, const Value* args, uint32_t argc,
                  const NativeKeywordArg* kwargs, uint32_t kwargc,
                  Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "prod() takes exactly one positional argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value result = Value::int64(1);
  bool has_start = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view keyword(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (keyword != "start" || has_start) {
      error = "prod() got an unexpected or duplicate keyword argument '" +
          std::string(keyword) + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_assign_fast(result, *kwargs[i].value);
    has_start = true;
  }
  Value iterator;
  if (!runtime_get_iter(runtime, args[0], iterator, error)) return false;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) return false;
    if (done) break;
    Value product;
    bool implemented = false;
    const Value* not_implemented = runtime.find_builtin("NotImplemented");
    for (const auto& candidate : {
             std::pair<const Value*, const char*>{&result, "__mul__"},
             std::pair<const Value*, const char*>{&item, "__rmul__"}}) {
      if (candidate.first->tag != ValueTag::Object) continue;
      Value method;
      std::string lookup_error;
      if (!object_get_special_method(runtime, *candidate.first,
                                     candidate.second, method, lookup_error)) continue;
      const Value& argument = candidate.first == &result ? item : result;
      if (!runtime_call_callable(runtime, method, &argument, 1, product, error)) return false;
      if (not_implemented == nullptr || !value_is(product, *not_implemented)) {
        implemented = true;
        break;
      }
    }
    if (!implemented && !value_mul(result, item, product, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    result = std::move(product);
  }
  out = std::move(result);
  return true;
}

bool math_prod(Runtime& runtime, const Value* args, uint32_t argc,
               Value& out, std::string& error, void* user_data) {
  return math_prod_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

} // namespace

void register_math_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "math");
  builder.value("pi", Value::number(3.14159265358979323846))
      .value("e", Value::number(2.71828182845904523536))
      .value("tau", Value::number(6.28318530717958647692))
      .value("inf", Value::number(std::numeric_limits<double>::infinity()))
      .value("nan", Value::number(std::numeric_limits<double>::quiet_NaN()))
      .function("log", math_log, math_log_fast)
      .function("exp", math_exp, math_exp_fast)
      .function("acos", math_acos, math_acos_fast)
      .function("floor", math_floor, math_floor_fast)
      .function("ceil", math_ceil, math_ceil_fast)
      .function("trunc", math_trunc)
      .function("isfinite", math_isfinite, math_isfinite_fast)
      .function("isnan", math_isnan, math_isnan_fast)
      .function("isinf", math_isinf, math_isinf_fast)
      .value("isclose", runtime.make_native_function(
          "math.isclose", math_isclose, nullptr, nullptr, nullptr, false, math_isclose_kw, false))
      .function("copysign", math_copysign)
      .function("ldexp", math_ldexp)
      .function("lgamma", math_lgamma, math_lgamma_fast)
      .function("fabs", math_fabs, math_fabs_fast)
      .function("log2", math_log2, math_log2_fast)
      .function("log10", math_log10, math_log10_fast)
      .function("log1p", math_log1p)
      .function("isqrt", math_isqrt)
      .function("sqrt", math_sqrt, math_sqrt_fast)
      .function("hypot", math_hypot)
      .function("erf", math_erf)
      .function("erfc", math_erfc)
      .function("tan", math_tan)
      .function("cosh", math_cosh)
      .function("asin", math_asin)
      .function("atan", math_atan)
      .function("fsum", math_fsum)
      .function("sumprod", math_sumprod)
      .function("modf", math_modf)
      .function("frexp", math_frexp)
      .function("sin", math_sin, math_sin_fast)
      .function("cos", math_cos, math_cos_fast)
      .function("sinh", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
        return unary_math("sinh", std::sinh, args, argc, out, error);
      })
      .function("tanh", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
        return unary_math("tanh", std::tanh, args, argc, out, error);
      });
  builder.function("gcd", math_gcd);
  builder.function("comb", math_comb)
      .function("perm", math_perm)
      .function("factorial", math_factorial)
      .function("prod", math_prod, nullptr, false, math_prod_kw);
  runtime.register_module("math", builder.finish());
}

} // namespace xlang3
