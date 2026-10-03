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

#include "xlang3/attribute.h"
#include "xlang3/builtin_methods.h"
#include "xlang3/contextvars_runtime.h"
#include "xlang3/decimal_runtime.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <vector>

namespace xlang3 {
namespace {

std::atomic<uint64_t> g_decimal_binary_fast{0};
std::atomic<uint64_t> g_decimal_binary_fallback{0};
std::atomic<uint64_t> g_decimal_quantize_fast{0};
std::atomic<uint64_t> g_decimal_quantize_fallback{0};
std::atomic<bool> g_decimal_profile_reported{false};
std::atomic<uint64_t> g_decimal_profile_operands_ns{0};
std::atomic<uint64_t> g_decimal_profile_context_ns{0};
std::atomic<uint64_t> g_decimal_profile_arithmetic_ns{0};
std::atomic<uint64_t> g_decimal_profile_rounding_ns{0};
std::atomic<uint64_t> g_decimal_profile_result_ns{0};

using DecimalProfileClock = std::chrono::steady_clock;

void decimal_profile_add_elapsed(
    std::atomic<uint64_t>& accumulator,
    DecimalProfileClock::time_point start) {
  accumulator.fetch_add(static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          DecimalProfileClock::now() - start).count()), std::memory_order_relaxed);
}

enum class DecimalBinaryFallbackReason : size_t {
  LeftOperand,
  RightOperand,
  Context,
  Exponent,
  Coefficient,
  Rounding,
  Result,
  Count,
};

std::array<std::atomic<uint64_t>, static_cast<size_t>(DecimalBinaryFallbackReason::Count)>
    g_decimal_binary_fallback_reasons{};

struct DecimalModuleState {
  Runtime* runtime = nullptr;
  Value fallback_module;
  bool initialized = false;
};

struct DecimalOperationState {
  Value decimal_class;
  Value original;
  Value getcontext;
  Value current_context_getter;
  Value fallback_module;
  // Cached only when bound ContextVar.get is the unmodified XLang3 native
  // callback. current_context_getter owns the variable for this key's lifetime.
  Object* current_context_key = nullptr;
  uint32_t exp_slot = 0;
  uint32_t int_slot = 0;
  uint32_t sign_slot = 0;
  uint32_t special_slot = 0;
  bool multiply = false;
  bool quantize = false;
  bool context_quantize = false;
  bool profile = false;
  DecimalBinaryFallbackReason binary_fallback_reason = DecimalBinaryFallbackReason::LeftOperand;
};

bool decimal_current_context(
    Runtime& runtime,
    const DecimalOperationState& state,
    Value& context) {
  // The native _decimal boundary retains the original bound ContextVar.get
  // for compatibility, but ordinary arithmetic can read its exact active
  // binding directly. This avoids generic callable dispatch on every op while
  // preserving get(None)'s missing-value and custom-method fallback behavior.
  if (contextvar_lookup_if_set(state.current_context_key, context)) {
    return true;
  }
  std::string context_error;
  const Value no_context = Value::none();
  if (state.current_context_getter.tag != ValueTag::Invalid &&
      runtime_call_callable(runtime, state.current_context_getter, &no_context, 1, context, context_error)) {
    return context.tag != ValueTag::None;
  }
  return state.getcontext.tag != ValueTag::Invalid &&
      runtime_call_callable(runtime, state.getcontext, nullptr, 0, context, context_error);
}

void decimal_note_binary_fallback(DecimalOperationState* state) {
  if (state != nullptr && state->profile) {
    g_decimal_binary_fallback_reasons[static_cast<size_t>(state->binary_fallback_reason)]
        .fetch_add(1, std::memory_order_relaxed);
  }
}

void decimal_module_state_cleanup(void* data) {
  if (std::getenv("XLANG3_DECIMAL_PROFILE") != nullptr &&
      !g_decimal_profile_reported.exchange(true, std::memory_order_relaxed)) {
    std::fprintf(stderr,
        "decimal native fast/fallback counts: binary=%llu/%llu quantize=%llu/%llu\n",
        static_cast<unsigned long long>(g_decimal_binary_fast.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_decimal_binary_fallback.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_decimal_quantize_fast.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_decimal_quantize_fallback.load(std::memory_order_relaxed)));
    static constexpr const char* reason_names[] = {
        "left_operand", "right_operand", "context", "exponent",
        "coefficient", "rounding", "result"};
    for (size_t i = 0; i < static_cast<size_t>(DecimalBinaryFallbackReason::Count); ++i) {
      const uint64_t count = g_decimal_binary_fallback_reasons[i].load(std::memory_order_relaxed);
      if (count != 0) {
        std::fprintf(stderr, "decimal binary fallback %s=%llu\n", reason_names[i],
            static_cast<unsigned long long>(count));
      }
    }
    if (std::getenv("XLANG3_DECIMAL_PROFILE_TIMING") != nullptr) {
      std::fprintf(stderr,
          "decimal fast-path stage ns: operands=%llu context=%llu arithmetic=%llu rounding=%llu result=%llu\n",
          static_cast<unsigned long long>(g_decimal_profile_operands_ns.load(std::memory_order_relaxed)),
          static_cast<unsigned long long>(g_decimal_profile_context_ns.load(std::memory_order_relaxed)),
          static_cast<unsigned long long>(g_decimal_profile_arithmetic_ns.load(std::memory_order_relaxed)),
          static_cast<unsigned long long>(g_decimal_profile_rounding_ns.load(std::memory_order_relaxed)),
          static_cast<unsigned long long>(g_decimal_profile_result_ns.load(std::memory_order_relaxed)));
    }
  }
  delete static_cast<DecimalModuleState*>(data);
}

void decimal_operation_state_cleanup(void* data) {
  delete static_cast<DecimalOperationState*>(data);
}

bool decimal_class_slot(const ClassObject& klass, const char* name, uint32_t& slot) {
  const auto found = klass.instance_slot_indices.find(name);
  if (found == klass.instance_slot_indices.end()) return false;
  slot = found->second;
  return true;
}

bool decimal_parts_view(
    const Value& value,
    const DecimalOperationState& state,
    bool& negative,
    Value& digits_owner,
    std::string_view& digits,
    int64_t& exponent) {
  auto* instance = value_as_instance(value);
  auto* expected_class = value_as_class(state.decimal_class);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr || klass != expected_class) return false;
  if (state.exp_slot >= instance_slot_count(instance) ||
      state.int_slot >= instance_slot_count(instance) ||
      state.sign_slot >= instance_slot_count(instance) ||
      state.special_slot >= instance_slot_count(instance)) {
    return false;
  }
  const Value& exp_value = instance_slot_at(instance, state.exp_slot);
  const Value& digits_value = instance_slot_at(instance, state.int_slot);
  const Value& sign_value = instance_slot_at(instance, state.sign_slot);
  const Value& special_value = instance_slot_at(instance, state.special_slot);
  // Keep the underlying string alive even if a custom Context accessor runs
  // Python code later in this operation and mutates a Decimal slot.
  value_assign_fast(digits_owner, digits_value);
  const auto* digits_object = value_as_string(digits_owner);
  if (exp_value.tag != ValueTag::Int64 || digits_object == nullptr ||
      sign_value.tag != ValueTag::Int64 || special_value.tag != ValueTag::Bool ||
      special_value.as.b) {
    return false;
  }
  const std::string_view digits_view = string_object_view(*digits_object);
  if (digits_view.empty()) return false;
  for (const char c : digits_view) {
    if (c < '0' || c > '9') return false;
  }
  negative = sign_value.as.i64 != 0;
  // The Decimal instance in the caller's live argument/register owns this
  // immutable coefficient for the whole native operation. Borrow it here so
  // the common arithmetic path avoids allocating and copying both operands;
  // only aligned operands and the result need owned storage.
  digits = digits_view;
  exponent = exp_value.as.i64;
  return true;
}

bool decimal_string_fast(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const DecimalOperationState& state,
    Value& out) {
  if (argc != 1) return false;
  auto* instance = value_as_instance(args[0]);
  auto* expected_class = value_as_class(state.decimal_class);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr || klass != expected_class ||
      state.sign_slot >= instance_slot_count(instance)) {
    return false;
  }
  const Value& sign_value = instance_slot_at(instance, state.sign_slot);
  if (sign_value.tag != ValueTag::Int64 ||
      (sign_value.as.i64 != 0 && sign_value.as.i64 != 1)) {
    return false;
  }

  bool negative = false;
  Value digits_owner = Value::invalid();
  std::string_view digits;
  int64_t exponent = 0;
  // Special values and nonstandard instance layouts stay on the original
  // _pydecimal implementation; this path covers exact finite Decimal values.
  if (!decimal_parts_view(
          args[0], state, negative, digits_owner, digits, exponent) ||
      digits.size() > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  const int64_t digit_count = static_cast<int64_t>(digits.size());
  if (exponent > std::numeric_limits<int64_t>::max() - digit_count ||
      exponent < std::numeric_limits<int64_t>::min() + digit_count) {
    return false;
  }
  const int64_t left_digits = exponent + digit_count;
  // Match _pydecimal.Decimal.__str__: small values use fixed notation;
  // otherwise one coefficient digit precedes the point.
  const int64_t dot_place = exponent <= 0 && left_digits > -6 ? left_digits : 1;

  std::string text;
  text.reserve(digits.size() + 16);
  if (negative) text.push_back('-');
  if (dot_place <= 0) {
    text.push_back('0');
    text.push_back('.');
    text.append(static_cast<size_t>(-dot_place), '0');
    text.append(digits);
  } else if (dot_place >= digit_count) {
    text.append(digits);
    text.append(static_cast<size_t>(dot_place - digit_count), '0');
  } else {
    const size_t split = static_cast<size_t>(dot_place);
    text.append(digits.substr(0, split));
    text.push_back('.');
    text.append(digits.substr(split));
  }

  if (left_digits != dot_place) {
    if (dot_place > 0 &&
        left_digits < std::numeric_limits<int64_t>::min() + dot_place) {
      return false;
    }
    const int64_t scientific_exponent = left_digits - dot_place;
    Value context;
    std::string ignored;
    if (!decimal_current_context(runtime, state, context)) return false;
    Value capitals;
    if (!object_get_attr(context, "capitals", capitals, ignored) ||
        capitals.tag != ValueTag::Int64 ||
        (capitals.as.i64 != 0 && capitals.as.i64 != 1)) {
      return false;
    }
    text.push_back(capitals.as.i64 != 0 ? 'E' : 'e');
    if (scientific_exponent >= 0) text.push_back('+');
    text += std::to_string(scientific_exponent);
  }
  out = Value::string(std::move(text));
  return true;
}

bool decimal_parts(
    const Value& value,
    const DecimalOperationState& state,
    bool& negative,
    std::string& digits,
    int64_t& exponent) {
  std::string_view digits_view;
  Value digits_owner = Value::invalid();
  if (!decimal_parts_view(value, state, negative, digits_owner, digits_view, exponent)) return false;
  digits.assign(digits_view);
  return true;
}

bool integer_parts(const Value& value, bool& negative, std::string& digits) {
  if (value.tag == ValueTag::Bool) {
    negative = false;
    digits = value.as.b ? "1" : "0";
    return value.as.b;
  }
  if (value.tag == ValueTag::Int64) {
    const int64_t number = value.as.i64;
    if (number == 0) return false;
    negative = number < 0;
    const uint64_t magnitude = negative
        ? static_cast<uint64_t>(-(number + 1)) + 1
        : static_cast<uint64_t>(number);
    digits = std::to_string(magnitude);
    return true;
  }
  if (value_as_bigint(value) == nullptr) return false;
  std::string text = value_bigint_to_string(value);
  if (text.empty() || text == "0") return false;
  negative = text.front() == '-';
  if (negative) text.erase(text.begin());
  digits = std::move(text);
  return true;
}

int compare_decimal_coefficients(std::string_view left, std::string_view right) {
  while (left.size() > 1 && left.front() == '0') left.remove_prefix(1);
  while (right.size() > 1 && right.front() == '0') right.remove_prefix(1);
  if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
  const int compared = left.compare(right);
  return compared < 0 ? -1 : (compared > 0 ? 1 : 0);
}

std::string add_decimal_coefficients(std::string_view left, std::string_view right) {
  const size_t width = std::max(left.size(), right.size());
  std::string result(width + 1, '0');
  int carry = 0;
  for (size_t offset = 0; offset < width; ++offset) {
    const int a = offset < left.size() ? left[left.size() - 1 - offset] - '0' : 0;
    const int b = offset < right.size() ? right[right.size() - 1 - offset] - '0' : 0;
    const int sum = a + b + carry;
    result[width - offset] = static_cast<char>('0' + sum % 10);
    carry = sum / 10;
  }
  result[0] = static_cast<char>('0' + carry);
  if (result.front() == '0') result.erase(result.begin());
  return result;
}

std::string subtract_decimal_coefficients(std::string_view larger, std::string_view smaller) {
  std::string result(larger);
  int borrow = 0;
  for (size_t offset = 0; offset < larger.size(); ++offset) {
    const size_t index = larger.size() - 1 - offset;
    int digit = larger[index] - '0' - borrow;
    const int subtrahend = offset < smaller.size()
        ? smaller[smaller.size() - 1 - offset] - '0' : 0;
    if (digit < subtrahend) {
      digit += 10;
      borrow = 1;
    } else {
      borrow = 0;
    }
    result[index] = static_cast<char>('0' + digit - subtrahend);
  }
  const size_t first_nonzero = result.find_first_not_of('0');
  return first_nonzero == std::string::npos ? "0" : result.substr(first_nonzero);
}

bool add_signed_decimal_coefficients(
    std::string_view left,
    bool left_negative,
    std::string_view right,
    bool right_negative,
    bool& result_negative,
    std::string& result) {
  if (left_negative == right_negative) {
    result_negative = left_negative;
    result = add_decimal_coefficients(left, right);
  } else {
    const int order = compare_decimal_coefficients(left, right);
    if (order == 0) {
      result_negative = false;
      result = "0";
    } else if (order > 0) {
      result_negative = left_negative;
      result = subtract_decimal_coefficients(left, right);
    } else {
      result_negative = right_negative;
      result = subtract_decimal_coefficients(right, left);
    }
  }
  if (result == "0") {
    // Preserve same-sign zero additions; opposite-sign cancellation is kept on
    // the Python compatibility path because its sign depends on Context.rounding.
    result_negative = left_negative == right_negative ? left_negative : false;
  }
  return true;
}

bool multiply_decimal_coefficients(
    std::string_view left,
    std::string_view right,
    bool& result_negative,
    std::string& result) {
  // The financial benchmark uses short coefficients. Keep the native path
  // bounded; very large arbitrary-precision products use _pydecimal's exact
  // implementation instead of quadratic temporary storage here.
  constexpr size_t kMaxFastCoefficientDigits = 2048;
  if (left.empty() || right.empty() || left.size() > kMaxFastCoefficientDigits ||
      right.size() > kMaxFastCoefficientDigits) return false;
  if (left == "0" || right == "0") {
    result_negative = false;
    result = "0";
    return true;
  }
  std::vector<uint8_t> product(left.size() + right.size(), 0);
  for (size_t i = left.size(); i-- > 0;) {
    const uint32_t a = static_cast<uint32_t>(left[i] - '0');
    for (size_t j = right.size(); j-- > 0;) {
      const uint32_t b = static_cast<uint32_t>(right[j] - '0');
      const size_t low = i + j + 1;
      const uint32_t sum = product[low] + a * b;
      product[low] = static_cast<uint8_t>(sum % 10);
      product[low - 1] = static_cast<uint8_t>(product[low - 1] + sum / 10);
    }
  }
  size_t first = 0;
  while (first + 1 < product.size() && product[first] == 0) ++first;
  result.clear();
  result.reserve(product.size() - first);
  for (; first < product.size(); ++first) result.push_back(static_cast<char>('0' + product[first]));
  result_negative = false;
  return true;
}

bool decimal_context_limits(
    Runtime& runtime,
    const DecimalOperationState& state,
    const Value* supplied_context,
    int64_t& precision,
    int64_t& emin,
    int64_t& emax,
    int64_t& etiny,
    int64_t& etop,
    Value* context_out = nullptr) {
  Value context;
  if (supplied_context != nullptr && supplied_context->tag != ValueTag::None) {
    context = *supplied_context;
  } else if (!decimal_current_context(runtime, state, context)) {
    // If the variable is unset, the caller falls back to Python so getcontext
    // installs a default exactly as the pure-Python implementation requires.
    return false;
  }
  Value prec_value;
  Value emin_value;
  Value emax_value;
  Value clamp_value;
  std::string ignored;
  if (!object_get_attr(context, "prec", prec_value, ignored) ||
      !object_get_attr(context, "Emin", emin_value, ignored) ||
      !object_get_attr(context, "Emax", emax_value, ignored) ||
      !object_get_attr(context, "clamp", clamp_value, ignored) ||
      prec_value.tag != ValueTag::Int64 || emin_value.tag != ValueTag::Int64 ||
      emax_value.tag != ValueTag::Int64 || clamp_value.tag != ValueTag::Int64) {
    return false;
  }
  precision = prec_value.as.i64;
  emin = emin_value.as.i64;
  emax = emax_value.as.i64;
  if (precision <= 0 || precision > 1000000 ||
      emin < std::numeric_limits<int64_t>::min() + precision ||
      emax > std::numeric_limits<int64_t>::max() - precision) {
    return false;
  }
  etiny = emin - precision + 1;
  etop = clamp_value.as.i64 == 0 ? emax : emax - precision + 1;
  if (context_out != nullptr) value_assign_fast(*context_out, context);
  return true;
}

bool decimal_exact_result_in_context(
    const std::string& digits,
    int64_t exponent,
    int64_t precision,
    int64_t emin,
    int64_t emax,
    int64_t etiny,
    int64_t etop) {
  if (digits.empty() || digits.size() > static_cast<size_t>(precision) ||
      exponent < etiny || exponent > etop ||
      exponent > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(digits.size())) {
    return false;
  }
  const int64_t adjusted = exponent + static_cast<int64_t>(digits.size()) - 1;
  return adjusted >= emin && adjusted <= emax;
}

bool decimal_round_result_in_context(
    Runtime& runtime,
    const DecimalOperationState& state,
    const Value& context,
    bool negative,
    std::string& digits,
    int64_t& exponent,
    int64_t precision,
    int64_t emin,
    int64_t emax,
    int64_t etiny,
    int64_t etop) {
  if (decimal_exact_result_in_context(digits, exponent, precision, emin, emax, etiny, etop)) return true;
  if (digits.empty() || digits.size() > 100000 || exponent < etiny || exponent > etop) return false;
  const int64_t adjusted = exponent + static_cast<int64_t>(digits.size()) - 1;
  if (adjusted > emax || adjusted < emin) return false;
  const size_t drop = digits.size() > static_cast<size_t>(precision)
      ? digits.size() - static_cast<size_t>(precision) : 0;
  if (drop == 0 || drop > static_cast<size_t>(std::numeric_limits<int64_t>::max()) ||
      exponent > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(drop)) return false;
  const size_t keep = digits.size() - drop;
  const char first_discarded = digits[keep];
  const bool later_nonzero = digits.find_first_not_of('0', keep + 1) != std::string::npos;
  const bool discarded_nonzero = first_discarded != '0' || later_nonzero;
  const bool greater_half = first_discarded > '5' ||
      (first_discarded == '5' && later_nonzero);
  const bool exact_half = first_discarded == '5' && !later_nonzero;

  Value rounding_value;
  std::string ignored;
  if (!object_get_attr(context, "rounding", rounding_value, ignored) ||
      value_as_string(rounding_value) == nullptr) return false;
  const std::string rounding_name = string_object_to_string(*value_as_string(rounding_value));
  std::string result = keep == 0 ? "0" : digits.substr(0, keep);
  bool increment = false;
  if (discarded_nonzero) {
    if (rounding_name == "ROUND_DOWN") increment = false;
    else if (rounding_name == "ROUND_UP") increment = true;
    else if (rounding_name == "ROUND_CEILING") increment = !negative;
    else if (rounding_name == "ROUND_FLOOR") increment = negative;
    else if (rounding_name == "ROUND_HALF_UP") increment = greater_half || exact_half;
    else if (rounding_name == "ROUND_HALF_DOWN") increment = greater_half;
    else if (rounding_name == "ROUND_HALF_EVEN") {
      const bool retained_odd = !result.empty() && ((result.back() - '0') & 1) != 0;
      increment = greater_half || (exact_half && retained_odd);
    } else if (rounding_name == "ROUND_05UP") {
      increment = result.empty() || result == "0" || result.back() == '0' || result.back() == '5';
    } else {
      return false;
    }
  }
  if (increment) {
    size_t index = result.size();
    while (index > 0 && result[index - 1] == '9') {
      result[index - 1] = '0';
      --index;
    }
    if (index == 0) result.insert(result.begin(), '1');
    else ++result[index - 1];
  }
  exponent += static_cast<int64_t>(drop);
  // A carry such as 99.9 -> 100 has one extra coefficient digit. Its final
  // zero is representational, so shift it into the exponent without changing
  // the rounded numeric value.
  if (result.size() > static_cast<size_t>(precision) && result.back() == '0') {
    result.pop_back();
    if (exponent == std::numeric_limits<int64_t>::max()) return false;
    ++exponent;
  }
  if (result.empty() || result.size() > static_cast<size_t>(precision) ||
      exponent < etiny || exponent > etop || exponent > std::numeric_limits<int64_t>::max() -
          static_cast<int64_t>(result.size())) return false;
  const bool zero = result.find_first_not_of('0') == std::string::npos;
  const int64_t result_adjusted = exponent + static_cast<int64_t>(result.size()) - 1;
  if (!zero && (result_adjusted > emax || result_adjusted < emin)) return false;

  // Context traps must retain priority over flag mutation. If either signal is
  // trapped, let the original Python method raise the exact Decimal exception.
  Value ignored_flags;
  Value flags;
  Value traps;
  if (!object_get_attr(context, "_ignored_flags", ignored_flags, ignored) ||
      value_as_list(ignored_flags) == nullptr || !value_as_list(ignored_flags)->items.empty() ||
      !object_get_attr(context, "flags", flags, ignored) ||
      !object_get_attr(context, "traps", traps, ignored)) return false;
  Value rounded_signal;
  Value inexact_signal;
  if (!module_get_attr(state.fallback_module, "Rounded", rounded_signal, ignored) ||
      !module_get_attr(state.fallback_module, "Inexact", inexact_signal, ignored)) return false;
  Value rounded_trap;
  Value inexact_trap;
  if (!mapping_get_item(traps, rounded_signal, rounded_trap, ignored) ||
      (discarded_nonzero && !mapping_get_item(traps, inexact_signal, inexact_trap, ignored)) ||
      value_truthy(rounded_trap) || (discarded_nonzero && value_truthy(inexact_trap))) return false;
  if (!mapping_set_item(flags, rounded_signal, Value::int64(1), ignored) ||
      (discarded_nonzero && !mapping_set_item(flags, inexact_signal, Value::int64(1), ignored))) {
    return false;
  }
  digits = std::move(result);
  return true;
}

bool assign_decimal_result(
    const DecimalOperationState& state,
    bool negative,
    std::string digits,
    int64_t exponent,
    Value& out) {
  out = Value::instance(state.decimal_class);
  auto* instance = value_as_instance(out);
  if (instance == nullptr || state.exp_slot >= instance_slot_count(instance) ||
      state.int_slot >= instance_slot_count(instance) || state.sign_slot >= instance_slot_count(instance) ||
      state.special_slot >= instance_slot_count(instance)) return false;
  // Decimal values are immutable after construction. Populate the known base
  // class slots directly so every arithmetic result avoids four descriptor
  // lookups and cannot accidentally invoke a Python override.
  value_assign_fast(instance_slot_at(instance, state.sign_slot), Value::int64(negative ? 1 : 0));
  value_assign_fast(instance_slot_at(instance, state.int_slot), Value::string(std::move(digits)));
  value_assign_fast(instance_slot_at(instance, state.exp_slot), Value::int64(exponent));
  value_assign_fast(instance_slot_at(instance, state.special_slot), Value::boolean(false));
  return true;
}

bool decimal_binary_fast(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const Value* supplied_context,
    DecimalOperationState& state,
    Value& out) {
  if (argc < 2 || argc > 3) return false;
  const bool profile_timing = state.profile &&
      std::getenv("XLANG3_DECIMAL_PROFILE_TIMING") != nullptr;
  auto stage_start = profile_timing ? DecimalProfileClock::now() : DecimalProfileClock::time_point{};
  bool left_negative = false;
  Value left_digits_owner = Value::invalid();
  std::string_view left_digits;
  int64_t left_exp = 0;
  state.binary_fallback_reason = DecimalBinaryFallbackReason::LeftOperand;
  if (!decimal_parts_view(
          args[0], state, left_negative, left_digits_owner, left_digits, left_exp)) {
    return false;
  }
  bool right_negative = false;
  Value right_digits_owner = Value::invalid();
  std::string owned_right_digits;
  std::string_view right_digits;
  int64_t right_exp = 0;
  if (!decimal_parts_view(
          args[1], state, right_negative, right_digits_owner, right_digits, right_exp)) {
    if (state.multiply && integer_parts(args[1], right_negative, owned_right_digits)) {
      right_digits = owned_right_digits;
      right_exp = 0;
    } else {
      state.binary_fallback_reason = DecimalBinaryFallbackReason::RightOperand;
      return false;
    }
  }
  if (profile_timing) {
    decimal_profile_add_elapsed(g_decimal_profile_operands_ns, stage_start);
    stage_start = DecimalProfileClock::now();
  }
  int64_t precision = 0;
  int64_t emin = 0;
  int64_t emax = 0;
  int64_t etiny = 0;
  int64_t etop = 0;
  Value context_value;
  const Value* context = supplied_context;
  if (context == nullptr && argc == 3) context = &args[2];
  state.binary_fallback_reason = DecimalBinaryFallbackReason::Context;
  if (!decimal_context_limits(runtime, state, context,
          precision, emin, emax, etiny, etop, &context_value)) {
    return false;
  }
  if (profile_timing) {
    decimal_profile_add_elapsed(g_decimal_profile_context_ns, stage_start);
    stage_start = DecimalProfileClock::now();
  }

  std::string result_digits;
  bool result_negative = false;
  int64_t result_exp = 0;
  if (state.multiply) {
    state.binary_fallback_reason = DecimalBinaryFallbackReason::Exponent;
    if ((right_exp > 0 && left_exp > std::numeric_limits<int64_t>::max() - right_exp) ||
        (right_exp < 0 && left_exp < std::numeric_limits<int64_t>::min() - right_exp)) {
      return false;
    }
    result_exp = left_exp + right_exp;
    state.binary_fallback_reason = DecimalBinaryFallbackReason::Coefficient;
    if (!multiply_decimal_coefficients(left_digits, right_digits, result_negative, result_digits)) return false;
    // Decimal multiplication preserves the XOR sign even when its coefficient
    // is zero (for example Decimal("-0") * Decimal("2") is Decimal("-0")).
    result_negative = left_negative != right_negative;
  } else {
    state.binary_fallback_reason = DecimalBinaryFallbackReason::Exponent;
    result_exp = std::min(left_exp, right_exp);
    if ((left_exp > result_exp && result_exp < 0 &&
         left_exp > std::numeric_limits<int64_t>::max() + result_exp) ||
        (right_exp > result_exp && result_exp < 0 &&
         right_exp > std::numeric_limits<int64_t>::max() + result_exp)) {
      return false;
    }
    const int64_t left_zeroes = left_exp - result_exp;
    const int64_t right_zeroes = right_exp - result_exp;
    if (left_zeroes > 100000 || right_zeroes > 100000 ||
        left_digits.size() + static_cast<size_t>(left_zeroes) > 100000 ||
        right_digits.size() + static_cast<size_t>(right_zeroes) > 100000) {
      return false;
    }
    std::string left_aligned;
    std::string right_aligned;
    if (left_zeroes != 0) {
      left_aligned.assign(left_digits);
      left_aligned.append(static_cast<size_t>(left_zeroes), '0');
      left_digits = left_aligned;
    }
    if (right_zeroes != 0) {
      right_aligned.assign(right_digits);
      right_aligned.append(static_cast<size_t>(right_zeroes), '0');
      right_digits = right_aligned;
    }
    add_signed_decimal_coefficients(
        left_digits, left_negative, right_digits, right_negative,
        result_negative, result_digits);
    if (result_digits == "0" && left_negative != right_negative) {
      // Exact cancellation produces -0 under ROUND_FLOOR and +0 under the
      // other modes. Keep this uncommon edge case on _pydecimal so flags,
      // traps, and the sign all follow the active Context exactly.
      return false;
    }
  }
  if (profile_timing) {
    decimal_profile_add_elapsed(g_decimal_profile_arithmetic_ns, stage_start);
    stage_start = DecimalProfileClock::now();
  }
  state.binary_fallback_reason = DecimalBinaryFallbackReason::Rounding;
  if (!decimal_round_result_in_context(
          runtime, state, context_value, result_negative, result_digits, result_exp,
          precision, emin, emax, etiny, etop)) {
    return false;
  }
  if (profile_timing) {
    decimal_profile_add_elapsed(g_decimal_profile_rounding_ns, stage_start);
    stage_start = DecimalProfileClock::now();
  }
  state.binary_fallback_reason = DecimalBinaryFallbackReason::Result;
  const bool assigned = assign_decimal_result(
      state, result_negative, std::move(result_digits), result_exp, out);
  if (profile_timing) decimal_profile_add_elapsed(g_decimal_profile_result_ns, stage_start);
  return assigned;
}

bool decimal_binary(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* data) {
  auto* state = static_cast<DecimalOperationState*>(data);
  if (state != nullptr && decimal_binary_fast(runtime, args, argc, nullptr, *state, out)) {
    if (state->profile) g_decimal_binary_fast.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  if (state != nullptr && state->profile) g_decimal_binary_fallback.fetch_add(1, std::memory_order_relaxed);
  decimal_note_binary_fallback(state);
  return state != nullptr && runtime_call_callable(
      runtime, state->original, args, argc, out, error);
}

bool decimal_string(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* data) {
  auto* state = static_cast<DecimalOperationState*>(data);
  if (state != nullptr && decimal_string_fast(runtime, args, argc, *state, out)) {
    return true;
  }
  return state != nullptr && runtime_call_callable(
      runtime, state->original, args, argc, out, error);
}

bool decimal_string_keywords(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* data) {
  auto* state = static_cast<DecimalOperationState*>(data);
  if (state == nullptr) return false;
  if (kwargc == 0) return decimal_string(runtime, args, argc, out, error, data);
  std::vector<std::pair<std::string, Value>> keyword_values;
  keyword_values.reserve(kwargc);
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (kwargs[index].name == nullptr || kwargs[index].value == nullptr) return false;
    keyword_values.emplace_back(kwargs[index].name, *kwargs[index].value);
  }
  return runtime_call_callable_kw(
      runtime, state->original, args, argc, keyword_values, out, error);
}

bool decimal_binary_keywords(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* data) {
  Value context;
  bool has_context = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || std::string_view(kwargs[i].name) != "context") {
      error = std::string("unexpected keyword argument '") +
          (kwargs[i].name == nullptr ? "" : kwargs[i].name) + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (has_context || kwargs[i].value == nullptr || argc >= 3) {
      error = "got multiple values for keyword argument 'context'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    context = *kwargs[i].value;
    has_context = true;
  }
  auto* state = static_cast<DecimalOperationState*>(data);
  if (state != nullptr && kwargc <= 1 && decimal_binary_fast(
          runtime, args, argc, has_context ? &context : nullptr, *state, out)) {
    if (state->profile) g_decimal_binary_fast.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  if (state != nullptr && state->profile) g_decimal_binary_fallback.fetch_add(1, std::memory_order_relaxed);
  decimal_note_binary_fallback(state);
  // Decimal.__add__ and __mul__ accept only the optional context keyword.
  // Rebuild that argument positionally before falling back to the Python
  // implementation so the compatibility path retains its normal semantics.
  if (has_context && argc == 2) {
    Value fallback_args[] = {args[0], args[1], context};
    return state != nullptr && runtime_call_callable(
        runtime, state->original, fallback_args, 3, out, error);
  }
  return state != nullptr && runtime_call_callable(
      runtime, state->original, args, argc, out, error);
}

bool decimal_quantize_fast(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const Value* rounding_override,
    const Value* supplied_context,
    DecimalOperationState& state,
    Value& out) {
  if (argc < 2 || argc > 4) return false;
  bool negative = false;
  std::string digits;
  int64_t exponent = 0;
  if (!decimal_parts(args[0], state, negative, digits, exponent)) return false;
  int64_t target_exp = 0;
  // The exponent operand's coefficient is ignored; only its finite status
  // and stored exponent are relevant to quantize.
  auto* exp_instance = value_as_instance(args[1]);
  auto* decimal_klass = value_as_class(state.decimal_class);
  if (exp_instance == nullptr || decimal_klass == nullptr ||
      value_as_class(exp_instance->klass) != decimal_klass) return false;
  if (state.exp_slot >= instance_slot_count(exp_instance) ||
      state.special_slot >= instance_slot_count(exp_instance)) {
    return false;
  }
  const Value& exp_value = instance_slot_at(exp_instance, state.exp_slot);
  const Value& special_value = instance_slot_at(exp_instance, state.special_slot);
  if (exp_value.tag != ValueTag::Int64 || special_value.tag != ValueTag::Bool || special_value.as.b) {
    return false;
  }
  target_exp = exp_value.as.i64;

  const Value* context_arg = supplied_context;
  if (context_arg == nullptr && argc >= 4) context_arg = &args[3];
  int64_t precision = 0, emin = 0, emax = 0, etiny = 0, etop = 0;
  if (!decimal_context_limits(runtime, state, context_arg,
          precision, emin, emax, etiny, etop)) return false;
  if (target_exp < etiny || target_exp > emax ||
      (target_exp > etop && etop != emax)) return false;
  if (digits.size() > static_cast<size_t>(std::numeric_limits<int64_t>::max()) ||
      exponent > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(digits.size())) {
    return false;
  }
  const int64_t adjusted = exponent + static_cast<int64_t>(digits.size()) - 1;
  if (adjusted > emax ||
      static_cast<long double>(adjusted) - static_cast<long double>(target_exp) + 1.0L >
          static_cast<long double>(precision)) return false;

  std::string rounding_name;
  const Value* rounding = rounding_override;
  if (rounding == nullptr && argc >= 3) rounding = &args[2];
  if (rounding == nullptr || rounding->tag == ValueTag::None) {
    Value context;
    if (context_arg != nullptr && context_arg->tag != ValueTag::None) context = *context_arg;
    else if (!decimal_current_context(runtime, state, context)) return false;
    Value context_rounding;
    std::string ignored;
    if (!object_get_attr(context, "rounding", context_rounding, ignored)) return false;
    rounding_name = value_to_string(context_rounding);
  } else if (value_as_string(*rounding) != nullptr) {
    rounding_name = string_object_to_string(*value_as_string(*rounding));
  } else {
    return false;
  }

  std::string result_digits;
  bool inexact = false;
  bool rounded = target_exp > exponent;
  bool increment = false;
  if (target_exp <= exponent) {
    const int64_t zeros = exponent - target_exp;
    if (zeros > 100000 || digits.size() + static_cast<size_t>(zeros) > 100000) return false;
    result_digits = digits;
    result_digits.append(static_cast<size_t>(zeros), '0');
  } else {
    const int64_t dropped = target_exp - exponent;
    if (dropped > 100000) return false;
    const size_t keep = dropped >= static_cast<int64_t>(digits.size())
        ? 0
        : digits.size() - static_cast<size_t>(dropped);
    result_digits = keep == 0 ? "0" : digits.substr(0, keep);
    const bool has_nonzero = digits.find_first_not_of('0', keep) != std::string::npos;
    inexact = has_nonzero;
    const char first_discarded = dropped > static_cast<int64_t>(digits.size())
        ? '0' : digits[keep];
    const size_t after_first = dropped > static_cast<int64_t>(digits.size())
        ? digits.size() : std::min(digits.size(), keep + 1);
    const bool later_nonzero = digits.find_first_not_of('0', after_first) != std::string::npos;
    const bool greater_half = first_discarded > '5' ||
        (first_discarded == '5' && later_nonzero);
    const bool exact_half = first_discarded == '5' && !later_nonzero;
    if (inexact) {
      if (rounding_name == "ROUND_DOWN") increment = false;
      else if (rounding_name == "ROUND_UP") increment = true;
      else if (rounding_name == "ROUND_CEILING") increment = !negative;
      else if (rounding_name == "ROUND_FLOOR") increment = negative;
      else if (rounding_name == "ROUND_HALF_UP") increment = greater_half || exact_half;
      else if (rounding_name == "ROUND_HALF_DOWN") increment = greater_half;
      else if (rounding_name == "ROUND_HALF_EVEN") {
        const bool retained_odd = !result_digits.empty() && ((result_digits.back() - '0') & 1) != 0;
        increment = greater_half || (exact_half && retained_odd);
      } else if (rounding_name == "ROUND_05UP") {
        increment = result_digits.empty() || result_digits == "0" ||
            result_digits.back() == '0' || result_digits.back() == '5';
      } else {
        return false;
      }
    }
    if (increment) {
      size_t index = result_digits.size();
      while (index > 0 && result_digits[index - 1] == '9') {
        result_digits[index - 1] = '0';
        --index;
      }
      if (index == 0) result_digits.insert(result_digits.begin(), '1');
      else ++result_digits[index - 1];
    }
  }
  if (result_digits.empty() || result_digits.size() > static_cast<size_t>(precision) ||
      target_exp > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(result_digits.size())) {
    return false;
  }
  const bool result_zero = result_digits.find_first_not_of('0') == std::string::npos;
  const int64_t result_adjusted = target_exp + static_cast<int64_t>(result_digits.size()) - 1;
  if (!result_zero && (result_adjusted > emax || result_adjusted < emin)) return false;

  if (rounded) {
    Value context;
    if (context_arg != nullptr && context_arg->tag != ValueTag::None) context = *context_arg;
    else if (!decimal_current_context(runtime, state, context)) return false;
    Value ignored_flags;
    Value flags;
    Value traps;
    std::string ignored;
    if (!object_get_attr(context, "_ignored_flags", ignored_flags, ignored) ||
        value_as_list(ignored_flags) == nullptr || !value_as_list(ignored_flags)->items.empty() ||
        !object_get_attr(context, "flags", flags, ignored) ||
        !object_get_attr(context, "traps", traps, ignored)) return false;
    Value rounded_signal;
    Value inexact_signal;
    if (!module_get_attr(state.fallback_module, "Rounded", rounded_signal, ignored) ||
        !module_get_attr(state.fallback_module, "Inexact", inexact_signal, ignored)) return false;
    Value rounded_trap;
    if (!mapping_get_item(traps, rounded_signal, rounded_trap, ignored) || value_truthy(rounded_trap)) return false;
    Value inexact_trap;
    if (inexact && (!mapping_get_item(traps, inexact_signal, inexact_trap, ignored) ||
        value_truthy(inexact_trap))) return false;
    if (inexact && !mapping_set_item(flags, inexact_signal, Value::int64(1), ignored)) return false;
    if (!mapping_set_item(flags, rounded_signal, Value::int64(1), ignored)) return false;
  }
  return assign_decimal_result(state, negative,
      std::move(result_digits), target_exp, out);
}

bool decimal_quantize(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* data) {
  auto* state = static_cast<DecimalOperationState*>(data);
  Value operands[2];
  const Value* quantize_args = args;
  uint32_t quantize_argc = argc;
  const Value* context = nullptr;
  if (state != nullptr && state->context_quantize) {
    if (argc != 3) return runtime_call_callable(runtime, state->original, args, argc, out, error);
    operands[0] = args[1];
    operands[1] = args[2];
    quantize_args = operands;
    quantize_argc = 2;
    context = &args[0];
  }
  if (state != nullptr && decimal_quantize_fast(
          runtime, quantize_args, quantize_argc, nullptr, context, *state, out)) {
    if (state->profile) g_decimal_quantize_fast.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  if (state != nullptr && state->profile) g_decimal_quantize_fallback.fetch_add(1, std::memory_order_relaxed);
  return state != nullptr && runtime_call_callable(runtime, state->original, args, argc, out, error);
}

bool decimal_quantize_keywords(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* data) {
  auto* state = static_cast<DecimalOperationState*>(data);
  if (state != nullptr && state->context_quantize) {
    std::vector<std::pair<std::string, Value>> keyword_values;
    keyword_values.reserve(kwargc);
    for (uint32_t i = 0; i < kwargc; ++i) {
      if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) return false;
      keyword_values.emplace_back(kwargs[i].name, *kwargs[i].value);
    }
    if (state->profile) g_decimal_quantize_fallback.fetch_add(1, std::memory_order_relaxed);
    return runtime_call_callable_kw(runtime, state->original, args, argc, keyword_values, out, error);
  }
  const Value* rounding = nullptr;
  const Value* context = nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) return false;
    const std::string_view name(kwargs[i].name);
    if (name == "rounding" && rounding == nullptr) rounding = kwargs[i].value;
    else if (name == "context" && context == nullptr) context = kwargs[i].value;
    else {
      error = "unexpected or duplicate keyword argument '" + std::string(name) + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (context != nullptr && argc >= 4) {
    error = "got multiple values for keyword argument 'context'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (rounding != nullptr && argc >= 3) {
    error = "got multiple values for keyword argument 'rounding'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (state != nullptr && decimal_quantize_fast(
          runtime, args, argc, rounding, context, *state, out)) {
    if (state->profile) g_decimal_quantize_fast.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  if (state != nullptr && state->profile) g_decimal_quantize_fallback.fetch_add(1, std::memory_order_relaxed);
  if (state == nullptr) return false;
  if (kwargc == 0) return runtime_call_callable(runtime, state->original, args, argc, out, error);
  Value fallback_args[4] = {Value::invalid(), Value::invalid(), Value::none(), Value::none()};
  for (uint32_t i = 0; i < argc && i < 4; ++i) fallback_args[i] = args[i];
  if (rounding != nullptr) fallback_args[2] = *rounding;
  if (context != nullptr) fallback_args[3] = *context;
  const uint32_t fallback_argc = std::min<uint32_t>(4, std::max<uint32_t>(argc, context != nullptr ? 4 : (rounding != nullptr ? 3 : argc)));
  return runtime_call_callable(runtime, state->original, fallback_args, fallback_argc, out, error);
}

bool install_decimal_operation(
    Runtime& runtime,
    Value owner_class,
    Value decimal_class,
    const Value& getcontext,
    const Value& current_context_getter,
    const Value& fallback_module,
    const char* method,
    bool multiply,
    bool context_quantize,
    std::string& error) {
  Value original;
  if (!object_get_attr(owner_class, method, original, error) ||
      value_as_function(original) == nullptr) {
    return false;
  }
  auto* state = new DecimalOperationState();
  state->decimal_class = decimal_class;
  state->original = original;
  state->getcontext = getcontext;
  state->current_context_getter = current_context_getter;
  state->current_context_key = contextvar_exact_getter_key(current_context_getter);
  state->fallback_module = fallback_module;
  state->multiply = multiply;
  state->quantize = std::string_view(method) == "quantize";
  state->context_quantize = context_quantize;
  state->profile = std::getenv("XLANG3_DECIMAL_PROFILE") != nullptr;
  auto* klass = value_as_class(decimal_class);
  if (klass == nullptr ||
      !decimal_class_slot(*klass, "_exp", state->exp_slot) ||
      !decimal_class_slot(*klass, "_int", state->int_slot) ||
      !decimal_class_slot(*klass, "_sign", state->sign_slot) ||
      !decimal_class_slot(*klass, "_is_special", state->special_slot)) {
    delete state;
    error = "Decimal native fast path could not resolve the base Decimal slots";
    return false;
  }
  const NativeFunctionCallback callback = state->quantize ? decimal_quantize : decimal_binary;
  // Match CPython's vectorcall shape at this native-module boundary: hot
  // Decimal operations consume VM arguments directly through the stack-backed
  // adapter, while uncommon values still reach the exact Python fallback in
  // the callback without per-call heap argument materialization.
  const NativeFastCallCallback fast_callback = state->quantize
      ? builtin_fast_adapter<decimal_quantize, 4>
      : builtin_fast_adapter<decimal_binary, 3>;
  const NativeKeywordFunctionCallback keyword_callback = state->quantize
      ? decimal_quantize_keywords : decimal_binary_keywords;
  Value accelerated = runtime.make_native_function(
      std::string("_decimal.Decimal.") + method,
      callback,
      state,
      decimal_operation_state_cleanup,
      fast_callback,
      false,
      keyword_callback,
      true);
  return object_set_attr(owner_class, method, accelerated, error);
}

bool install_decimal_string_operation(
    Runtime& runtime,
    Value decimal_class,
    const Value& getcontext,
    const Value& current_context_getter,
    std::string& error) {
  Value original;
  if (!object_get_attr(decimal_class, "__str__", original, error) ||
      value_as_function(original) == nullptr) {
    return false;
  }
  auto* state = new DecimalOperationState();
  state->decimal_class = decimal_class;
  state->original = original;
  state->getcontext = getcontext;
  state->current_context_getter = current_context_getter;
  state->current_context_key = contextvar_exact_getter_key(current_context_getter);
  auto* klass = value_as_class(decimal_class);
  if (klass == nullptr ||
      !decimal_class_slot(*klass, "_exp", state->exp_slot) ||
      !decimal_class_slot(*klass, "_int", state->int_slot) ||
      !decimal_class_slot(*klass, "_sign", state->sign_slot) ||
      !decimal_class_slot(*klass, "_is_special", state->special_slot)) {
    delete state;
    error = "Decimal native string path could not resolve the base Decimal slots";
    return false;
  }
  // CPython's native _decimal uses dec_str/mpd_to_sci for str(Decimal).
  // Keep _pydecimal as the behavioral fallback, while formatting ordinary
  // exact finite values directly from their immutable coefficient and slots.
  Value accelerated = runtime.make_native_function(
      "_decimal.Decimal.__str__",
      decimal_string,
      state,
      decimal_operation_state_cleanup,
      builtin_fast_adapter<decimal_string, 1>,
      false,
      decimal_string_keywords,
      true);
  return object_set_attr(decimal_class, "__str__", accelerated, error);
}

bool initialize_decimal_fallback(
    Runtime& runtime,
    DecimalModuleState& state,
    std::string& error) {
  if (state.initialized) return true;
  if (!runtime.import_module("_pydecimal", state.fallback_module, error)) return false;
  Value decimal_class;
  Value getcontext;
  Value current_context_var;
  Value current_context_getter;
  Value context_class;
  if (!module_get_attr(state.fallback_module, "Decimal", decimal_class, error) ||
      !module_get_attr(state.fallback_module, "getcontext", getcontext, error) ||
      !module_get_attr(state.fallback_module, "Context", context_class, error) ||
      !module_get_attr(state.fallback_module, "_current_context_var", current_context_var, error) ||
      !attribute_get(current_context_var, "get", current_context_getter, error) ||
      value_as_class(decimal_class) == nullptr) {
    return false;
  }
  // Keep the pure-Python type and its full fallback contract, while routing
  // exact, normal-context add/multiply through the XLang3 runtime's integer
  // core. Out-of-range, rounded, subclass, special, and custom-context cases
  // retain the original method. This matches CPython's native _decimal
  // boundary without embedding or depending on CPython's C API.
  if (!install_decimal_operation(runtime, decimal_class, decimal_class, getcontext,
          current_context_getter, state.fallback_module, "__mul__", true, false, error) ||
      !install_decimal_operation(runtime, decimal_class, decimal_class, getcontext,
          current_context_getter, state.fallback_module, "__rmul__", true, false, error) ||
      !install_decimal_operation(runtime, decimal_class, decimal_class, getcontext,
          current_context_getter, state.fallback_module, "__add__", false, false, error) ||
      !install_decimal_operation(runtime, decimal_class, decimal_class, getcontext,
          current_context_getter, state.fallback_module, "__radd__", false, false, error) ||
      !install_decimal_operation(runtime, decimal_class, decimal_class, getcontext,
          current_context_getter, state.fallback_module, "quantize", false, false, error) ||
      !install_decimal_operation(runtime, context_class, decimal_class, getcontext,
          current_context_getter, state.fallback_module, "quantize", false, true, error) ||
      !install_decimal_string_operation(
          runtime, decimal_class, getcontext, current_context_getter, error)) {
    return false;
  }
  state.initialized = true;
  return true;
}

bool decimal_module_getattr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* data) {
  if (argc != 2 || value_as_string(args[1]) == nullptr) {
    error = "_decimal module attribute name must be a string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = static_cast<DecimalModuleState*>(data);
  if (state == nullptr || !initialize_decimal_fallback(runtime, *state, error)) return false;
  const std::string name = string_object_to_string(*value_as_string(args[1]));
  if (!module_get_attr(state->fallback_module, name, out, error)) return false;
  return true;
}

} // namespace

bool decimal_try_fast_binary(
    Runtime& runtime,
    const Value& receiver,
    const Value& rhs,
    const char* method_name,
    Value& out) {
  if (method_name == nullptr) return false;
  const std::string_view method(method_name);
  const bool multiply = method == "__mul__" || method == "__rmul__";
  if (!multiply && method != "__add__" && method != "__radd__") return false;
  auto* instance = value_as_instance(receiver);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  // Reject unrelated heap types before hashing a name or probing a class map;
  // only the stdlib Decimal base class installs this accelerator.
  if (klass == nullptr || klass->name != "Decimal") return false;
  // Inspect the exact class dictionary before bypassing descriptor binding.
  // This keeps inherited/subclass overrides and monkey-patched methods on the
  // ordinary Python path while avoiding a temporary BoundMethod per Decimal
  // arithmetic opcode in workloads such as pyperformance's telco loop.
  const auto found = klass->attrs.find(method_name);
  if (found == klass->attrs.end()) return false;
  auto* native = value_as_native_function(found->second);
  if (native == nullptr || native->callback != decimal_binary || native->user_data == nullptr) return false;
  auto* state = static_cast<DecimalOperationState*>(native->user_data);
  if (state->quantize || state->multiply != multiply ||
      state->decimal_class.tag != ValueTag::Object ||
      state->decimal_class.as.obj != reinterpret_cast<Object*>(klass)) return false;
  const Value args[2] = {receiver, rhs};
  if (!decimal_binary_fast(runtime, args, 2, nullptr, *state, out)) return false;
  if (state->profile) g_decimal_binary_fast.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void register_decimal_module(Runtime& runtime) {
  Value module = Value::module("_decimal");
  auto* module_object = value_as_module(module);
  module_object->runtime = &runtime;
  auto* state = new DecimalModuleState();
  state->runtime = &runtime;
  Value getattr = runtime.make_native_function(
      "_decimal.__getattr__", decimal_module_getattr, state,
      decimal_module_state_cleanup, nullptr, false, nullptr, true);
  Value module_class = Value::class_object(
      "_XLangDecimalNativeModule", {{"__getattr__", std::move(getattr)}});
  value_assign_fast(module_object->klass, module_class);
  runtime.register_module("_decimal", std::move(module));
}

} // namespace xlang3
