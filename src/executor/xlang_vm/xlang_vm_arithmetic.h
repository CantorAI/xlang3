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
#include "xlang3/runtime.h"

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
  return op == ir::Op::Add || op == ir::Op::Sub || op == ir::Op::Mul || op == ir::Op::Div || op == ir::Op::Mod;
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
    case ir::Op::Mod: {
      bool modulo_by_zero = false;
      if (xlang_vm_fast_mod(lhs, rhs, out, modulo_by_zero)) return true;
      if (modulo_by_zero) {
        error = "integer modulo by zero";
        return false;
      }
      return value_mod(lhs, rhs, out, error);
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

XLANG3_HOT_INLINE bool fast_mod(const Value& lhs, const Value& rhs, Value& out, bool& modulo_by_zero) {
  return xlang_vm_fast_mod(lhs, rhs, out, modulo_by_zero);
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
