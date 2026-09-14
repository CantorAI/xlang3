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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <string>

namespace xlang3 {

namespace {

using Complex = std::complex<double>;

bool stored_complex(const Value& value, Complex& out) {
  if (auto* number = value_as_complex(value)) {
    out = Complex(number->real, number->imag);
    return true;
  }
  Value stored;
  std::string ignored;
  if (value_as_instance(value) != nullptr &&
      object_get_attr(value, "__xlang3_complex_value__", stored, ignored)) {
    if (auto* number = value_as_complex(stored)) {
      out = Complex(number->real, number->imag);
      return true;
    }
  }
  return false;
}

bool scalar_double(const Value& value, double& out) {
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1.0 : 0.0;
    return true;
  }
  if (value.tag == ValueTag::Int64) {
    out = static_cast<double>(value.as.i64);
    return true;
  }
  if (value.tag == ValueTag::Double) {
    out = value.as.f64;
    return true;
  }
  Value stored;
  std::string ignored;
  if (value_as_instance(value) != nullptr &&
      (object_get_attr(value, "__xlang3_float_value__", stored, ignored) ||
       object_get_attr(value, "__xlang3_int_value__", stored, ignored) ||
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
  return false;
}

bool cmath_number(Runtime& runtime, const Value& value, const char* name, Complex& out, std::string& error) {
  if (stored_complex(value, out)) return true;
  double real = 0.0;
  if (scalar_double(value, real)) {
    out = Complex(real, 0.0);
    return true;
  }

  for (const char* conversion : {"__complex__", "__float__", "__index__"}) {
    Value method;
    std::string lookup_error;
    if (!object_get_attr(value, conversion, method, lookup_error)) continue;
    Value converted;
    if (!runtime_call_callable(runtime, method, nullptr, 0, converted, error)) return false;
    if (std::string_view(conversion) == "__complex__") {
      if (stored_complex(converted, out)) return true;
      error = "__complex__ returned non-complex";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (scalar_double(converted, real)) {
      out = Complex(real, 0.0);
      return true;
    }
    error = std::string(conversion) + " returned non-number";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  error = std::string(name) + "() argument must be a number";
  runtime.raise_class_error("TypeError", error);
  return false;
}

void set_complex(Value& out, const Complex& value) {
  out = Value::complex(value.real(), value.imag());
}

enum class UnaryOperation : uintptr_t {
  Acos,
  Acosh,
  Asin,
  Asinh,
  Atan,
  Atanh,
  Cos,
  Cosh,
  Exp,
  Log10,
  Sin,
  Sinh,
  Sqrt,
  Tan,
  Tanh,
};

const char* unary_name(UnaryOperation operation) {
  switch (operation) {
    case UnaryOperation::Acos: return "acos";
    case UnaryOperation::Acosh: return "acosh";
    case UnaryOperation::Asin: return "asin";
    case UnaryOperation::Asinh: return "asinh";
    case UnaryOperation::Atan: return "atan";
    case UnaryOperation::Atanh: return "atanh";
    case UnaryOperation::Cos: return "cos";
    case UnaryOperation::Cosh: return "cosh";
    case UnaryOperation::Exp: return "exp";
    case UnaryOperation::Log10: return "log10";
    case UnaryOperation::Sin: return "sin";
    case UnaryOperation::Sinh: return "sinh";
    case UnaryOperation::Sqrt: return "sqrt";
    case UnaryOperation::Tan: return "tan";
    case UnaryOperation::Tanh: return "tanh";
  }
  return "cmath";
}

Complex apply_unary(UnaryOperation operation, const Complex& value) {
  if (operation == UnaryOperation::Acos && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(1.57079632679489661923, value.imag());
  }
  if (operation == UnaryOperation::Acos && std::isinf(value.real()) && std::isnan(value.imag())) {
    return Complex(std::numeric_limits<double>::quiet_NaN(),
                   std::numeric_limits<double>::infinity());
  }
  if (operation == UnaryOperation::Acos && std::isnan(value.real()) && std::isinf(value.imag())) {
    return Complex(std::numeric_limits<double>::quiet_NaN(), -value.imag());
  }
  if (operation == UnaryOperation::Acosh && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(std::numeric_limits<double>::quiet_NaN(), 1.57079632679489661923);
  }
  if (operation == UnaryOperation::Acosh &&
      ((std::isinf(value.real()) && std::isnan(value.imag())) ||
       (std::isnan(value.real()) && std::isinf(value.imag())))) {
    return Complex(std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN());
  }
  if (operation == UnaryOperation::Asin && std::isnan(value.real()) && std::isinf(value.imag())) {
    return Complex(std::numeric_limits<double>::quiet_NaN(), value.imag());
  }
  if (operation == UnaryOperation::Asin && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Asin && std::isinf(value.real()) && std::isnan(value.imag())) {
    return Complex(std::numeric_limits<double>::quiet_NaN(),
                   std::numeric_limits<double>::infinity());
  }
  if (operation == UnaryOperation::Asinh && std::isinf(value.real()) && std::isnan(value.imag())) {
    return Complex(value.real(), std::numeric_limits<double>::quiet_NaN());
  }
  if (operation == UnaryOperation::Asinh && std::isnan(value.real()) && std::isinf(value.imag())) {
    return Complex(std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN());
  }
  if (operation == UnaryOperation::Asinh && std::isnan(value.real()) && value.imag() == 0.0) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Atan && std::isnan(value.real()) && value.imag() == 0.0) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Atan && std::isnan(value.real()) && std::isinf(value.imag())) {
    return Complex(value.real(), std::copysign(0.0, value.imag()));
  }
  if (operation == UnaryOperation::Atan && std::isinf(value.real()) && std::isnan(value.imag())) {
    return Complex(std::copysign(1.57079632679489661923, value.real()), 0.0);
  }
  if (operation == UnaryOperation::Atanh && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Atanh && std::isinf(value.real()) && std::isnan(value.imag())) {
    return Complex(std::copysign(0.0, value.real()), value.imag());
  }
  if (operation == UnaryOperation::Atanh && std::isnan(value.real()) && std::isinf(value.imag())) {
    return Complex(0.0, std::copysign(1.57079632679489661923, value.imag()));
  }
  if (operation == UnaryOperation::Cosh && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(std::numeric_limits<double>::quiet_NaN(), 0.0);
  }
  if (operation == UnaryOperation::Cosh && std::isinf(value.real()) && std::isnan(value.imag())) {
    return Complex(std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN());
  }
  if (operation == UnaryOperation::Cosh && std::isinf(value.real()) &&
      std::isfinite(value.imag()) && value.imag() != 0.0) {
    const double infinity = std::numeric_limits<double>::infinity();
    const double real = std::copysign(infinity, std::cos(value.imag()));
    double imag = std::copysign(infinity, std::sin(value.imag()));
    if (value.real() < 0.0) imag = -imag;
    return Complex(real, imag);
  }
  if (operation == UnaryOperation::Cosh && std::isinf(value.real()) && value.imag() == 0.0) {
    return Complex(std::numeric_limits<double>::infinity(),
                   value.real() < 0.0 ? -value.imag() : value.imag());
  }
  if (operation == UnaryOperation::Cosh && std::isnan(value.real()) && value.imag() == 0.0) {
    return Complex(value.real(), 0.0);
  }
  if (operation == UnaryOperation::Sinh && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Sinh && std::isinf(value.real()) &&
      std::isfinite(value.imag()) && value.imag() != 0.0) {
    const double infinity = std::numeric_limits<double>::infinity();
    double real = std::copysign(infinity, std::cos(value.imag()));
    if (value.real() < 0.0) real = -real;
    const double imag = std::copysign(infinity, std::sin(value.imag()));
    return Complex(real, imag);
  }
  if (operation == UnaryOperation::Sinh && std::isinf(value.real()) && value.imag() == 0.0) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Sinh && std::isinf(value.real()) && std::isnan(value.imag())) {
    return Complex(std::numeric_limits<double>::infinity(), value.imag());
  }
  if (operation == UnaryOperation::Sinh && std::isnan(value.real()) && value.imag() == 0.0) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Tanh && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Tanh && std::isinf(value.real()) && !std::isfinite(value.imag())) {
    return Complex(std::copysign(1.0, value.real()), 0.0);
  }
  if (operation == UnaryOperation::Tanh && std::isnan(value.real()) && value.imag() == 0.0) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Cos &&
      ((std::isnan(value.real()) && value.imag() == 0.0) ||
       (value.real() == 0.0 && std::isnan(value.imag())))) {
    return Complex(std::numeric_limits<double>::quiet_NaN(), 0.0);
  }
  if (operation == UnaryOperation::Cos && std::isfinite(value.real()) && std::isinf(value.imag())) {
    const double infinity = std::numeric_limits<double>::infinity();
    const double sine = std::sin(value.real());
    const bool imag_negative = (!std::signbit(sine)) == (!std::signbit(value.imag()));
    return Complex(std::copysign(infinity, std::cos(value.real())),
                   std::copysign(sine == 0.0 ? 0.0 : infinity, imag_negative ? -1.0 : 1.0));
  }
  if (operation == UnaryOperation::Cos && std::isnan(value.real()) && std::isinf(value.imag())) {
    return Complex(std::numeric_limits<double>::infinity(), value.real());
  }
  if (operation == UnaryOperation::Sin && std::isnan(value.real()) && value.imag() == 0.0) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Sin && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Sin && std::isfinite(value.real()) && std::isinf(value.imag())) {
    const double infinity = std::numeric_limits<double>::infinity();
    const double sine = std::sin(value.real());
    const double cosine = std::cos(value.real());
    const bool imag_negative = std::signbit(cosine) != std::signbit(value.imag());
    return Complex(std::copysign(sine == 0.0 ? 0.0 : infinity, sine),
                   std::copysign(infinity, imag_negative ? -1.0 : 1.0));
  }
  if (operation == UnaryOperation::Sin && std::isnan(value.real()) && std::isinf(value.imag())) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Tan && std::isnan(value.real()) && value.imag() == 0.0) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Tan && value.real() == 0.0 && std::isnan(value.imag())) {
    return Complex(value.real(), value.imag());
  }
  if (operation == UnaryOperation::Tan && std::isinf(value.imag()) &&
      (std::isinf(value.real()) || std::isnan(value.real()))) {
    return Complex(0.0, std::copysign(1.0, value.imag()));
  }
  switch (operation) {
    case UnaryOperation::Acos: return std::acos(value);
    case UnaryOperation::Acosh: return std::acosh(value);
    case UnaryOperation::Asin: return std::asin(value);
    case UnaryOperation::Asinh: return std::asinh(value);
    case UnaryOperation::Atan: return std::atan(value);
    case UnaryOperation::Atanh: return std::atanh(value);
    case UnaryOperation::Cos: return std::cos(value);
    case UnaryOperation::Cosh: return std::cosh(value);
    case UnaryOperation::Exp: return std::exp(value);
    case UnaryOperation::Log10: return std::log10(value);
    case UnaryOperation::Sin: return std::sin(value);
    case UnaryOperation::Sinh: return std::sinh(value);
    case UnaryOperation::Sqrt: return std::sqrt(value);
    case UnaryOperation::Tan: return std::tan(value);
    case UnaryOperation::Tanh: return std::tanh(value);
  }
  return {};
}

bool cmath_unary(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  const auto operation = static_cast<UnaryOperation>(reinterpret_cast<uintptr_t>(user_data));
  const char* name = unary_name(operation);
  if (argc != 1) {
    error = std::string(name) + "() expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Complex value;
  if (!cmath_number(runtime, args[0], name, value, error)) return false;
  if ((operation == UnaryOperation::Atan && value.real() == 0.0 && std::fabs(value.imag()) == 1.0) ||
      (operation == UnaryOperation::Atanh && std::fabs(value.real()) == 1.0 && value.imag() == 0.0) ||
      (operation == UnaryOperation::Log10 && value.real() == 0.0 && value.imag() == 0.0) ||
      (operation == UnaryOperation::Exp && std::isinf(value.imag()) &&
       (std::isfinite(value.real()) || (std::isinf(value.real()) && value.real() > 0.0))) ||
      ((operation == UnaryOperation::Cosh || operation == UnaryOperation::Sinh) &&
       std::isinf(value.imag()) && !std::isnan(value.real())) ||
      ((operation == UnaryOperation::Cos || operation == UnaryOperation::Sin) &&
       std::isinf(value.real()) && !std::isnan(value.imag())) ||
      (operation == UnaryOperation::Tanh && std::isinf(value.imag()) && std::isfinite(value.real())) ||
      (operation == UnaryOperation::Tan && std::isinf(value.real()) && std::isfinite(value.imag()))) {
    error = "math domain error";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  const Complex result = apply_unary(operation, value);
  if (std::isfinite(value.real()) && std::isfinite(value.imag()) &&
      (std::isinf(result.real()) || std::isinf(result.imag()))) {
    error = "math range error";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  set_complex(out, result);
  return true;
}

bool cmath_log(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "log() expected 1 or 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Complex value;
  if (!cmath_number(runtime, args[0], "log", value, error)) return false;
  if (value.real() == 0.0 && value.imag() == 0.0) {
    error = "math domain error";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  Complex result = std::log(value);
  if (argc == 2) {
    Complex base;
    if (!cmath_number(runtime, args[1], "log", base, error)) return false;
    result /= std::log(base);
  }
  set_complex(out, result);
  return true;
}

bool cmath_phase(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "phase() expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Complex value;
  if (!cmath_number(runtime, args[0], "phase", value, error)) return false;
  value_set_number(out, std::arg(value));
  return true;
}

bool cmath_polar(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "polar() expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Complex value;
  if (!cmath_number(runtime, args[0], "polar", value, error)) return false;
  const double magnitude = std::abs(value);
  if (std::isinf(magnitude) && std::isfinite(value.real()) && std::isfinite(value.imag())) {
    error = "math range error";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  out = Value::tuple({Value::number(magnitude), Value::number(std::arg(value))});
  return true;
}

bool cmath_rect(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "rect() expected 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  double radius = 0.0;
  double angle = 0.0;
  if (!scalar_double(args[0], radius) || !scalar_double(args[1], angle)) {
    error = "rect() arguments must be real numbers";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (radius != 0.0 && !std::isnan(radius) && std::isinf(angle)) {
    error = "math domain error";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  set_complex(out, std::polar(radius, angle));
  return true;
}

enum class Predicate : uintptr_t { IsFinite, IsInf, IsNan };

bool cmath_predicate(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "cmath predicate expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Complex value;
  if (!cmath_number(runtime, args[0], "cmath", value, error)) return false;
  const auto predicate = static_cast<Predicate>(reinterpret_cast<uintptr_t>(user_data));
  bool result = false;
  if (predicate == Predicate::IsFinite) result = std::isfinite(value.real()) && std::isfinite(value.imag());
  else if (predicate == Predicate::IsInf) result = std::isinf(value.real()) || std::isinf(value.imag());
  else result = std::isnan(value.real()) || std::isnan(value.imag());
  value_set_bool(out, result);
  return true;
}

bool cmath_isclose_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "isclose() expected 2 positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  double rel_tol = 1e-9;
  double abs_tol = 0.0;
  bool have_rel = false;
  bool have_abs = false;
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
    if (*present || kwargs[index].value == nullptr || !scalar_double(*kwargs[index].value, *target)) {
      error = "isclose() tolerances must be real numbers";
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
  Complex left;
  Complex right;
  if (!cmath_number(runtime, args[0], "isclose", left, error) ||
      !cmath_number(runtime, args[1], "isclose", right, error)) return false;
  bool close = left == right;
  const bool finite = std::isfinite(left.real()) && std::isfinite(left.imag()) &&
      std::isfinite(right.real()) && std::isfinite(right.imag());
  if (!close && finite) {
    const double difference = std::abs(left - right);
    close = difference <= std::abs(rel_tol * right) ||
        difference <= std::abs(rel_tol * left) || difference <= abs_tol;
  }
  value_set_bool(out, close);
  return true;
}

bool cmath_isclose(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  return cmath_isclose_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

} // namespace

void register_cmath_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "cmath");
  auto unary = [&](const char* name, UnaryOperation operation) {
    builder.value(name, runtime.make_native_function(
        std::string("cmath.") + name,
        cmath_unary,
        reinterpret_cast<void*>(static_cast<uintptr_t>(operation)),
        nullptr,
        nullptr,
        false,
        nullptr,
        false));
  };
  unary("acos", UnaryOperation::Acos);
  unary("acosh", UnaryOperation::Acosh);
  unary("asin", UnaryOperation::Asin);
  unary("asinh", UnaryOperation::Asinh);
  unary("atan", UnaryOperation::Atan);
  unary("atanh", UnaryOperation::Atanh);
  unary("cos", UnaryOperation::Cos);
  unary("cosh", UnaryOperation::Cosh);
  unary("exp", UnaryOperation::Exp);
  unary("log10", UnaryOperation::Log10);
  unary("sin", UnaryOperation::Sin);
  unary("sinh", UnaryOperation::Sinh);
  unary("sqrt", UnaryOperation::Sqrt);
  unary("tan", UnaryOperation::Tan);
  unary("tanh", UnaryOperation::Tanh);
  builder.function("log", cmath_log)
      .function("phase", cmath_phase)
      .function("polar", cmath_polar)
      .function("rect", cmath_rect)
      .value("isclose", runtime.make_native_function(
          "cmath.isclose", cmath_isclose, nullptr, nullptr, nullptr, false, cmath_isclose_kw, false))
      .value("isfinite", runtime.make_native_function(
          "cmath.isfinite", cmath_predicate, reinterpret_cast<void*>(static_cast<uintptr_t>(Predicate::IsFinite)),
          nullptr, nullptr, false, nullptr, false))
      .value("isinf", runtime.make_native_function(
          "cmath.isinf", cmath_predicate, reinterpret_cast<void*>(static_cast<uintptr_t>(Predicate::IsInf)),
          nullptr, nullptr, false, nullptr, false))
      .value("isnan", runtime.make_native_function(
          "cmath.isnan", cmath_predicate, reinterpret_cast<void*>(static_cast<uintptr_t>(Predicate::IsNan)),
          nullptr, nullptr, false, nullptr, false))
      .value("pi", Value::number(3.14159265358979323846))
      .value("e", Value::number(2.71828182845904523536))
      .value("tau", Value::number(6.28318530717958647692))
      .value("inf", Value::number(std::numeric_limits<double>::infinity()))
      .value("infj", Value::complex(0.0, std::numeric_limits<double>::infinity()))
      .value("nan", Value::number(std::numeric_limits<double>::quiet_NaN()))
      .value("nanj", Value::complex(0.0, std::numeric_limits<double>::quiet_NaN()));
  runtime.register_module("cmath", builder.finish());
}

} // namespace xlang3
