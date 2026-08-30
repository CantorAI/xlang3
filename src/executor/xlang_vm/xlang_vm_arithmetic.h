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
#pragma once

#include "xlang3/ir.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

#include <cmath>
#include <string>

namespace xlang3 {

XLANG3_HOT_INLINE const char* xlang_vm_compare_name(ir::CompareOp op) {
  switch (op) {
    case ir::CompareOp::Eq: return "==";
    case ir::CompareOp::Ne: return "!=";
    case ir::CompareOp::Lt: return "<";
    case ir::CompareOp::Le: return "<=";
    case ir::CompareOp::Gt: return ">";
    case ir::CompareOp::Ge: return ">=";
  }
  return "?";
}

XLANG3_HOT_INLINE bool xlang_vm_value_is_number(const Value& value) {
  return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double;
}

XLANG3_HOT_INLINE double xlang_vm_value_to_double_fast(const Value& value) {
  return value.tag == ValueTag::Int64 ? static_cast<double>(value.as.i64) : value.as.f64;
}

XLANG3_HOT_INLINE bool xlang_vm_function_module(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    const ir::Module*& out) {
  out = fn_obj.module != nullptr ? fn_obj.module.get() : &current_module;
  return fn_obj.function_id < out->functions.size();
}

XLANG3_HOT_INLINE bool xlang_vm_const_bool_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    bool& out) {
  const ir::Module* fn_module = nullptr;
  if (!xlang_vm_function_module(current_module, fn_obj, fn_module)) {
    return false;
  }
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.params.size() != 1 || function.free_vars.size() != 0 || function.cell_slots.size() != 0 ||
      function.code.size() < 2) {
    return false;
  }
  const auto& load_const = function.code[0];
  const auto& ret = function.code[1];
  if (load_const.op != ir::Op::LoadConst || load_const.a >= function.constants.size() ||
      ret.op != ir::Op::Return || ret.a != load_const.dst) {
    return false;
  }
  const auto& value = function.constants[load_const.a];
  if (value.tag != ValueTag::Bool) {
    return false;
  }
  out = value.as.b;
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_truthy(const ir::Module& current_module, const Value& value) {
  Value bool_method;
  std::string ignored;
  if (object_get_class_attr_for_instance(value, "__bool__", bool_method, ignored)) {
    if (auto* function = value_as_function(bool_method)) {
      bool out = true;
      if (xlang_vm_const_bool_method(current_module, *function, out)) {
        return out;
      }
    }
  }
  return value_truthy(value);
}

XLANG3_HOT_INLINE bool xlang_vm_fast_add(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 + rhs.as.i64);
    return true;
  }
  if (xlang_vm_value_is_number(lhs) && xlang_vm_value_is_number(rhs)) {
    value_set_number(out, xlang_vm_value_to_double_fast(lhs) + xlang_vm_value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_sub(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 - rhs.as.i64);
    return true;
  }
  if (xlang_vm_value_is_number(lhs) && xlang_vm_value_is_number(rhs)) {
    value_set_number(out, xlang_vm_value_to_double_fast(lhs) - xlang_vm_value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_mul(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 * rhs.as.i64);
    return true;
  }
  if (xlang_vm_value_is_number(lhs) && xlang_vm_value_is_number(rhs)) {
    value_set_number(out, xlang_vm_value_to_double_fast(lhs) * xlang_vm_value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_div(const Value& lhs, const Value& rhs, Value& out, bool& divide_by_zero) {
  divide_by_zero = false;
  if (!xlang_vm_value_is_number(lhs) || !xlang_vm_value_is_number(rhs)) {
    return false;
  }
  const double divisor = xlang_vm_value_to_double_fast(rhs);
  if (divisor == 0.0) {
    divide_by_zero = true;
    return false;
  }
  value_set_number(out, xlang_vm_value_to_double_fast(lhs) / divisor);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_floor_div(const Value& lhs, const Value& rhs, Value& out, bool& divide_by_zero) {
  divide_by_zero = false;
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    if (rhs.as.i64 == 0) {
      divide_by_zero = true;
      return false;
    }
    int64_t q = lhs.as.i64 / rhs.as.i64;
    const int64_t r = lhs.as.i64 % rhs.as.i64;
    if (r != 0 && ((r < 0) != (rhs.as.i64 < 0))) {
      --q;
    }
    value_set_int64(out, q);
    return true;
  }
  if (!xlang_vm_value_is_number(lhs) || !xlang_vm_value_is_number(rhs)) {
    return false;
  }
  const double divisor = xlang_vm_value_to_double_fast(rhs);
  if (divisor == 0.0) {
    divide_by_zero = true;
    return false;
  }
  value_set_number(out, std::floor(xlang_vm_value_to_double_fast(lhs) / divisor));
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_mod(const Value& lhs, const Value& rhs, Value& out, bool& modulo_by_zero) {
  modulo_by_zero = false;
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    return false;
  }
  if (rhs.as.i64 == 0) {
    modulo_by_zero = true;
    return false;
  }
  int64_t result = lhs.as.i64 % rhs.as.i64;
  if (result != 0 && ((result < 0) != (rhs.as.i64 < 0))) {
    result += rhs.as.i64;
  }
  value_set_int64(out, result);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_pow(const Value& lhs, const Value& rhs, Value& out) {
  if (!xlang_vm_value_is_number(lhs) || !xlang_vm_value_is_number(rhs)) {
    return false;
  }
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64 && rhs.as.i64 >= 0) {
    int64_t result = 1;
    int64_t base = lhs.as.i64;
    uint64_t exponent = static_cast<uint64_t>(rhs.as.i64);
    while (exponent != 0) {
      if ((exponent & 1u) != 0) result *= base;
      exponent >>= 1u;
      if (exponent != 0) base *= base;
    }
    value_set_int64(out, result);
    return true;
  }
  value_set_number(out, std::pow(xlang_vm_value_to_double_fast(lhs), xlang_vm_value_to_double_fast(rhs)));
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_bit_and(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) return false;
  value_set_int64(out, lhs.as.i64 & rhs.as.i64);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_bit_or(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) return false;
  value_set_int64(out, lhs.as.i64 | rhs.as.i64);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_bit_xor(const Value& lhs, const Value& rhs, Value& out) {
  const bool lhs_int = lhs.tag == ValueTag::Int64 || lhs.tag == ValueTag::Bool;
  const bool rhs_int = rhs.tag == ValueTag::Int64 || rhs.tag == ValueTag::Bool;
  if (!lhs_int || !rhs_int) return false;
  const int64_t lhs_value = lhs.tag == ValueTag::Bool ? (lhs.as.b ? 1 : 0) : lhs.as.i64;
  const int64_t rhs_value = rhs.tag == ValueTag::Bool ? (rhs.as.b ? 1 : 0) : rhs.as.i64;
  if (lhs.tag == ValueTag::Bool && rhs.tag == ValueTag::Bool) {
    value_set_bool(out, (lhs_value ^ rhs_value) != 0);
  } else {
    value_set_int64(out, lhs_value ^ rhs_value);
  }
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_shift_left(const Value& lhs, const Value& rhs, Value& out, bool& bad_shift) {
  bad_shift = false;
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) return false;
  if (rhs.as.i64 < 0) {
    bad_shift = true;
    return false;
  }
  if (rhs.as.i64 >= 63) return false;
  value_set_int64(out, lhs.as.i64 << rhs.as.i64);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_shift_right(const Value& lhs, const Value& rhs, Value& out, bool& negative_shift) {
  negative_shift = false;
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) return false;
  if (rhs.as.i64 < 0) {
    negative_shift = true;
    return false;
  }
  if (rhs.as.i64 >= 63) {
    value_set_int64(out, lhs.as.i64 < 0 ? -1 : 0);
    return true;
  }
  value_set_int64(out, lhs.as.i64 >> rhs.as.i64);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_fast_compare(ir::CompareOp op, const Value& lhs, const Value& rhs, Value& out) {
  if (!xlang_vm_value_is_number(lhs) || !xlang_vm_value_is_number(rhs)) {
    return false;
  }
  const double a = xlang_vm_value_to_double_fast(lhs);
  const double b = xlang_vm_value_to_double_fast(rhs);
  bool compare_result = false;
  switch (op) {
    case ir::CompareOp::Eq: compare_result = a == b; break;
    case ir::CompareOp::Ne: compare_result = a != b; break;
    case ir::CompareOp::Lt: compare_result = a < b; break;
    case ir::CompareOp::Le: compare_result = a <= b; break;
    case ir::CompareOp::Gt: compare_result = a > b; break;
    case ir::CompareOp::Ge: compare_result = a >= b; break;
  }
  value_set_bool(out, compare_result);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_is_inline_binary_op(ir::Op op) {
  return op == ir::Op::Add || op == ir::Op::Sub || op == ir::Op::Mul || op == ir::Op::Div ||
         op == ir::Op::FloorDiv || op == ir::Op::Mod || op == ir::Op::Pow ||
         op == ir::Op::BitAnd || op == ir::Op::BitOr || op == ir::Op::BitXor ||
         op == ir::Op::Shl || op == ir::Op::Shr;
}

XLANG3_HOT_INLINE bool xlang_vm_execute_binary_op(
    ir::Op op,
    const Value& lhs,
    const Value& rhs,
    Value& out,
    std::string& error) {
  switch (op) {
    case ir::Op::Add:
      if (xlang_vm_fast_add(lhs, rhs, out)) return true;
      return value_add(lhs, rhs, out, error);
    case ir::Op::Sub:
      if (xlang_vm_fast_sub(lhs, rhs, out)) return true;
      return value_sub(lhs, rhs, out, error);
    case ir::Op::Mul:
      if (xlang_vm_fast_mul(lhs, rhs, out)) return true;
      return value_mul(lhs, rhs, out, error);
    case ir::Op::Div: {
      bool divide_by_zero = false;
      if (xlang_vm_fast_div(lhs, rhs, out, divide_by_zero)) return true;
      if (divide_by_zero) {
        error = "division by zero";
        return false;
      }
      return value_div(lhs, rhs, out, error);
    }
    case ir::Op::FloorDiv: {
      bool divide_by_zero = false;
      if (xlang_vm_fast_floor_div(lhs, rhs, out, divide_by_zero)) return true;
      if (divide_by_zero) {
        error = "division by zero";
        return false;
      }
      return value_floor_div(lhs, rhs, out, error);
    }
    case ir::Op::Mod: {
      bool modulo_by_zero = false;
      if (xlang_vm_fast_mod(lhs, rhs, out, modulo_by_zero)) return true;
      if (modulo_by_zero) {
        error = "integer modulo by zero";
        return false;
      }
      return value_mod(lhs, rhs, out, error);
    }
    case ir::Op::Pow:
      if (xlang_vm_fast_pow(lhs, rhs, out)) return true;
      return value_pow(lhs, rhs, out, error);
    case ir::Op::BitAnd:
      if (xlang_vm_fast_bit_and(lhs, rhs, out)) return true;
      return value_bit_and(lhs, rhs, out, error);
    case ir::Op::BitOr:
      if (xlang_vm_fast_bit_or(lhs, rhs, out)) return true;
      return value_bit_or(lhs, rhs, out, error);
    case ir::Op::BitXor:
      if (xlang_vm_fast_bit_xor(lhs, rhs, out)) return true;
      return value_bit_xor(lhs, rhs, out, error);
    case ir::Op::Shl: {
      bool bad_shift = false;
      if (xlang_vm_fast_shift_left(lhs, rhs, out, bad_shift)) return true;
      if (bad_shift) {
        error = rhs.tag == ValueTag::Int64 && rhs.as.i64 < 0 ? "negative shift count" : "shift count too large for int64";
        return false;
      }
      return value_shift_left(lhs, rhs, out, error);
    }
    case ir::Op::Shr: {
      bool negative_shift = false;
      if (xlang_vm_fast_shift_right(lhs, rhs, out, negative_shift)) return true;
      if (negative_shift) {
        error = "negative shift count";
        return false;
      }
      return value_shift_right(lhs, rhs, out, error);
    }
    default:
      error = "unsupported inline function operation";
      return false;
  }
}

XLANG3_HOT_INLINE const char* compare_name(ir::CompareOp op) {
  return xlang_vm_compare_name(op);
}

XLANG3_HOT_INLINE bool fast_add(const Value& lhs, const Value& rhs, Value& out) {
  return xlang_vm_fast_add(lhs, rhs, out);
}

XLANG3_HOT_INLINE bool fast_sub(const Value& lhs, const Value& rhs, Value& out) {
  return xlang_vm_fast_sub(lhs, rhs, out);
}

XLANG3_HOT_INLINE bool fast_mul(const Value& lhs, const Value& rhs, Value& out) {
  return xlang_vm_fast_mul(lhs, rhs, out);
}

XLANG3_HOT_INLINE bool fast_div(const Value& lhs, const Value& rhs, Value& out, bool& divide_by_zero) {
  return xlang_vm_fast_div(lhs, rhs, out, divide_by_zero);
}

XLANG3_HOT_INLINE bool fast_floor_div(const Value& lhs, const Value& rhs, Value& out, bool& divide_by_zero) {
  return xlang_vm_fast_floor_div(lhs, rhs, out, divide_by_zero);
}

XLANG3_HOT_INLINE bool fast_mod(const Value& lhs, const Value& rhs, Value& out, bool& modulo_by_zero) {
  return xlang_vm_fast_mod(lhs, rhs, out, modulo_by_zero);
}

XLANG3_HOT_INLINE bool fast_pow(const Value& lhs, const Value& rhs, Value& out) {
  return xlang_vm_fast_pow(lhs, rhs, out);
}

XLANG3_HOT_INLINE bool fast_bit_and(const Value& lhs, const Value& rhs, Value& out) {
  return xlang_vm_fast_bit_and(lhs, rhs, out);
}

XLANG3_HOT_INLINE bool fast_bit_or(const Value& lhs, const Value& rhs, Value& out) {
  return xlang_vm_fast_bit_or(lhs, rhs, out);
}

XLANG3_HOT_INLINE bool fast_bit_xor(const Value& lhs, const Value& rhs, Value& out) {
  return xlang_vm_fast_bit_xor(lhs, rhs, out);
}

XLANG3_HOT_INLINE bool fast_shift_left(const Value& lhs, const Value& rhs, Value& out, bool& bad_shift) {
  return xlang_vm_fast_shift_left(lhs, rhs, out, bad_shift);
}

XLANG3_HOT_INLINE bool fast_shift_right(const Value& lhs, const Value& rhs, Value& out, bool& negative_shift) {
  return xlang_vm_fast_shift_right(lhs, rhs, out, negative_shift);
}

XLANG3_HOT_INLINE bool fast_compare(ir::CompareOp op, const Value& lhs, const Value& rhs, Value& out) {
  return xlang_vm_fast_compare(op, lhs, rhs, out);
}

XLANG3_HOT_INLINE bool is_inline_binary_op(ir::Op op) {
  return xlang_vm_is_inline_binary_op(op);
}

XLANG3_HOT_INLINE bool execute_binary_op(
    ir::Op op,
    const Value& lhs,
    const Value& rhs,
    Value& out,
    std::string& error) {
  return xlang_vm_execute_binary_op(op, lhs, rhs, out, error);
}

} // namespace xlang3
