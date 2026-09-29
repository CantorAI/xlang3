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

#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter_events.h"
#include "xlang3/sequence.h"

#include <string>

namespace xlang3::xlang_vm::ops {

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow get_iter(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  std::string error;
  if (auto* range = value_as_range(regs[in.a])) {
    regs[in.dst] = Value::range_iterator(range->start, range->stop, range->step);
    return XlangVMOpFlow::Next;
  }
  if (value_as_list(regs[in.a]) != nullptr) {
    regs[in.dst] = Value::sequence_iterator(regs[in.a], 0);
    return XlangVMOpFlow::Next;
  }
  if (!runtime_get_iter(runtime, regs[in.a], regs[in.dst], error)) {
    Value pending;
    const bool has_pending = runtime.take_pending_exception(pending);
    if (error == "object is not iterable" || error.empty()) {
      error = "'" + std::string(value_binary_type_name(regs[in.a])) + "' object is not iterable";
    } else if (has_pending) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_exception_value(runtime.make_exception("TypeError", error))
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow iter_next(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  bool done = false;
  if (auto* range = value_as_range_iterator(regs[in.a])) {
    done = range->step > 0 ? range->current >= range->stop : range->current <= range->stop;
    if (done) {
      value_set_none(regs[in.dst]);
      ip = in.b;
      return XlangVMOpFlow::ContinueLoop;
    }
    value_set_int64(regs[in.dst], range->current);
    range->current += range->step;
    return XlangVMOpFlow::Next;
  }
  if (auto* iterator = value_as_sequence_iterator(regs[in.a])) {
    if (auto* list = value_as_list(iterator->source)) {
      if (iterator->index >= list->items.size()) {
        value_set_none(regs[in.dst]);
        value_set_invalid(iterator->source);
        ip = in.b;
        return XlangVMOpFlow::ContinueLoop;
      }
      // The destination register can own the iterator source.  Retain the
      // yielded element before replacing that register so container teardown
      // cannot invalidate the element reference.
      Value item = list->items[static_cast<size_t>(iterator->index)];
      value_move_assign_fast(regs[in.dst], item);
      ++iterator->index;
      return XlangVMOpFlow::Next;
    }
  }
  std::string error;
  if (!sequence_iter_next(regs[in.a], done, regs[in.dst], error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  if (done) {
    if (sys_monitoring_event_may_dispatch(kSysMonitoringEventStopIteration)) {
      Value stop = runtime.make_exception("StopIteration", "");
      if (!emit_monitoring_event(kSysMonitoringEventStopIteration, &stop)) {
        return XlangVMOpFlow::ReturnResult;
      }
    }
    ip = in.b;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow iter_next_local(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.dst >= locals.size() || in.c >= regs.size()) {
    return raise_runtime_error("invalid fused iterator local target")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const ir::Instr next_in{ir::Op::IterNext, in.c, in.a, in.b, 0};
  const auto flow = iter_next(
      next_in, runtime, regs, ip,
      std::forward<EmitMonitoringEvent>(emit_monitoring_event),
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
  if (flow != XlangVMOpFlow::Next) return flow;
  value_move_assign_fast(locals[in.dst], regs[in.c]);
  return XlangVMOpFlow::Next;
}

XLANG3_NOINLINE inline bool xlang_vm_try_range_sum_fast_path(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    XlangVMSmallValueBuffer& cells,
    size_t& ip,
    uint32_t accumulator_slot,
    int64_t value,
    int64_t stop,
    int64_t step) {
  const int64_t previous = accumulator_slot == in.a
      ? value : locals[accumulator_slot].as.i64;
  int64_t accumulated = 0;
  if (!xlang_vm_checked_add_i64(previous, value, accumulated)) {
    // Leave every local untouched; the guarded AddLocalLocal and its
    // ordinary fallback perform bigint promotion at the original IPs.
    return false;
  }
  size_t accumulator_cell = SIZE_MAX;
  for (size_t cell_index = 0; cell_index < fn.cell_slots.size(); ++cell_index) {
    if (fn.cell_slots[cell_index] == in.a) {
      if (cell_index >= cells.size() || value_as_cell(cells[cell_index]) == nullptr) {
        return false;
      }
      accumulator_cell = cell_index;
      break;
    }
  }
  value_set_int64(locals[in.a], value);
  if (accumulator_cell != SIZE_MAX) {
    value_assign_fast(value_as_cell(cells[accumulator_cell])->value, locals[in.a]);
  }
  value_set_int64(locals[accumulator_slot], accumulated);
  const size_t add_ip = ip + 1;
  const size_t fallback_span = fn.code[add_ip].c & ir::kGuardedLocalAddSpanMask;
  // Both operands are exact integers and observers are disabled, so this
  // loop may skip the add fallback and back-edge dispatch; all other shapes
  // keep the original body, including bigint and custom-operator behavior.
  frame.release_memoryviews_for_skipped_local_add(fn, add_ip + 1, fallback_span);
  int64_t next = 0;
  if (!xlang_vm_checked_add_i64(value, step, next)) next = stop;
  value_set_int64(locals[in.b], next);
  return true;
}

XLANG3_HOT_INLINE XlangVMOpFlow for_range_const_local_next(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallValueBuffer& locals,
    XlangVMSmallValueBuffer& cells,
    size_t& ip,
    RuntimeResult& result) {
  if (in.a >= locals.size() || in.b >= locals.size() || in.c >= fn.range_specs.size()) {
    result.errors.push_back("invalid fused range loop");
    return XlangVMOpFlow::ReturnResult;
  }
  auto& current = locals[in.b];
  const auto& spec = fn.range_specs[in.c];
  if (current.tag != ValueTag::Int64 ||
      spec.first >= fn.constants.size() ||
      spec.second >= fn.constants.size() ||
      fn.constants[spec.first].tag != ValueTag::Int64 ||
      fn.constants[spec.second].tag != ValueTag::Int64) {
    result.errors.push_back("invalid fused range state");
    return XlangVMOpFlow::ReturnResult;
  }
  const int64_t value = current.as.i64;
  const int64_t stop = fn.constants[spec.first].as.i64;
  const int64_t step = fn.constants[spec.second].as.i64;
  const bool done = step > 0 ? value >= stop : value <= stop;
  if (done) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  value_set_int64(locals[in.a], value);
  for (size_t cell_index = 0; cell_index < fn.cell_slots.size(); ++cell_index) {
    if (fn.cell_slots[cell_index] == in.a) {
      if (cell_index >= cells.size() || value_as_cell(cells[cell_index]) == nullptr) {
        result.errors.push_back("invalid fused range cell");
        return XlangVMOpFlow::ReturnResult;
      }
      value_assign_fast(value_as_cell(cells[cell_index])->value, locals[in.a]);
      break;
    }
  }
  value_set_int64(current, value + step);
  return XlangVMOpFlow::Next;
}

// This is a distinct IR opcode so ordinary range loops keep the original hot
// handler and do not pay a per-iteration fusion-flag check.
XLANG3_NOINLINE inline XlangVMOpFlow for_range_const_local_sum(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    XlangVMSmallValueBuffer& cells,
    size_t& ip,
    bool allow_guarded_fast_path,
    RuntimeResult& result) {
  if ((in.c & ir::kRangeSumFusionFlag) == 0) {
    result.errors.push_back("invalid fused range sum");
    return XlangVMOpFlow::ReturnResult;
  }
  const uint32_t range_spec_index = in.c & ir::kRangeSumSpecMask;
  const uint32_t sum_accumulator =
      (in.c & ir::kRangeSumAccumulatorMask) >> ir::kRangeSumAccumulatorShift;
  if (range_spec_index >= fn.range_specs.size() || sum_accumulator >= locals.size()) {
    result.errors.push_back("invalid fused range sum operands");
    return XlangVMOpFlow::ReturnResult;
  }
  const ir::Instr generic{ir::Op::ForRangeConstLocalNext, in.dst, in.a, in.b,
                          range_spec_index};
  if (in.a >= locals.size() || in.b >= locals.size()) {
    result.errors.push_back("invalid fused range loop");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& spec = fn.range_specs[range_spec_index];
  auto& current = locals[in.b];
  const size_t add_ip = ip + 1;
  const bool addition_shape = add_ip < fn.code.size() &&
      fn.code[add_ip].op == ir::Op::AddLocalLocal &&
      fn.code[add_ip].dst == sum_accumulator &&
      fn.code[add_ip].a == sum_accumulator && fn.code[add_ip].b == in.a &&
      (fn.code[add_ip].c & ir::kGuardedLocalAddFlag) != 0;
  if (current.tag == ValueTag::Int64 && spec.first < fn.constants.size() &&
      spec.second < fn.constants.size() &&
      fn.constants[spec.first].tag == ValueTag::Int64 &&
      fn.constants[spec.second].tag == ValueTag::Int64 && addition_shape) {
    const int64_t stop = fn.constants[spec.first].as.i64;
    const int64_t step = fn.constants[spec.second].as.i64;
    if (step == 0) {
      return for_range_const_local_next(generic, fn, locals, cells, ip, result);
    }
    const int64_t initial = current.as.i64;
    if ((step > 0 ? initial >= stop : initial <= stop)) {
      ip = in.dst;
      return XlangVMOpFlow::ContinueLoop;
    }
    const size_t fallback = fn.code[add_ip].c & ir::kGuardedLocalAddSpanMask;
    const size_t back_edge = add_ip + fallback + 1;
    const bool loop_shape_valid = fallback != 0 && back_edge < fn.code.size() &&
        fn.code[back_edge].op == ir::Op::Jump && fn.code[back_edge].dst == ip;
    size_t loop_cell = SIZE_MAX;
    size_t accumulator_cell = SIZE_MAX;
    for (size_t cell_index = 0; cell_index < fn.cell_slots.size(); ++cell_index) {
      if (fn.cell_slots[cell_index] == in.a) loop_cell = cell_index;
      if (fn.cell_slots[cell_index] == sum_accumulator) accumulator_cell = cell_index;
    }
    const bool cells_valid =
        (loop_cell == SIZE_MAX ||
         (loop_cell < cells.size() && value_as_cell(cells[loop_cell]) != nullptr)) &&
        (accumulator_cell == SIZE_MAX ||
         (accumulator_cell < cells.size() && value_as_cell(cells[accumulator_cell]) != nullptr));
    if (allow_guarded_fast_path && loop_shape_valid && cells_valid &&
        (sum_accumulator == in.a || locals[sum_accumulator].tag == ValueTag::Int64)) {
      // The August VM yielded one range item per dispatch. Once exact-int and
      // observer guards pass, execute this pure range-plus-sum loop in native
      // code, publishing locals/cells every 32 items so pending callbacks see
      // coherent state; overflow and observers return to the original bytecode.
      int64_t index = initial;
      int64_t accumulated = sum_accumulator == in.a ? index : locals[sum_accumulator].as.i64;
      int64_t last_value = index;
      size_t completed = 0;
      bool deopt = false;
      bool must_dispatch = false;
      while (step > 0 ? index < stop : index > stop) {
        if ((completed & 31u) == 0 &&
            (interpreter_pending_events() != 0 ||
             sys_monitoring_event_may_dispatch(kSysMonitoringEventAll))) {
          must_dispatch = true;
          break;
        }
        const int64_t previous = sum_accumulator == in.a ? index : accumulated;
        int64_t next_total = 0;
        int64_t next_index = 0;
        if (!xlang_vm_checked_add_i64(previous, index, next_total) ||
            !xlang_vm_checked_add_i64(index, step, next_index)) {
          deopt = true;
          break;
        }
        accumulated = next_total;
        last_value = index;
        index = next_index;
        ++completed;
      }
      if (must_dispatch && completed == 0) return XlangVMOpFlow::ContinueLoop;
      if (completed != 0) {
        value_set_int64(current, index);
        if (sum_accumulator == in.a) {
          value_set_int64(locals[in.a], accumulated);
        } else {
          value_set_int64(locals[in.a], last_value);
          value_set_int64(locals[sum_accumulator], accumulated);
        }
        if (loop_cell != SIZE_MAX) {
          value_assign_fast(value_as_cell(cells[loop_cell])->value, locals[in.a]);
        }
        if (accumulator_cell != SIZE_MAX && accumulator_cell != loop_cell) {
          value_assign_fast(value_as_cell(cells[accumulator_cell])->value,
                            locals[sum_accumulator]);
        }
        frame.release_memoryviews_for_skipped_local_add(fn, add_ip + 1, fallback);
      }
      if (deopt) {
        // The failing iteration remains untouched; let its ordinary range yield
        // and AddLocalLocal perform Python integer promotion.
        return for_range_const_local_next(generic, fn, locals, cells, ip, result);
      }
      if (completed != 0) {
        if (step > 0 ? index >= stop : index <= stop) ip = in.dst;
        return XlangVMOpFlow::ContinueLoop;
      }
    }
  }
  // Overflow, a non-exact integer, or active observers executes the untouched
  // range instruction and then the guarded add's original fallback bytecode.
  return for_range_const_local_next(generic, fn, locals, cells, ip, result);
}

} // namespace xlang3::xlang_vm::ops
