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

#include "../xlang_vm_op_switch.h"

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/sequence.h"

#include <string>

namespace xlang3::xlang_vm::ops {

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow get_iter(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  std::string error;
  if (auto* range = value_as_range(regs[in.a])) {
    regs[in.dst] = Value::range_iterator(range->start, range->stop, range->step);
    return XlangVMOpFlow::Next;
  }
  if (value_as_list(regs[in.a]) != nullptr) {
    regs[in.dst] = Value::sequence_iterator(regs[in.a], 0);
    return XlangVMOpFlow::Next;
  }
  if (!sequence_get_iter(regs[in.a], regs[in.dst], error)) {
    Value iter_method;
    std::string attr_error;
    if (attribute_get(regs[in.a], "__iter__", iter_method, attr_error)) {
      Value iter_result;
      std::string call_error;
      if (!runtime_call_callable(runtime, iter_method, nullptr, 0, iter_result, call_error)) {
        return raise_runtime_error(call_error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      Value concrete_iterator;
      std::string concrete_error;
      if (sequence_get_iter(iter_result, concrete_iterator, concrete_error)) {
        value_assign_fast(regs[in.dst], concrete_iterator);
      } else {
        regs[in.dst] = functional_protocol_iterator(&runtime, std::move(iter_result));
      }
      return XlangVMOpFlow::Next;
    }
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow iter_next(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    RaiseRuntimeError&& raise_runtime_error) {
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
        ip = in.b;
        return XlangVMOpFlow::ContinueLoop;
      }
      value_assign_fast(regs[in.dst], list->items[static_cast<size_t>(iterator->index)]);
      ++iterator->index;
      return XlangVMOpFlow::Next;
    }
  }
  std::string error;
  if (!sequence_iter_next(regs[in.a], done, regs[in.dst], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  if (done) {
    ip = in.b;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow for_range_const_local_next(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallValueBuffer& locals,
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
  value_set_int64(current, value + step);
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
