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

#include "../xlang_vm_arithmetic.h"
#include "../xlang_vm_op_switch.h"

#include "xlang_vm_ops_call.h"

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

#include <string>

namespace xlang3::xlang_vm::ops {

template <typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow raise_zero_division(
    Runtime& runtime,
    const char* message,
    RaiseExceptionValue&& raise_exception_value) {
  return raise_exception_value(runtime.make_exception("ZeroDivisionError", message))
      ? XlangVMOpFlow::ContinueLoop
      : XlangVMOpFlow::ReturnResult;
}

template <typename FastOp, typename SlowOp, typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow binary_arithmetic(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    FastOp&& fast_op,
    SlowOp&& slow_op,
    RaiseRuntimeError&& raise_runtime_error) {
  const auto& lhs = regs[in.a];
  const auto& rhs = regs[in.b];
  if (!fast_op(lhs, rhs, regs[in.dst])) {
    std::string error;
    if (!slow_op(lhs, rhs, regs[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow add(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs, RaiseRuntimeError&& raise_runtime_error) {
  return binary_arithmetic(in, regs, fast_add, value_add, std::forward<RaiseRuntimeError>(raise_runtime_error));
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow sub(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs, RaiseRuntimeError&& raise_runtime_error) {
  return binary_arithmetic(in, regs, fast_sub, value_sub, std::forward<RaiseRuntimeError>(raise_runtime_error));
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow mul(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs, RaiseRuntimeError&& raise_runtime_error) {
  return binary_arithmetic(in, regs, fast_mul, value_mul, std::forward<RaiseRuntimeError>(raise_runtime_error));
}

template <
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_binary_special_method(
    Runtime& runtime,
    const Value& lhs,
    const Value& rhs,
    const char* method_name,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    uint32_t return_dst,
    size_t& ip,
    std::vector<Value>& native_call_args,
    XlangRuntimeExecutionGuard& execution_lock,
    RuntimeResult& result,
    Value& out,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  Value method;
  std::string attr_error;
  if (!attribute_get(lhs, method_name, method, attr_error)) {
    return XlangVMOpFlow::Next;
  }
  auto* bound = value_as_bound_method(method);
  if (bound == nullptr) {
    return XlangVMOpFlow::Next;
  }
  Value leading[2] = {bound->self, rhs};
  CallArgsView args;
  args.leading = leading;
  args.leading_count = 2;
  bool pushed_frame = false;
  if (!call_callable_value(
          runtime,
          bound->function,
          args,
          module,
          module_owner,
          return_dst,
          ip,
          native_call_args,
          execution_lock,
          out,
          pushed_frame,
          std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
          std::forward<PushFrame>(push_frame),
          std::forward<RaiseRuntimeError>(raise_runtime_error),
          std::forward<RaiseExceptionValue>(raise_exception_value))) {
    if (!result.errors.empty()) {
      return XlangVMOpFlow::ReturnResult;
    }
    return XlangVMOpFlow::ContinueLoop;
  }
  return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::ContinueLoop;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow div(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  const auto& lhs = regs[in.a];
  const auto& rhs = regs[in.b];
  bool divide_by_zero = false;
  if (!fast_div(lhs, rhs, regs[in.dst], divide_by_zero)) {
    if (divide_by_zero) {
      return raise_zero_division(runtime, "division by zero", std::forward<RaiseExceptionValue>(raise_exception_value));
    }
    if (lhs.tag == ValueTag::Object && lhs.as.obj != nullptr) {
      const auto flow = call_binary_special_method(
          runtime,
          lhs,
          rhs,
          "__truediv__",
          module,
          module_owner,
          in.dst,
          ip,
          native_call_args,
          execution_lock,
          result,
          regs[in.dst],
          std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
          std::forward<PushFrame>(push_frame),
          std::forward<RaiseRuntimeError>(raise_runtime_error),
          std::forward<RaiseExceptionValue>(raise_exception_value));
      if (flow != XlangVMOpFlow::Next) {
        return flow;
      }
    }
    std::string error;
    if (!value_div(lhs, rhs, regs[in.dst], error)) {
      if (error == "division by zero") {
        return raise_zero_division(runtime, error.c_str(), std::forward<RaiseExceptionValue>(raise_exception_value));
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow floor_div(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  const auto& lhs = regs[in.a];
  const auto& rhs = regs[in.b];
  bool divide_by_zero = false;
  if (!fast_floor_div(lhs, rhs, regs[in.dst], divide_by_zero)) {
    if (divide_by_zero) {
      return raise_zero_division(runtime, "division by zero", std::forward<RaiseExceptionValue>(raise_exception_value));
    }
    std::string error;
    if (!value_floor_div(lhs, rhs, regs[in.dst], error)) {
      if (error == "integer division by zero" || error == "float floor division by zero" || error == "division by zero") {
        return raise_zero_division(runtime, error.c_str(), std::forward<RaiseExceptionValue>(raise_exception_value));
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow mod(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  const auto& lhs = regs[in.a];
  const auto& rhs = regs[in.b];
  bool modulo_by_zero = false;
  if (!fast_mod(lhs, rhs, regs[in.dst], modulo_by_zero)) {
    if (modulo_by_zero) {
      return raise_zero_division(runtime, "integer modulo by zero", std::forward<RaiseExceptionValue>(raise_exception_value));
    }
    std::string error;
    if (!value_mod(lhs, rhs, regs[in.dst], error)) {
      if (error == "integer modulo by zero" || error == "float modulo by zero") {
        return raise_zero_division(runtime, error.c_str(), std::forward<RaiseExceptionValue>(raise_exception_value));
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow mod_const(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.b >= fn.constants.size()) {
    result.errors.push_back("invalid modulo constant");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& lhs = regs[in.a];
  const auto& rhs = fn.constants[in.b];
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    if (rhs.as.i64 == 0) {
      return raise_zero_division(runtime, "integer modulo by zero", std::forward<RaiseExceptionValue>(raise_exception_value));
    }
    value_set_int64(regs[in.dst], lhs.as.i64 % rhs.as.i64);
    return XlangVMOpFlow::Next;
  }
  bool modulo_by_zero = false;
  if (!fast_mod(lhs, rhs, regs[in.dst], modulo_by_zero)) {
    if (modulo_by_zero) {
      return raise_zero_division(runtime, "integer modulo by zero", std::forward<RaiseExceptionValue>(raise_exception_value));
    }
    std::string error;
    if (!value_mod(lhs, rhs, regs[in.dst], error)) {
      if (error == "integer modulo by zero" || error == "float modulo by zero") {
        return raise_zero_division(runtime, error.c_str(), std::forward<RaiseExceptionValue>(raise_exception_value));
      }
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow pow(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs, RaiseRuntimeError&& raise_runtime_error) {
  return binary_arithmetic(in, regs, fast_pow, value_pow, std::forward<RaiseRuntimeError>(raise_runtime_error));
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow bit_and(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs, RaiseRuntimeError&& raise_runtime_error) {
  return binary_arithmetic(in, regs, fast_bit_and, value_bit_and, std::forward<RaiseRuntimeError>(raise_runtime_error));
}

template <
    typename FastOp,
    typename SlowOp,
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow binary_arithmetic_with_special_method(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    FastOp&& fast_op,
    SlowOp&& slow_op,
    const char* special_method_name,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  const auto& lhs = regs[in.a];
  const auto& rhs = regs[in.b];
  if (fast_op(lhs, rhs, regs[in.dst])) {
    return XlangVMOpFlow::Next;
  }
  if (lhs.tag == ValueTag::Object && lhs.as.obj != nullptr) {
    const auto flow = call_binary_special_method(
        runtime,
        lhs,
        rhs,
        special_method_name,
        module,
        module_owner,
        in.dst,
        ip,
        native_call_args,
        execution_lock,
        result,
        regs[in.dst],
        std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
        std::forward<PushFrame>(push_frame),
        std::forward<RaiseRuntimeError>(raise_runtime_error),
        std::forward<RaiseExceptionValue>(raise_exception_value));
    if (flow != XlangVMOpFlow::Next) {
      return flow;
    }
  }
  std::string error;
  if (slow_op(lhs, rhs, regs[in.dst], error)) {
    return XlangVMOpFlow::Next;
  }
  return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow bit_and(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  return binary_arithmetic_with_special_method(
      in,
      module,
      module_owner,
      runtime,
      regs,
      native_call_args,
      ip,
      result,
      execution_lock,
      fast_bit_and,
      value_bit_and,
      "__and__",
      std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
      std::forward<PushFrame>(push_frame),
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow bit_or(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  return binary_arithmetic_with_special_method(
      in,
      module,
      module_owner,
      runtime,
      regs,
      native_call_args,
      ip,
      result,
      execution_lock,
      fast_bit_or,
      value_bit_or,
      "__or__",
      std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
      std::forward<PushFrame>(push_frame),
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow bit_xor(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  return binary_arithmetic_with_special_method(
      in,
      module,
      module_owner,
      runtime,
      regs,
      native_call_args,
      ip,
      result,
      execution_lock,
      fast_bit_xor,
      value_bit_xor,
      "__xor__",
      std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
      std::forward<PushFrame>(push_frame),
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow shl(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs, RaiseRuntimeError&& raise_runtime_error) {
  const auto& lhs = regs[in.a];
  const auto& rhs = regs[in.b];
  bool bad_shift = false;
  if (!fast_shift_left(lhs, rhs, regs[in.dst], bad_shift)) {
    if (bad_shift) {
      const char* message = rhs.tag == ValueTag::Int64 && rhs.as.i64 < 0 ? "negative shift count" : "shift count too large for int64";
      return raise_runtime_error(message) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    std::string error;
    if (!value_shift_left(lhs, rhs, regs[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow shr(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs, RaiseRuntimeError&& raise_runtime_error) {
  const auto& lhs = regs[in.a];
  const auto& rhs = regs[in.b];
  bool negative_shift = false;
  if (!fast_shift_right(lhs, rhs, regs[in.dst], negative_shift)) {
    if (negative_shift) {
      return raise_runtime_error("negative shift count") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    std::string error;
    if (!value_shift_right(lhs, rhs, regs[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE void bool_and(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs) {
  value_assign_fast(regs[in.dst], value_truthy(regs[in.a]) ? regs[in.b] : regs[in.a]);
}

XLANG3_HOT_INLINE void bool_or(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs) {
  value_assign_fast(regs[in.dst], value_truthy(regs[in.a]) ? regs[in.a] : regs[in.b]);
}

XLANG3_HOT_INLINE const char* rich_compare_method(ir::CompareOp op) {
  switch (op) {
    case ir::CompareOp::Eq: return "__eq__";
    case ir::CompareOp::Ne: return "__ne__";
    case ir::CompareOp::Lt: return "__lt__";
    case ir::CompareOp::Le: return "__le__";
    case ir::CompareOp::Gt: return "__gt__";
    case ir::CompareOp::Ge: return "__ge__";
  }
  return nullptr;
}

template <typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool try_rich_compare(
    Runtime& runtime,
    ir::CompareOp op,
    const Value& lhs,
    const Value& rhs,
    Value& out,
    XlangVMOpFlow& flow,
    RaiseExceptionValue&& raise_exception_value) {
  flow = XlangVMOpFlow::Next;
  if (value_as_instance(lhs) == nullptr) {
    return false;
  }
  const char* method_name = rich_compare_method(op);
  if (method_name == nullptr) {
    return false;
  }
  Value method;
  std::string ignored;
  if (!object_get_attr(lhs, method_name, method, ignored)) {
    return false;
  }
  std::string error;
  if (runtime_call_callable(runtime, method, &rhs, 1, out, error)) {
    return true;
  }
  Value pending;
  if (runtime.take_pending_exception(pending)) {
    flow = raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    return true;
  }
  flow = raise_exception_value(runtime.make_exception("RuntimeError", error))
             ? XlangVMOpFlow::ContinueLoop
             : XlangVMOpFlow::ReturnResult;
  return true;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow compare(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  const auto& lhs = regs[in.a];
  const auto& rhs = regs[in.b];
  const auto op = static_cast<ir::CompareOp>(in.c);
  XlangVMOpFlow rich_flow = XlangVMOpFlow::Next;
  if (value_as_instance(lhs) != nullptr &&
      try_rich_compare(runtime, op, lhs, rhs, regs[in.dst], rich_flow, raise_exception_value)) {
    return rich_flow;
  }
  if (!fast_compare(op, lhs, rhs, regs[in.dst])) {
    std::string error;
    if (!value_compare(compare_name(op), lhs, rhs, regs[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE void is_op(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs) {
  value_set_bool(regs[in.dst], value_is(regs[in.a], regs[in.b]) != (in.c != 0));
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow contains(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs, RaiseRuntimeError&& raise_runtime_error) {
  bool contains_value = false;
  std::string error;
  if (!value_contains(regs[in.b], regs[in.a], contains_value, error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_set_bool(regs[in.dst], contains_value != (in.c != 0));
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE void not_op(
    const ir::Instr& in,
    const ir::Module& module,
    XlangVMSmallRegisterBuffer& regs) {
  value_set_bool(regs[in.dst], !xlang_vm_truthy(module, regs[in.a]));
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow neg(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (regs[in.a].tag == ValueTag::Int64) {
    if (regs[in.a].as.i64 == std::numeric_limits<int64_t>::min()) {
      std::string error;
      if (!value_int_like_sub(Value::int64(0), regs[in.a], regs[in.dst])) {
        return raise_runtime_error("unsupported operand for unary -") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return XlangVMOpFlow::Next;
    }
    value_set_int64(regs[in.dst], -regs[in.a].as.i64);
  } else if (value_as_bigint(regs[in.a]) != nullptr) {
    if (!value_int_like_sub(Value::int64(0), regs[in.a], regs[in.dst])) {
      return raise_runtime_error("unsupported operand for unary -") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  } else if (regs[in.a].tag == ValueTag::Double) {
    value_set_number(regs[in.dst], -regs[in.a].as.f64);
  } else {
    if (regs[in.a].tag == ValueTag::Object && regs[in.a].as.obj != nullptr) {
      Value method;
      std::string attr_error;
      if (attribute_get(regs[in.a], "__neg__", method, attr_error)) {
        auto* bound = value_as_bound_method(method);
        if (bound != nullptr) {
          Value leading[1] = {bound->self};
          CallArgsView args;
          args.leading = leading;
          args.leading_count = 1;
          bool pushed_frame = false;
          if (!call_callable_value(
                  runtime,
                  bound->function,
                  args,
                  module,
                  module_owner,
                  in.dst,
                  ip,
                  native_call_args,
                  execution_lock,
                  regs[in.dst],
                  pushed_frame,
                  std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
                  std::forward<PushFrame>(push_frame),
                  std::forward<RaiseRuntimeError>(raise_runtime_error),
                  std::forward<RaiseExceptionValue>(raise_exception_value))) {
            if (!result.errors.empty()) {
              return XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::ContinueLoop;
          }
          return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::ContinueLoop;
        }
      }
    }
    return raise_runtime_error("unsupported operand for unary -") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow invert(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (regs[in.a].tag == ValueTag::Object && regs[in.a].as.obj != nullptr) {
    Value method;
    std::string attr_error;
    if (attribute_get(regs[in.a], "__invert__", method, attr_error)) {
      auto* bound = value_as_bound_method(method);
      if (bound != nullptr) {
        Value leading[1] = {bound->self};
        CallArgsView args;
        args.leading = leading;
        args.leading_count = 1;
        bool pushed_frame = false;
        if (!call_callable_value(
                runtime,
                bound->function,
                args,
                module,
                module_owner,
                in.dst,
                ip,
                native_call_args,
                execution_lock,
                regs[in.dst],
                pushed_frame,
                std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
                std::forward<PushFrame>(push_frame),
                std::forward<RaiseRuntimeError>(raise_runtime_error),
                std::forward<RaiseExceptionValue>(raise_exception_value))) {
          if (!result.errors.empty()) {
            return XlangVMOpFlow::ReturnResult;
          }
          return XlangVMOpFlow::ContinueLoop;
        }
        return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::ContinueLoop;
      }
    }
  }
  std::string error;
  if (!value_invert(regs[in.a], regs[in.dst], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
