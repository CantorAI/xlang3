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

#include "../xlang_frame.h"
#include "../xlang_vm_arithmetic.h"
#include "../xlang_vm_inline_call.h"
#include "../xlang_vm_inline_support.h"
#include "../xlang_vm_op_switch.h"
#include "xlang_vm_ops_iteration.h"

#include "xlang3/builtins.h"
#include "xlang3/builtin_methods.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/interpreter_events.h"

#include <string>
#include <utility>
#include <vector>

namespace xlang3::xlang_vm::ops {

template <typename EmitMonitoringEvent, typename RaiseRuntimeError,
          typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow jump(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool collapse_unobservable_chain,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  uint32_t target = in.dst;
  if (collapse_unobservable_chain) {
    size_t remaining = fn.code.size();
    while (target < fn.code.size() && remaining-- != 0) {
      const auto& target_instruction = fn.code[target];
      if (target_instruction.op != ir::Op::Jump ||
          target_instruction.dst == target) {
        break;
      }
      target = target_instruction.dst;
    }
  }
  if (collapse_unobservable_chain && target < fn.code.size()) {
    const auto& target_instruction = fn.code[target];
    XlangVMOpFlow flow = XlangVMOpFlow::ContinueLoop;
    if (target_instruction.op == ir::Op::IterNext) {
      ip = target;
      flow = iter_next(
          target_instruction, runtime, regs, ip,
          std::forward<EmitMonitoringEvent>(emit_monitoring_event),
          std::forward<RaiseRuntimeError>(raise_runtime_error),
          std::forward<RaiseExceptionValue>(raise_exception_value));
    } else if (target_instruction.op == ir::Op::IterNextLocal) {
      ip = target;
      flow = iter_next_local(
          target_instruction, runtime, regs, locals, ip,
          std::forward<EmitMonitoringEvent>(emit_monitoring_event),
          std::forward<RaiseRuntimeError>(raise_runtime_error),
          std::forward<RaiseExceptionValue>(raise_exception_value));
    } else {
      flow = XlangVMOpFlow::ContinueLoop;
    }
    if (target_instruction.op == ir::Op::IterNext ||
        target_instruction.op == ir::Op::IterNextLocal) {
      if (flow == XlangVMOpFlow::Next) ip = target + 1;
      return flow == XlangVMOpFlow::Next ? XlangVMOpFlow::ContinueLoop : flow;
    }
  }
  Value destination = Value::int64(static_cast<int64_t>(target));
  if (!emit_monitoring_event(kSysMonitoringEventJump, &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  ip = target;
  return XlangVMOpFlow::ContinueLoop;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_false(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending))
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.dst;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow not_jump_if_false(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  bool operand = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], operand, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const bool condition = !operand;
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.dst;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow move_jump_if_false(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  value_assign_fast(regs[in.dst], regs[in.a]);
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.b;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.b;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow move_jump_if_true(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  value_assign_fast(regs[in.dst], regs[in.a]);
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? in.b : static_cast<uint32_t>(ip + 1);
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) return XlangVMOpFlow::Next;
  if (!emit_monitoring_event(kSysMonitoringEventJump, &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  ip = in.b;
  return XlangVMOpFlow::ContinueLoop;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError,
          typename RaiseExceptionValue, typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_false_load_local(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    RuntimeResult& result,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.b;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.b;
    return XlangVMOpFlow::ContinueLoop;
  }
  if (in.c >= locals.size()) {
    result.errors.push_back("invalid local slot in conditional load");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.c].tag == ValueTag::Invalid) {
    const std::string name = in.c < fn.locals.size() ? fn.locals[in.c] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], locals[in.c]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue, typename EmitMonitoringEvent>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_local_const_false(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  if (in.a >= locals.size() || in.b >= fn.constants.size()) {
    result.errors.push_back("invalid local const jump");
    return XlangVMOpFlow::ReturnResult;
  }
  Value compare_result;
  const auto op = static_cast<ir::CompareOp>(in.c);
  const auto compare_flow = compare_values(
      op, locals[in.a], fn.constants[in.b], compare_result, runtime,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
  if (compare_flow != XlangVMOpFlow::Next) return compare_flow;
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, compare_result, condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.dst;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue,
          typename EmitMonitoringEvent>
XLANG3_NOINLINE inline XlangVMOpFlow for_scalar_arithmetic_loop(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool allow_guarded_fast_path,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  const size_t expression_ip = ip + 1;
  const ir::Instr generic{ir::Op::JumpIfLocalConstFalse, in.dst, in.a, in.b,
                          static_cast<uint32_t>(ir::CompareOp::Lt)};
  if ((in.c & ir::kScalarArithmeticLoopFlag) == 0 || in.a >= locals.size() ||
      in.b >= fn.constants.size() || expression_ip >= fn.code.size() ||
      fn.code[expression_ip].op != ir::Op::GuardedLocalNumericExpr ||
      fn.code[expression_ip].dst >= locals.size() ||
      fn.code[expression_ip].a >= fn.guarded_local_numeric_exprs.size() ||
      fn.constants[in.b].tag != ValueTag::Int64) {
    result.errors.push_back("invalid fused scalar arithmetic loop");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& expression = fn.code[expression_ip];
  const auto& spec = fn.guarded_local_numeric_exprs[expression.a];
  if (spec.nodes.size() != 7 || spec.fallback_span == 0 ||
      spec.fallback_span >= fn.code.size() - expression_ip) {
    result.errors.push_back("invalid fused scalar arithmetic loop plan");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& n0 = spec.nodes[0];
  const auto& n1 = spec.nodes[1];
  const auto& n2 = spec.nodes[2];
  const auto& n3 = spec.nodes[3];
  const auto& n4 = spec.nodes[4];
  const auto& n5 = spec.nodes[5];
  const auto& n6 = spec.nodes[6];
  if (n0.kind != ir::GuardedLocalNumericExprNodeKind::Local ||
      n0.a != expression.dst || n1.kind != ir::GuardedLocalNumericExprNodeKind::Local ||
      n1.a != in.a || n2.kind != ir::GuardedLocalNumericExprNodeKind::Constant ||
      n3.kind != ir::GuardedLocalNumericExprNodeKind::Mul || n3.a != 1 || n3.b != 2 ||
      n4.kind != ir::GuardedLocalNumericExprNodeKind::Add || n4.a != 0 || n4.b != 3 ||
      n5.kind != ir::GuardedLocalNumericExprNodeKind::Constant ||
      n6.kind != ir::GuardedLocalNumericExprNodeKind::Sub || n6.a != 4 || n6.b != 5 ||
      n2.a >= fn.constants.size() || n5.a >= fn.constants.size() ||
      fn.constants[n2.a].tag != ValueTag::Int64 ||
      fn.constants[n5.a].tag != ValueTag::Int64 ||
      expression.dst == in.a) {
    result.errors.push_back("invalid fused scalar arithmetic loop nodes");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t increment_ip = expression_ip + spec.fallback_span + 1;
  if (increment_ip >= fn.code.size() || fn.code[increment_ip].op != ir::Op::AddLocalConst ||
      fn.code[increment_ip].dst != in.a || fn.code[increment_ip].a != in.a ||
      fn.code[increment_ip].b >= fn.constants.size() ||
      (fn.code[increment_ip].c & ir::kGuardedLocalAddFlag) == 0 ||
      fn.constants[fn.code[increment_ip].b].tag != ValueTag::Int64 ||
      fn.constants[fn.code[increment_ip].b].as.i64 <= 0) {
    result.errors.push_back("invalid fused scalar arithmetic loop increment");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t increment_fallback =
      fn.code[increment_ip].c & ir::kGuardedLocalAddSpanMask;
  const size_t back_edge_ip = increment_ip + increment_fallback + 1;
  if (increment_fallback == 0 || back_edge_ip >= fn.code.size() ||
      fn.code[back_edge_ip].op != ir::Op::Jump ||
      fn.code[back_edge_ip].dst != ip || in.dst != back_edge_ip + 1) {
    result.errors.push_back("invalid fused scalar arithmetic loop back edge");
    return XlangVMOpFlow::ReturnResult;
  }

  const auto hook_active = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };
  const bool observable = !allow_guarded_fast_path || runtime.debug_step_active() ||
      hook_active(frame.trace_function) || hook_active(runtime.trace_function()) ||
      hook_active(runtime.profile_function()) ||
      sys_monitoring_event_may_dispatch(kSysMonitoringEventAll);
  const int64_t bound = fn.constants[in.b].as.i64;
  const int64_t multiplier = fn.constants[n2.a].as.i64;
  const int64_t decrement = fn.constants[n5.a].as.i64;
  const int64_t step = fn.constants[fn.code[increment_ip].b].as.i64;
  const bool eligible = !observable && locals[in.a].tag == ValueTag::Int64 &&
      locals[expression.dst].tag == ValueTag::Int64 && locals[in.a].as.i64 < bound;
  if (eligible) {
    int64_t current = locals[in.a].as.i64;
    size_t completed = 0;
    bool must_return_to_dispatcher = false;
    while (current < bound) {
      if ((completed & 31u) == 0 &&
          (interpreter_pending_events() != 0 ||
           sys_monitoring_event_may_dispatch(kSysMonitoringEventAll))) {
        must_return_to_dispatcher = true;
        break;
      }
      int64_t product = 0;
      int64_t adjusted = 0;
      int64_t new_accumulator = 0;
      int64_t next_counter = 0;
      // All five operations are checked before storing either local. If one
      // overflows, generic bytecode evaluates this same logical iteration and
      // promotes to bigint exactly where normal Python arithmetic would.
      if (!xlang_vm_checked_mul_i64(current, multiplier, product) ||
          !xlang_vm_checked_sub_i64(product, decrement, adjusted) ||
          !xlang_vm_checked_add_i64(locals[expression.dst].as.i64, adjusted,
                                    new_accumulator) ||
          !xlang_vm_checked_add_i64(current, step, next_counter)) {
        break;
      }
      value_set_int64(locals[expression.dst], new_accumulator);
      value_set_int64(locals[in.a], next_counter);
      current = next_counter;
      ++completed;
    }
    if (completed != 0) {
      frame.release_memoryviews_for_skipped_local_add(
          fn, expression_ip + 1, spec.fallback_span);
      frame.release_memoryviews_for_skipped_local_add(
          fn, increment_ip + 1, increment_fallback);
      if (!must_return_to_dispatcher && current >= bound) ip = in.dst;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (must_return_to_dispatcher) return XlangVMOpFlow::ContinueLoop;
  }
  if (allow_guarded_fast_path && locals[in.a].tag == ValueTag::Int64 &&
      locals[in.a].as.i64 >= bound && !observable) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return jump_if_local_const_false(
      generic, fn, runtime, locals, ip, result,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value),
      std::forward<EmitMonitoringEvent>(emit_monitoring_event));
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue,
          typename EmitMonitoringEvent>
XLANG3_NOINLINE inline XlangVMOpFlow for_property_access_loop(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    XlangVMInstrCacheStorage& instr_cache,
    size_t& ip,
    bool allow_guarded_fast_path,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  const uint32_t total_slot = (in.c & ir::kPropertyAccessLoopLocalMask) >>
      ir::kPropertyAccessLoopLocalShift;
  const size_t set_ip = ip + 2;
  const size_t get_ip = ip + 4;
  const size_t mod_ip = ip + 8;
  const size_t branch_ip = ip + 10;
  const size_t delete_ip = ip + 12;
  const size_t second_get_ip = ip + 14;
  const size_t increment_ip = ip + 18;
  const ir::Instr generic{ir::Op::JumpIfLocalConstFalse, in.dst, in.a, in.b,
                          static_cast<uint32_t>(ir::CompareOp::Lt)};
  if ((in.c & ir::kPropertyAccessLoopFlag) == 0 || total_slot >= locals.size() ||
      in.a >= locals.size() || in.b >= fn.constants.size() ||
      fn.constants[in.b].tag != ValueTag::Int64 || increment_ip >= fn.code.size() ||
      fn.code[ip + 1].op != ir::Op::LoadLocalPair ||
      fn.code[ip + 1].a >= locals.size() || fn.code[ip + 1].a == in.a ||
      fn.code[ip + 1].c != in.a || fn.code[set_ip].op != ir::Op::StoreAttr ||
      fn.code[ip + 3].op != ir::Op::LoadLocalPair ||
      fn.code[ip + 3].a != total_slot || fn.code[get_ip].op != ir::Op::LoadAttr ||
      fn.code[ip + 7].op != ir::Op::LoadLocal || fn.code[ip + 7].a != in.a ||
      fn.code[mod_ip].op != ir::Op::ModConst ||
      fn.code[mod_ip].b >= fn.constants.size() ||
      fn.constants[fn.code[mod_ip].b].tag != ValueTag::Int64 ||
      fn.constants[fn.code[mod_ip].b].as.i64 <= 0 ||
      fn.code[branch_ip].op != ir::Op::CompareJumpIfFalse ||
      fn.code[branch_ip].dst != increment_ip || fn.code[delete_ip].op != ir::Op::DeleteAttr ||
      fn.code[delete_ip].a != fn.code[set_ip].a ||
      fn.code[second_get_ip].op != ir::Op::LoadAttr ||
      fn.code[second_get_ip].b != fn.code[get_ip].b ||
      fn.code[increment_ip].op != ir::Op::AddLocalConst ||
      fn.code[increment_ip].dst != in.a || fn.code[increment_ip].a != in.a ||
      fn.code[increment_ip].b >= fn.constants.size() ||
      fn.constants[fn.code[increment_ip].b].tag != ValueTag::Int64) {
    result.errors.push_back("invalid fused property access loop");
    return XlangVMOpFlow::ReturnResult;
  }

  auto* instance = value_as_instance(locals[fn.code[ip + 1].a]);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  const auto hook_active = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };
  const bool observable = !allow_guarded_fast_path || runtime.debug_step_active() ||
      hook_active(frame.trace_function) || hook_active(runtime.trace_function()) ||
      hook_active(runtime.profile_function()) ||
      sys_monitoring_event_may_dispatch(kSysMonitoringEventAll);
  const bool sites_valid = klass != nullptr && set_ip < instr_cache.size() &&
      get_ip < instr_cache.size() && second_get_ip < instr_cache.size();
  AttrSiteCache* setter = sites_valid ? &instr_cache[set_ip].attr : nullptr;
  AttrSiteCache* getter = sites_valid ? &instr_cache[get_ip].attr : nullptr;
  AttrSiteCache* getter2 = sites_valid ? &instr_cache[second_get_ip].attr : nullptr;
  const bool caches_match = setter != nullptr && getter != nullptr && getter2 != nullptr &&
      setter->kind == AttrSiteKind::PropertyInstanceAttr && setter->setter_inline &&
      getter->kind == AttrSiteKind::PropertyInstanceAttr && getter->getter_inline &&
      getter2->kind == AttrSiteKind::PropertyInstanceAttr && getter2->getter_inline &&
      setter->owner == &klass->header && getter->owner == &klass->header &&
      getter2->owner == &klass->header && setter->version == klass->version &&
      getter->version == klass->version && getter2->version == klass->version &&
      setter->property_attr_name != nullptr && getter->property_attr_name != nullptr &&
      getter2->property_attr_name != nullptr &&
      *setter->property_attr_name == *getter->property_attr_name &&
      *getter->property_attr_name == *getter2->property_attr_name &&
      setter->index == getter->index && getter->index == getter2->index &&
      setter->setter_op == ir::Op::Sub && setter->setter_has_const &&
      setter->setter_const.tag == ValueTag::Int64 &&
      getter->getter_op == ir::Op::Add && getter->getter_has_const &&
      getter->getter_const.tag == ValueTag::Int64 && getter2->index == getter->index;
  const bool safe_object = caches_match && instance != nullptr &&
      !klass->has_getattribute_hook && !klass->has_getattr_hook &&
      !klass->has_setattr_hook && !klass->has_delattr_hook &&
      instance->native_get_attr == nullptr && instance->native_set_attr == nullptr &&
      instance->native_delete_attr == nullptr &&
      value_as_dict(instance_attribute_storage(*instance)) == nullptr &&
      setter->index < instance->attrs.size() &&
      instance->attrs[setter->index].first == *setter->property_attr_name &&
      instance->attrs[setter->index].second.tag == ValueTag::Int64 &&
      locals[in.a].tag == ValueTag::Int64 && locals[total_slot].tag == ValueTag::Int64;
  if (observable || !safe_object || locals[in.a].as.i64 >= fn.constants[in.b].as.i64) {
    return jump_if_local_const_false(
        generic, fn, runtime, locals, ip, result,
        std::forward<RaiseRuntimeError>(raise_runtime_error),
        std::forward<RaiseExceptionValue>(raise_exception_value),
        std::forward<EmitMonitoringEvent>(emit_monitoring_event));
  }

  // Keep the exact integer descriptor loop inside the VM once its accessor
  // caches prove the pure setter/getter formulas. At each periodic delete (or
  // event/overflow) resume the untouched bytecode so arbitrary descriptor code
  // and Python's integer promotion semantics remain authoritative.
  const int64_t bound = fn.constants[in.b].as.i64;
  const int64_t step = fn.constants[fn.code[increment_ip].b].as.i64;
  const int64_t divisor = fn.constants[fn.code[mod_ip].b].as.i64;
  const int64_t residue = fn.constants[fn.code[ip + 9].a].as.i64;
  int64_t current = locals[in.a].as.i64;
  size_t completed = 0;
  while (current < bound) {
    if ((completed & 31u) == 0 &&
        (interpreter_pending_events() != 0 ||
         sys_monitoring_event_may_dispatch(kSysMonitoringEventAll))) {
      ip += 1;
      return XlangVMOpFlow::ContinueLoop;
    }
    int64_t remainder = current % divisor;
    if (remainder < 0) remainder += divisor; // Match Python's nonnegative modulo.
    if (remainder == residue) {
      ip += 1;
      return XlangVMOpFlow::ContinueLoop;
    }
    int64_t stored = 0, value = 0, total = 0, next = 0;
    if (!xlang_vm_checked_sub_i64(current, setter->setter_const.as.i64, stored) ||
        !xlang_vm_checked_add_i64(stored, getter->getter_const.as.i64, value) ||
        !xlang_vm_checked_add_i64(locals[total_slot].as.i64, value, total) ||
        !xlang_vm_checked_add_i64(current, step, next)) {
      ip += 1;
      return XlangVMOpFlow::ContinueLoop;
    }
    value_set_int64(instance->attrs[setter->index].second, stored);
    value_set_int64(locals[total_slot], total);
    value_set_int64(locals[in.a], next);
    current = next;
    ++completed;
  }
  const size_t fallback = fn.code[increment_ip].c & ir::kGuardedLocalAddSpanMask;
  frame.release_memoryviews_for_skipped_local_add(fn, increment_ip + 1, fallback);
  ip = in.dst;
  return XlangVMOpFlow::ContinueLoop;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue,
          typename EmitMonitoringEvent>
XLANG3_NOINLINE inline XlangVMOpFlow for_construct_method_accumulate_loop(
    const ir::Instr& in,
    const ir::Module& module,
    const ir::Function& fn,
    XlangVMFrame& frame,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    XlangVMInstrCacheStorage& instr_cache,
    size_t& ip,
    bool allow_guarded_fast_path,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  const uint32_t accumulator_slot =
      (in.c & ir::kConstructMethodAccumulateLoopLocalMask) >>
      ir::kConstructMethodAccumulateLoopLocalShift;
  const size_t class_load_ip = ip + 1;
  const size_t constructor_args_ip = ip + 2;
  const size_t construct_ip = ip + 3;
  const size_t save_instance_ip = ip + 4;
  const size_t receiver_pair_ip = ip + 5;
  const size_t method_ip = ip + 6;
  const size_t sum_ip = ip + 7;
  const size_t save_sum_ip = ip + 8;
  const size_t increment_ip = ip + 9;
  const ir::Instr generic{ir::Op::JumpIfLocalConstFalse, in.dst, in.a, in.b,
                          static_cast<uint32_t>(ir::CompareOp::Lt)};
  if ((in.c & ir::kConstructMethodAccumulateLoopFlag) == 0 ||
      accumulator_slot >= locals.size() || in.a >= locals.size() ||
      in.b >= fn.constants.size() || increment_ip >= fn.code.size() ||
      fn.code[class_load_ip].op != ir::Op::LoadModuleSlot ||
      fn.code[constructor_args_ip].op != ir::Op::LoadLocalConst ||
      fn.code[constructor_args_ip].a != in.a ||
      fn.code[constructor_args_ip].c >= fn.constants.size() ||
      fn.code[construct_ip].op != ir::Op::Call ||
      fn.code[construct_ip].a != fn.code[class_load_ip].dst ||
      fn.code[construct_ip].b >= fn.call_args.size() ||
      fn.call_args[fn.code[construct_ip].b].size() != 2 ||
      fn.call_args[fn.code[construct_ip].b][0] != fn.code[constructor_args_ip].dst ||
      fn.call_args[fn.code[construct_ip].b][1] != fn.code[constructor_args_ip].b ||
      fn.code[save_instance_ip].op != ir::Op::StoreLocal ||
      fn.code[save_instance_ip].a != fn.code[construct_ip].dst ||
      fn.code[receiver_pair_ip].op != ir::Op::LoadLocalPair ||
      fn.code[receiver_pair_ip].a != accumulator_slot ||
      fn.code[receiver_pair_ip].c != fn.code[save_instance_ip].dst ||
      fn.code[method_ip].op != ir::Op::CallMethod ||
      fn.code[method_ip].a != fn.code[receiver_pair_ip].b ||
      fn.code[method_ip].c >= fn.call_args.size() ||
      !fn.call_args[fn.code[method_ip].c].empty() ||
      fn.code[sum_ip].op != ir::Op::Add ||
      (fn.code[sum_ip].a != fn.code[receiver_pair_ip].dst &&
       fn.code[sum_ip].b != fn.code[receiver_pair_ip].dst) ||
      (fn.code[sum_ip].a == fn.code[receiver_pair_ip].dst
           ? fn.code[sum_ip].b : fn.code[sum_ip].a) != fn.code[method_ip].dst ||
      fn.code[save_sum_ip].op != ir::Op::StoreLocal ||
      fn.code[save_sum_ip].dst != accumulator_slot ||
      fn.code[save_sum_ip].a != fn.code[sum_ip].dst ||
      fn.code[increment_ip].op != ir::Op::AddLocalConst ||
      fn.code[increment_ip].dst != in.a || fn.code[increment_ip].a != in.a ||
      fn.code[increment_ip].b >= fn.constants.size() ||
      (fn.code[increment_ip].c & ir::kGuardedLocalAddFlag) == 0 ||
      fn.constants[in.b].tag != ValueTag::Int64 ||
      fn.constants[fn.code[constructor_args_ip].c].tag != ValueTag::Int64 ||
      fn.constants[fn.code[increment_ip].b].tag != ValueTag::Int64 ||
      fn.constants[fn.code[increment_ip].b].as.i64 <= 0 ||
      fn.code[class_load_ip].dst >= regs.size() ||
      fn.code[constructor_args_ip].dst >= regs.size() ||
      fn.code[constructor_args_ip].b >= regs.size() ||
      fn.code[construct_ip].dst >= regs.size() ||
      fn.code[save_instance_ip].dst >= locals.size() ||
      fn.code[receiver_pair_ip].dst >= regs.size() ||
      fn.code[receiver_pair_ip].b >= regs.size() ||
      fn.code[method_ip].dst >= regs.size() || fn.code[sum_ip].dst >= regs.size()) {
    result.errors.push_back("invalid fused construction/method loop");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t fallback_span =
      fn.code[increment_ip].c & ir::kGuardedLocalAddSpanMask;
  const size_t back_edge_ip = increment_ip + fallback_span + 1;
  if (fallback_span == 0 || back_edge_ip >= fn.code.size() ||
      fn.code[back_edge_ip].op != ir::Op::Jump ||
      fn.code[back_edge_ip].dst != ip || in.dst != back_edge_ip + 1) {
    result.errors.push_back("invalid fused construction/method loop back edge");
    return XlangVMOpFlow::ReturnResult;
  }

  const size_t constructor_call_ip = construct_ip;
  const auto* module_object = value_as_module(frame.globals_module);
  const CallSiteCache* constructor_cache = constructor_call_ip < instr_cache.size()
      ? &instr_cache[constructor_call_ip].call : nullptr;
  const CallSiteCache* method_cache = method_ip < instr_cache.size()
      ? &instr_cache[method_ip].call : nullptr;
  const bool class_slot_valid = module_object != nullptr &&
      fn.code[class_load_ip].a < module_object->slots.size();
  const Value* class_value = class_slot_valid
      ? &module_object->slots[fn.code[class_load_ip].a] : nullptr;
  ClassObject* klass = class_value == nullptr ? nullptr : value_as_class(*class_value);
  const bool caches_match = klass != nullptr && constructor_cache != nullptr &&
      constructor_cache->kind == CallSiteKind::InlineSlotConstructor &&
      constructor_cache->callee_object == &klass->header &&
      constructor_cache->class_version == klass->version &&
      constructor_cache->function != nullptr &&
      method_cache != nullptr && method_cache->kind == CallSiteKind::InlineSelfAttrBinaryMethod &&
      method_cache->callee_object == &klass->header &&
      method_cache->class_version == klass->version &&
      method_cache->function != nullptr && method_cache->inline_op == ir::Op::Add;
  const auto hook_active = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };
  const bool observable = !allow_guarded_fast_path || runtime.debug_step_active() ||
      hook_active(frame.trace_function) || hook_active(runtime.trace_function()) ||
      hook_active(runtime.profile_function()) ||
      sys_monitoring_event_may_dispatch(kSysMonitoringEventAll);
  Value finalizer;
  std::string finalizer_lookup_error;
  const bool class_has_finalizer = class_value != nullptr &&
      object_lookup_class_attr(*class_value, "__del__", finalizer, finalizer_lookup_error);

  uint32_t lhs_constructor_arg = UINT32_MAX;
  uint32_t rhs_constructor_arg = UINT32_MAX;
  bool method_shape_matches = false;
  if (caches_match && constructor_cache->slot_constructor_args.size() == 2 &&
      !klass->has_setattr_hook && !klass->has_getattribute_hook) {
    const auto& ctor_obj = *constructor_cache->function;
    const ir::Module* ctor_module = ctor_obj.module != nullptr
        ? ctor_obj.module.get() : &module;
    const ir::Module* method_module = method_cache->function->module != nullptr
        ? method_cache->function->module.get() : &module;
    if (ctor_obj.function_id < ctor_module->functions.size() &&
        method_cache->function->function_id < method_module->functions.size()) {
      const auto& ctor_fn = ctor_module->functions[ctor_obj.function_id];
      const auto& method_fn = method_module->functions[method_cache->function->function_id];
      if (method_fn.code.size() == 5 &&
          method_fn.code[0].op == ir::Op::LoadLocalAttr &&
          method_fn.code[1].op == ir::Op::LoadLocalAttr &&
          method_fn.code[0].b < method_fn.names.size() &&
          method_fn.code[1].b < method_fn.names.size() &&
          (constructor_cache->slot_constructor_args[0].first &
              kXlangVMInlineConstructorAttrFlag) != 0 &&
          (constructor_cache->slot_constructor_args[1].first &
              kXlangVMInlineConstructorAttrFlag) != 0) {
        const std::string& lhs_name = method_fn.names[method_fn.code[0].b];
        const std::string& rhs_name = method_fn.names[method_fn.code[1].b];
        for (size_t index = 0; index < constructor_cache->slot_constructor_args.size(); ++index) {
          const auto& item = constructor_cache->slot_constructor_args[index];
          const uint32_t name_index = item.first & ~kXlangVMInlineConstructorAttrFlag;
          if (name_index >= ctor_fn.names.size()) break;
          if (ctor_fn.names[name_index] == lhs_name) lhs_constructor_arg = item.second;
          if (ctor_fn.names[name_index] == rhs_name) rhs_constructor_arg = item.second;
        }
        method_shape_matches = lhs_constructor_arg < 2 && rhs_constructor_arg < 2 &&
            lhs_constructor_arg != rhs_constructor_arg &&
            method_cache->lhs_slot < constructor_cache->slot_constructor_args.size() &&
            method_cache->rhs_slot < constructor_cache->slot_constructor_args.size() &&
            lhs_constructor_arg == method_cache->lhs_slot &&
            rhs_constructor_arg == method_cache->rhs_slot;
      }
    }
  }
  const int64_t bound = fn.constants[in.b].as.i64;
  const int64_t rhs_argument = fn.constants[fn.code[constructor_args_ip].c].as.i64;
  const int64_t step = fn.constants[fn.code[increment_ip].b].as.i64;
  const bool safe_class = klass != nullptr && !klass->has_setattr_hook &&
      !klass->has_getattribute_hook &&
      !(klass->restrict_instance_attrs && !klass->allow_instance_dict) &&
      !class_has_finalizer && finalizer_lookup_error.empty();
  const bool eligible = !observable && caches_match && method_shape_matches && safe_class &&
      constructor_cache->slot_constructor_args[0].second < 2 &&
      constructor_cache->slot_constructor_args[1].second < 2 &&
      locals[in.a].tag == ValueTag::Int64 && locals[accumulator_slot].tag == ValueTag::Int64 &&
      locals[in.a].as.i64 < bound;
  if (eligible) {
    int64_t current = locals[in.a].as.i64;
    size_t completed = 0;
    bool must_return_to_dispatcher = false;
    while (current < bound) {
      if ((completed & 31u) == 0 &&
          (interpreter_pending_events() != 0 ||
           sys_monitoring_event_may_dispatch(kSysMonitoringEventAll))) {
        must_return_to_dispatcher = true;
        break;
      }
      const auto& ctor_spec = constructor_cache->slot_constructor_args;
      const int64_t ctor_values[2] = {
          current, rhs_argument};
      const int64_t lhs = ctor_values[lhs_constructor_arg];
      const int64_t rhs = ctor_values[rhs_constructor_arg];
      int64_t method_value = 0;
      int64_t new_total = 0;
      int64_t next_counter = 0;
      if (!xlang_vm_checked_add_i64(lhs, rhs, method_value) ||
          !xlang_vm_checked_add_i64(locals[accumulator_slot].as.i64, method_value, new_total) ||
          !xlang_vm_checked_add_i64(current, step, next_counter)) {
        break;
      }

      value_set_int64(regs[fn.code[constructor_args_ip].dst], current);
      value_assign_fast(regs[fn.code[constructor_args_ip].b],
                        fn.constants[fn.code[constructor_args_ip].c]);
      CallArgsView constructor_call_args;
      constructor_call_args.registers = regs.value_data();
      constructor_call_args.register_args = &fn.call_args[fn.code[construct_ip].b];
      Value instance = Value::instance(*class_value);
      std::string constructor_error;
      if (!xlang_vm_execute_slot_constructor(
              instance, module, *constructor_cache->function,
              constructor_call_args, ctor_spec, regs[fn.code[construct_ip].dst],
              constructor_error)) {
        if (!constructor_error.empty()) {
          return raise_runtime_error(constructor_error)
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        break;
      }
      value_assign_fast(locals[fn.code[save_instance_ip].dst],
                        regs[fn.code[construct_ip].dst]);
      value_assign_fast(regs[fn.code[receiver_pair_ip].dst],
                        locals[accumulator_slot]);
      value_assign_fast(regs[fn.code[receiver_pair_ip].b],
                        locals[fn.code[save_instance_ip].dst]);
      value_set_int64(regs[fn.code[method_ip].dst], method_value);
      value_set_int64(regs[fn.code[sum_ip].dst], new_total);
      value_assign_fast(locals[accumulator_slot], regs[fn.code[sum_ip].dst]);
      value_set_int64(locals[in.a], next_counter);
      current = next_counter;
      ++completed;
    }
    if (completed != 0) {
      frame.release_memoryviews_for_skipped_local_add(fn, increment_ip + 1, fallback_span);
      if (!must_return_to_dispatcher && current >= bound) ip = in.dst;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (must_return_to_dispatcher) return XlangVMOpFlow::ContinueLoop;
  }
  if (allow_guarded_fast_path && locals[in.a].tag == ValueTag::Int64 &&
      locals[in.a].as.i64 >= bound && !observable) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return jump_if_local_const_false(
      generic, fn, runtime, locals, ip, result,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value),
      std::forward<EmitMonitoringEvent>(emit_monitoring_event));
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue,
          typename EmitMonitoringEvent>
XLANG3_NOINLINE inline XlangVMOpFlow for_call_accumulate_loop(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    XlangVMInstrCacheStorage& instr_cache,
    size_t& ip,
    bool allow_guarded_fast_path,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  const uint32_t accumulator_slot =
      (in.c & ir::kCallAccumulateLoopLocalMask) >>
      ir::kCallAccumulateLoopLocalShift;
  const ir::Instr generic{ir::Op::JumpIfLocalConstFalse, in.dst, in.a, in.b,
                          static_cast<uint32_t>(ir::CompareOp::Lt)};
  const size_t sum_load_ip = ip + 1;
  const size_t callee_load_ip = ip + 2;
  const size_t first_two_args_ip = ip + 3;
  const size_t last_arg_ip = ip + 4;
  const size_t call_ip = ip + 5;
  const size_t sum_ip = ip + 6;
  const size_t store_ip = ip + 7;
  const size_t increment_ip = ip + 8;
  if ((in.c & ir::kCallAccumulateLoopFlag) == 0 ||
      accumulator_slot >= locals.size() || in.a >= locals.size() ||
      in.b >= fn.constants.size() || increment_ip >= fn.code.size() ||
      fn.code[sum_load_ip].op != ir::Op::LoadLocal ||
      fn.code[sum_load_ip].a != accumulator_slot ||
      fn.code[callee_load_ip].op != ir::Op::LoadModuleSlot ||
      fn.code[first_two_args_ip].op != ir::Op::LoadLocalConst ||
      fn.code[first_two_args_ip].a != in.a ||
      fn.code[first_two_args_ip].c >= fn.constants.size() ||
      fn.code[last_arg_ip].op != ir::Op::LoadConst ||
      fn.code[last_arg_ip].a >= fn.constants.size() ||
      fn.code[call_ip].op != ir::Op::Call ||
      fn.code[call_ip].a != fn.code[callee_load_ip].dst ||
      (fn.code[call_ip].c & ir::kCallAccumulateLocalFlag) == 0 ||
      (fn.code[call_ip].c & ir::kCallAccumulateLocalMask) != accumulator_slot ||
      fn.code[call_ip].b >= fn.call_args.size() ||
      fn.call_args[fn.code[call_ip].b].size() != 3 ||
      fn.call_args[fn.code[call_ip].b][0] != fn.code[first_two_args_ip].dst ||
      fn.call_args[fn.code[call_ip].b][1] != fn.code[first_two_args_ip].b ||
      fn.call_args[fn.code[call_ip].b][2] != fn.code[last_arg_ip].dst ||
      fn.code[sum_ip].op != ir::Op::Add ||
      fn.code[store_ip].op != ir::Op::StoreLocal ||
      fn.code[store_ip].dst != accumulator_slot ||
      fn.code[increment_ip].op != ir::Op::AddLocalConst ||
      fn.code[increment_ip].dst != in.a || fn.code[increment_ip].a != in.a ||
      fn.code[increment_ip].b >= fn.constants.size() ||
      (fn.code[increment_ip].c & ir::kGuardedLocalAddFlag) == 0 ||
      fn.constants[in.b].tag != ValueTag::Int64 ||
      fn.constants[fn.code[first_two_args_ip].c].tag != ValueTag::Int64 ||
      fn.constants[fn.code[last_arg_ip].a].tag != ValueTag::Int64 ||
      fn.constants[fn.code[increment_ip].b].tag != ValueTag::Int64 ||
      fn.constants[fn.code[increment_ip].b].as.i64 <= 0) {
    result.errors.push_back("invalid fused call accumulation loop");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t fallback_span =
      fn.code[increment_ip].c & ir::kGuardedLocalAddSpanMask;
  const size_t back_edge_ip = increment_ip + fallback_span + 1;
  if (fallback_span == 0 || back_edge_ip >= fn.code.size() ||
      fn.code[back_edge_ip].op != ir::Op::Jump ||
      fn.code[back_edge_ip].dst != ip || in.dst != back_edge_ip + 1) {
    result.errors.push_back("invalid fused call accumulation loop back edge");
    return XlangVMOpFlow::ReturnResult;
  }

  const auto hook_active = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };
  const auto& call = fn.code[call_ip];
  const CallSiteCache* cached = call_ip < instr_cache.size()
      ? &instr_cache[call_ip].call : nullptr;
  const auto* module_object = value_as_module(frame.globals_module);
  const bool module_target_matches = module_object != nullptr &&
      fn.code[callee_load_ip].a < module_object->slots.size() &&
      cached != nullptr &&
      value_as_function(module_object->slots[fn.code[callee_load_ip].a]) == cached->function;
  const bool observable = !allow_guarded_fast_path || runtime.debug_step_active() ||
      hook_active(frame.trace_function) || hook_active(runtime.trace_function()) ||
      hook_active(runtime.profile_function()) ||
      sys_monitoring_event_may_dispatch(kSysMonitoringEventAll);
  const bool cached_shape = cached != nullptr &&
      cached->kind == CallSiteKind::InlineArgBinaryFunction &&
      cached->function != nullptr && module_target_matches &&
      cached->inline_op == ir::Op::Add && cached->next_op == ir::Op::Add &&
      cached->has_next && !cached->next_is_constant && cached->lhs_slot < 3 &&
      cached->rhs_slot < 3 && cached->next_arg < 3;
  const int64_t bound = fn.constants[in.b].as.i64;
  const int64_t first_constant = fn.constants[fn.code[first_two_args_ip].c].as.i64;
  const int64_t last_constant = fn.constants[fn.code[last_arg_ip].a].as.i64;
  const int64_t step = fn.constants[fn.code[increment_ip].b].as.i64;
  const bool eligible = !observable && cached_shape &&
      locals[in.a].tag == ValueTag::Int64 && locals[accumulator_slot].tag == ValueTag::Int64 &&
      locals[in.a].as.i64 < bound;
  if (eligible) {
    int64_t current = locals[in.a].as.i64;
    size_t completed = 0;
    bool must_return_to_dispatcher = false;
    while (current < bound) {
      if ((completed & 31u) == 0 &&
          (interpreter_pending_events() != 0 ||
           sys_monitoring_event_may_dispatch(kSysMonitoringEventAll))) {
        must_return_to_dispatcher = true;
        break;
      }
      const int64_t arguments[3] = {current, first_constant, last_constant};
      int64_t intermediate = 0;
      int64_t function_value = 0;
      int64_t new_total = 0;
      int64_t next_counter = 0;
      if (!xlang_vm_checked_add_i64(
              arguments[cached->lhs_slot], arguments[cached->rhs_slot], intermediate) ||
          !xlang_vm_checked_add_i64(
              intermediate, arguments[cached->next_arg], function_value) ||
          !xlang_vm_checked_add_i64(
              locals[accumulator_slot].as.i64, function_value, new_total) ||
          !xlang_vm_checked_add_i64(current, step, next_counter)) {
        break;
      }
      value_set_int64(locals[accumulator_slot], new_total);
      value_set_int64(locals[in.a], next_counter);
      current = next_counter;
      ++completed;
    }
    if (completed != 0) {
      frame.release_memoryviews_for_skipped_local_add(fn, increment_ip + 1, fallback_span);
      if (!must_return_to_dispatcher && current >= bound) ip = in.dst;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (must_return_to_dispatcher) return XlangVMOpFlow::ContinueLoop;
  }
  if (allow_guarded_fast_path && locals[in.a].tag == ValueTag::Int64 &&
      locals[in.a].as.i64 >= bound && !observable) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return jump_if_local_const_false(
      generic, fn, runtime, locals, ip, result,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value),
      std::forward<EmitMonitoringEvent>(emit_monitoring_event));
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue,
          typename EmitMonitoringEvent>
XLANG3_NOINLINE inline XlangVMOpFlow for_local_move_add_loop(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    XlangVMInstrCacheStorage& instr_cache,
    size_t& ip,
    bool allow_guarded_fast_path,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  if ((in.c & ir::kLocalAppendLoopFlag) != 0) {
    const uint32_t list_slot =
        (in.c & ir::kLocalAppendLoopListMask) >> ir::kLocalAppendLoopListShift;
    const uint32_t step_constant = in.c & ir::kLocalMoveLoopStepConstantMask;
    const size_t pair_ip = ip + 1;
    const size_t call_ip = ip + 2;
    const size_t add_ip = ip + 4;
    const ir::Instr generic{ir::Op::JumpIfLocalConstFalse, in.dst, in.a, in.b,
                            static_cast<uint32_t>(ir::CompareOp::Lt)};
    if (list_slot >= locals.size() || in.a >= locals.size() ||
        in.b >= fn.constants.size() || step_constant >= fn.constants.size() ||
        pair_ip >= fn.code.size() || call_ip >= fn.code.size() ||
        add_ip >= fn.code.size() || fn.code[pair_ip].op != ir::Op::LoadLocalPair ||
        fn.code[pair_ip].a != list_slot || fn.code[pair_ip].c != in.a ||
        fn.code[call_ip].op != ir::Op::CallMethod ||
        fn.code[call_ip].a != fn.code[pair_ip].dst ||
        fn.code[call_ip].c >= fn.call_args.size() ||
        fn.call_args[fn.code[call_ip].c].size() != 1 ||
        fn.call_args[fn.code[call_ip].c][0] != fn.code[pair_ip].b ||
        fn.code[ip + 3].op != ir::Op::Pop ||
        fn.code[ip + 3].a != fn.code[call_ip].dst ||
        fn.code[add_ip].op != ir::Op::AddLocalConst ||
        fn.code[add_ip].dst != in.a || fn.code[add_ip].a != in.a ||
        fn.code[add_ip].b != step_constant ||
        (fn.code[add_ip].c & ir::kGuardedLocalAddFlag) == 0 ||
        fn.constants[in.b].tag != ValueTag::Int64 ||
        fn.constants[step_constant].tag != ValueTag::Int64 ||
        fn.constants[step_constant].as.i64 <= 0) {
      result.errors.push_back("invalid fused list append loop");
      return XlangVMOpFlow::ReturnResult;
    }
    const size_t fallback_span = fn.code[add_ip].c & ir::kGuardedLocalAddSpanMask;
    const size_t jump_ip = add_ip + fallback_span + 1;
    if (fallback_span == 0 || jump_ip >= fn.code.size() ||
        fn.code[jump_ip].op != ir::Op::Jump || fn.code[jump_ip].dst != ip ||
        in.dst != jump_ip + 1) {
      result.errors.push_back("invalid fused list append loop back edge");
      return XlangVMOpFlow::ReturnResult;
    }

    const auto hook_active = [](const Value& hook) {
      return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
    };
    const int64_t bound = fn.constants[in.b].as.i64;
    const int64_t step = fn.constants[step_constant].as.i64;
    auto* append_target = list_slot < locals.size()
        ? value_as_mutable_list_storage(locals[list_slot]) : nullptr;
    const size_t call_dst = fn.code[call_ip].dst;
    const BuiltinMethodSpec* cached_append = call_ip < instr_cache.size() &&
        instr_cache[call_ip].call.kind == CallSiteKind::BuiltinMethodSpec
        ? instr_cache[call_ip].call.builtin_method : nullptr;
    const bool observable = !allow_guarded_fast_path || runtime.debug_step_active() ||
        hook_active(frame.trace_function) || hook_active(runtime.trace_function()) ||
        hook_active(runtime.profile_function()) ||
        sys_monitoring_event_may_dispatch(kSysMonitoringEventAll);
    const bool eligible = !observable && cached_append != nullptr &&
        cached_append->specialization_id == kBuiltinMethodSpecializationListAppend &&
        list_slot < locals.size() && locals[list_slot].tag == ValueTag::Object &&
        append_target != nullptr &&
        locals[in.a].tag == ValueTag::Int64 &&
        locals[in.a].as.i64 < bound &&
        fn.code[call_ip].dst < regs.size();
    if (eligible) {
      // Keep this exact list/int loop in native code and poll asynchronous work
      // every 32 items. Cached registry identity proves this is builtin append;
      // observer guards preserve hooks. Pre-sizing once avoids thousands of
      // tiny vector growth operations while a pending event still truncates
      // storage to the exact committed prefix before control returns to Python.
      int64_t current = locals[in.a].as.i64;
      const uint64_t distance = static_cast<uint64_t>(bound) -
          static_cast<uint64_t>(current);
      const uint64_t iterations = (distance - 1) / static_cast<uint64_t>(step) + 1;
      const uint64_t distance_to_max = static_cast<uint64_t>(INT64_MAX) -
          static_cast<uint64_t>(current);
      const uint64_t safe_increments = distance_to_max / static_cast<uint64_t>(step);
      const uint64_t batch64 = std::min(iterations, safe_increments);
      const size_t old_size = append_target->items.size();
      const size_t available = append_target->items.max_size() - old_size;
      if (batch64 != 0 && batch64 <= available) {
        const size_t batch = static_cast<size_t>(batch64);
        const Value zero = Value::int64(0);
        append_target->items.resize(old_size + batch, zero);
        Value* slots = append_target->items.data() + old_size;
        int64_t item = current;
        size_t appended = 0;
        while (appended < batch) {
          if ((appended & 31u) == 0 &&
              (interpreter_pending_events() != 0 ||
               sys_monitoring_event_may_dispatch(kSysMonitoringEventAll))) {
            break;
          }
          // Resize() prepares exact integer values in one contiguous pass.
          // The hot loop changes only each payload; unrolling amortizes loop
          // control across four elements.
          const size_t group_size = std::min<size_t>(32, batch - appended);
          const size_t group_end = appended + group_size;
          for (; appended + 4 <= group_end; appended += 4) {
            slots[appended].as.i64 = item;
            item += step;
            slots[appended + 1].as.i64 = item;
            item += step;
            slots[appended + 2].as.i64 = item;
            item += step;
            slots[appended + 3].as.i64 = item;
            item += step;
          }
          for (; appended < group_end; ++appended) {
            slots[appended].as.i64 = item;
            item += step;
          }
        }
        append_target->items.resize(old_size + appended);
        value_set_int64(locals[in.a], item);
        value_set_invalid(regs[call_dst]);
        if (appended != batch) {
          // The next uncommitted value is left for generic CallMethod; a signal
          // callback may have replaced append or changed the list instance.
          if (call_ip < instr_cache.size()) {
            instr_cache[call_ip].call.kind = CallSiteKind::Empty;
            instr_cache[call_ip].call.builtin_method = nullptr;
          }
          return XlangVMOpFlow::ContinueLoop;
        }
        frame.release_memoryviews_for_skipped_local_add(fn, add_ip + 1, fallback_span);
        if (batch64 == iterations) ip = in.dst;
        else ip = ip + 1; // Overflowing counter increment uses its Python fallback.
        return XlangVMOpFlow::ContinueLoop;
      }
    }
    if (eligible && locals[in.a].tag == ValueTag::Int64 &&
        locals[in.a].as.i64 >= bound) {
      ip = in.dst;
      return XlangVMOpFlow::ContinueLoop;
    }
    return jump_if_local_const_false(
        generic, fn, runtime, locals, ip, result,
        std::forward<RaiseRuntimeError>(raise_runtime_error),
        std::forward<RaiseExceptionValue>(raise_exception_value),
        std::forward<EmitMonitoringEvent>(emit_monitoring_event));
  }

  const uint32_t move_count =
      (in.c & ir::kLocalMoveLoopMoveCountMask) >>
      ir::kLocalMoveLoopMoveCountShift;
  const uint32_t step_constant = in.c & ir::kLocalMoveLoopStepConstantMask;
  const ir::Instr generic{ir::Op::JumpIfLocalConstFalse, in.dst, in.a, in.b,
                          static_cast<uint32_t>(ir::CompareOp::Lt)};
  if (move_count < 2 || in.a >= locals.size() || in.b >= fn.constants.size() ||
      step_constant >= fn.constants.size() || in.dst > fn.code.size() ||
      fn.constants[in.b].tag != ValueTag::Int64 ||
      fn.constants[step_constant].tag != ValueTag::Int64) {
    result.errors.push_back("invalid fused local move loop");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t add_ip = ip + 1 + move_count;
  if (add_ip >= fn.code.size() ||
      fn.code[add_ip].op != ir::Op::AddLocalConst ||
      fn.code[add_ip].dst != in.a || fn.code[add_ip].a != in.a ||
      fn.code[add_ip].b != step_constant ||
      (fn.code[add_ip].c & ir::kGuardedLocalAddFlag) == 0) {
    result.errors.push_back("invalid fused local move loop body");
    return XlangVMOpFlow::ReturnResult;
  }
  const size_t fallback_span =
      fn.code[add_ip].c & ir::kGuardedLocalAddSpanMask;
  const size_t jump_ip = add_ip + fallback_span + 1;
  if (fallback_span == 0 || jump_ip >= fn.code.size() ||
      fn.code[jump_ip].op != ir::Op::Jump || fn.code[jump_ip].dst != ip ||
      in.dst != jump_ip + 1) {
    result.errors.push_back("invalid fused local move loop back edge");
    return XlangVMOpFlow::ReturnResult;
  }

  bool safe = allow_guarded_fast_path && locals[in.a].tag == ValueTag::Int64;
  const int64_t bound = fn.constants[in.b].as.i64;
  const int64_t step = fn.constants[step_constant].as.i64;
  int64_t next_counter = 0;
  if (safe && locals[in.a].as.i64 < bound &&
      !xlang_vm_checked_add_i64(locals[in.a].as.i64, step, next_counter)) {
    safe = false;
  }
  for (size_t offset = 0; safe && offset < move_count; ++offset) {
    const auto& move = fn.code[ip + 1 + offset];
    if (move.op != ir::Op::MoveLocal || move.dst >= locals.size() ||
        move.a >= locals.size() ||
        (offset == 0
             ? ((move.c & ir::kGuardedLocalMoveFlag) == 0 ||
                (move.c & ir::kGuardedLocalMoveSpanMask) != move_count - 1)
             : move.c != 0)) {
      result.errors.push_back("invalid fused local move loop assignment");
      return XlangVMOpFlow::ReturnResult;
    }
    const auto numeric = [](const Value& value) {
      return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double;
    };
    if (!numeric(locals[move.dst]) || !numeric(locals[move.a])) safe = false;
  }
  if (safe && locals[in.a].as.i64 < bound) {
    // A small batch amortizes returning through the VM dispatcher for each
    // iteration. The exact-scalar guards prevent user callbacks/finalizers;
    // poll the eval breaker for every logical iteration so batching does not
    // delay weakref or signal delivery relative to ordinary VM dispatch.
    constexpr size_t kMaxBatchIterations = 32;
    size_t completed = 0;
    int64_t counter = locals[in.a].as.i64;
    while (completed < kMaxBatchIterations && counter < bound) {
      if (completed != 0) {
        if (runtime.debug_poll_needed()) break;
        const uint32_t pending_events = interpreter_poll_pending_events();
        if (pending_events != 0) {
          interpreter_hint_pending_event_poll(pending_events);
          break;
        }
      }
      if (!xlang_vm_checked_add_i64(counter, step, next_counter)) break;
      // Preserve assignment order: loop-carried local rotations can read a
      // value written by an earlier assignment in the same iteration.
      for (size_t offset = 0; offset < move_count; ++offset) {
        const auto& move = fn.code[ip + 1 + offset];
        value_assign_fast(locals[move.dst], locals[move.a]);
      }
      value_set_int64(locals[in.a], next_counter);
      frame.release_memoryviews_for_skipped_local_add(fn, add_ip + 1, fallback_span);
      counter = next_counter;
      ++completed;
    }
    if (completed != 0) {
      if (counter >= bound) ip = in.dst;
      return XlangVMOpFlow::ContinueLoop;
    }
  }
  if (allow_guarded_fast_path && locals[in.a].tag == ValueTag::Int64 &&
      locals[in.a].as.i64 >= bound) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  // Preserve comparison exceptions, tracing, and Python numeric semantics
  // whenever the exact-int loop conditions do not hold.
  return jump_if_local_const_false(
      generic, fn, runtime, locals, ip, result,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value),
      std::forward<EmitMonitoringEvent>(emit_monitoring_event));
}

XLANG3_HOT_INLINE void setup_except(
    const ir::Instr& in,
    std::vector<ExceptionHandler>& exception_handlers) {
  exception_handlers.push_back({in.dst, ExceptionHandlerKind::Except, 0});
}

XLANG3_HOT_INLINE void setup_with(
    const ir::Instr& in,
    std::vector<ExceptionHandler>& exception_handlers) {
  exception_handlers.push_back({in.dst, ExceptionHandlerKind::With, in.a});
}

XLANG3_HOT_INLINE XlangVMOpFlow pop_except(
    std::vector<ExceptionHandler>& exception_handlers,
    RuntimeResult& result) {
  if (exception_handlers.empty()) {
    result.errors.push_back("invalid exception handler pop");
    return XlangVMOpFlow::ReturnResult;
  }
  exception_handlers.pop_back();
  return XlangVMOpFlow::Next;
}

template <typename RaiseExceptionValue, typename NormalizeException>
XLANG3_HOT_INLINE XlangVMOpFlow raise_op(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Value& pending_cause,
    bool& pending_explicit_cause,
    Value& current_exception,
    RuntimeResult& result,
    RaiseExceptionValue&& raise_exception_value,
    NormalizeException&& normalize_exception) {
  if (in.a >= regs.size()) {
    result.errors.push_back("invalid raise value");
    return XlangVMOpFlow::ReturnResult;
  }
  Value exception = normalize_exception(regs[in.a]);
  if (value_as_instance(exception) != nullptr) {
    std::string ignored;
    if (pending_explicit_cause) {
      object_set_attr(exception, "__cause__", pending_cause, ignored);
      if (current_exception.tag != ValueTag::Invalid &&
          !value_is(exception, current_exception)) {
        object_set_attr(exception, "__context__", current_exception, ignored);
      }
      object_set_attr(exception, "__suppress_context__", Value::boolean(true), ignored);
    } else if (current_exception.tag != ValueTag::Invalid &&
               !value_is(exception, current_exception)) {
      object_set_attr(exception, "__context__", current_exception, ignored);
      object_set_attr(exception, "__suppress_context__", Value::boolean(false), ignored);
    }
  }
  value_set_invalid(pending_cause);
  pending_explicit_cause = false;
  return raise_exception_value(std::move(exception)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

template <typename NormalizeException>
XLANG3_HOT_INLINE XlangVMOpFlow set_exception_cause(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Value& pending_cause,
    bool& pending_explicit_cause,
    RuntimeResult& result,
    NormalizeException&& normalize_exception) {
  if (in.a >= regs.size()) {
    result.errors.push_back("invalid exception cause");
    return XlangVMOpFlow::ReturnResult;
  }
  if (regs[in.a].tag == ValueTag::None) {
    value_set_none(pending_cause);
  } else {
    pending_cause = normalize_exception(regs[in.a]);
  }
  pending_explicit_cause = true;
  return XlangVMOpFlow::Next;
}

template <typename RaiseExceptionValue, typename RaiseRuntimeError, typename EmitMonitoringEvent>
XLANG3_HOT_INLINE XlangVMOpFlow reraise(
    Value& current_exception,
    RaiseExceptionValue&& raise_exception_value,
    RaiseRuntimeError&& raise_runtime_error,
    EmitMonitoringEvent&& emit_monitoring_event) {
  if (current_exception.tag == ValueTag::Invalid) {
    return raise_runtime_error("No active exception to reraise") ? XlangVMOpFlow::ContinueLoop
                                                                : XlangVMOpFlow::ReturnResult;
  }
  if (!emit_monitoring_event(kSysMonitoringEventReraise, &current_exception)) {
    return XlangVMOpFlow::ReturnResult;
  }
  return raise_exception_value(current_exception) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

XLANG3_HOT_INLINE void clear_exception(
    Runtime& runtime,
    Value& current_exception,
    std::vector<Value>& previous_exceptions,
    std::vector<size_t>& active_exception_handler_depths,
    std::vector<size_t>& active_exception_handler_frames) {
  if (!previous_exceptions.empty()) {
    value_assign_fast(current_exception, previous_exceptions.back());
    previous_exceptions.pop_back();
    active_exception_handler_depths.pop_back();
    active_exception_handler_frames.pop_back();
  } else {
    value_set_invalid(current_exception);
  }
  if (current_exception.tag == ValueTag::Invalid) {
    runtime.clear_active_exception();
  } else {
    runtime.set_active_exception(current_exception);
  }
  Value ignored_pending;
  runtime.take_pending_exception(ignored_pending);
}

XLANG3_HOT_INLINE void load_exception(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    const Value& current_exception) {
  value_assign_fast(regs[in.dst], current_exception);
}

XLANG3_HOT_INLINE void set_exception(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& current_exception) {
  value_assign_fast(current_exception, regs[in.a]);
  runtime.set_active_exception(current_exception);
}

XLANG3_HOT_INLINE void load_exception_type(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    const Value& current_exception) {
  regs[in.dst] = runtime.exception_type(current_exception);
}

template <typename ExceptionMatches>
XLANG3_HOT_INLINE void match_exception(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    ExceptionMatches&& exception_matches) {
  value_set_bool(regs[in.dst], exception_matches(regs[in.a]));
}

template <typename EmitMonitoringEvent, typename EmitTraceEvent, typename EmitProfileEvent, typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow yield_op(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    VMFrame& frame,
    std::vector<VMFrame>& frames,
    size_t frame_count,
    GeneratorObject* generator,
    RuntimeResult& result,
    EmitMonitoringEvent&& emit_monitoring_event,
    EmitTraceEvent&& emit_trace_event,
    EmitProfileEvent&& emit_profile_event,
    RaiseRuntimeError&& raise_runtime_error) {
  if (generator == nullptr) {
    return raise_runtime_error("yield used outside generator") ? XlangVMOpFlow::ContinueLoop
                                                              : XlangVMOpFlow::ReturnResult;
  }

  // For an unobserved generator consumed by native any(), false values cannot
  // affect the result. Continue this VM activation in place instead of saving
  // a continuation and re-entering the interpreter for each false yield.
  // The full guards retain normal suspension whenever Python can observe a
  // yield, exception handler, trace/monitoring hook, or profiling counter.
  if (generator->consume_for_any && !value_truthy(regs[in.a]) &&
      !xlang_perf_enabled() &&
      !generator_continuation_has_observers(runtime, frames, frame_count) &&
      !generator_continuation_has_active_exception_handlers(frames, frame_count)) {
    value_set_none(regs[in.dst]);
    return XlangVMOpFlow::Next;
  }
  Value yielded_value;
  value_assign_fast(yielded_value, regs[in.a]);
  if (!emit_monitoring_event(frame, kSysMonitoringEventPyYield, &yielded_value)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!emit_trace_event(frame, "return", yielded_value) ||
      !emit_profile_event(frame, "return", yielded_value)) {
    return XlangVMOpFlow::ReturnResult;
  }
  ++ip;

  auto* state = acquire_generator_vm_state(generator);
  state->frames = std::move(frames);
  state->frame_count = frame_count;
  state->send_target = in.dst;
  if (generator->vm_state_cleanup != nullptr && generator->vm_state != nullptr) {
    generator->vm_state_cleanup(generator->vm_state);
  }
  generator->vm_state = state;
  generator->vm_state_cleanup = destroy_generator_vm_state;
  generator->done = false;
  value_assign_fast(result.value, yielded_value);
  return XlangVMOpFlow::ReturnResult;
}

template <typename FinishFrame>
XLANG3_HOT_INLINE XlangVMOpFlow return_value(
    const Value& source,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame) {
  Value return_value;
  value_assign_fast(return_value, source);
  if (!finish_frame(return_value)) {
    if (generator != nullptr) {
      generator->done = true;
      value_assign_fast(generator->return_value, return_value);
      value_assign_fast(result.value, return_value);
    }
    return XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::SwitchFrame;
}

template <typename RaiseUnboundLocalError, typename RaiseRuntimeError,
          typename RaiseExceptionValue, typename EmitMonitoringEvent>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_local_local_false(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  if (in.a >= locals.size() || in.b >= locals.size()) {
    result.errors.push_back("invalid local local jump");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid || locals[in.b].tag == ValueTag::Invalid) {
    const uint32_t slot = locals[in.a].tag == ValueTag::Invalid ? in.a : in.b;
    const std::string name = slot < fn.locals.size() ? fn.locals[slot] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name +
               "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  Value compare_result;
  const auto op = static_cast<ir::CompareOp>(in.c);
  const auto compare_flow = compare_values(
      op, locals[in.a], locals[in.b], compare_result, runtime,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
  if (compare_flow != XlangVMOpFlow::Next) return compare_flow;
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, compare_result, condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.dst;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename FinishFrame>
XLANG3_HOT_INLINE XlangVMOpFlow return_op(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame) {
  return return_value(regs[in.a], generator, result, std::forward<FinishFrame>(finish_frame));
}

template <typename FinishFrame>
XLANG3_HOT_INLINE XlangVMOpFlow return_const(
    const ir::Instr& in,
    const ir::Function& fn,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame) {
  if (in.a >= fn.constants.size()) {
    result.errors.push_back("invalid return constant");
    return XlangVMOpFlow::ReturnResult;
  }
  return return_value(fn.constants[in.a], generator, result, std::forward<FinishFrame>(finish_frame));
}

template <typename FinishFrame, typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow return_local(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallValueBuffer& locals,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  if (in.a >= locals.size()) {
    result.errors.push_back("invalid return local");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return return_value(locals[in.a], generator, result, std::forward<FinishFrame>(finish_frame));
}

} // namespace xlang3::xlang_vm::ops
