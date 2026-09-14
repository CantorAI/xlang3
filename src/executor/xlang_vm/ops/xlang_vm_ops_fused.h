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

#include "xlang_vm_ops_attr.h"
#include "xlang_vm_ops_containers.h"
#include "xlang_vm_ops_variables.h"

namespace xlang3::xlang_vm::ops {

template <typename MakeGeneratorIfNeeded, typename PushFrame,
          typename RaiseNameError, typename RaiseRuntimeError,
          typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow load_module_attr(
    const ir::Instr& in, const ir::Function& fn, const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner, Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs, Value& globals_module,
    std::unordered_map<std::string, Value>& globals,
    std::vector<XlangVMInstrCache>& instr_cache,
    std::vector<Value>& native_call_args, size_t& ip, RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed, PushFrame&& push_frame,
    RaiseNameError&& raise_name_error, RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.c >= regs.size()) {
    result.errors.push_back("invalid module attribute receiver register");
    return XlangVMOpFlow::ReturnResult;
  }
  const ir::Instr module_load{ir::Op::LoadModuleSlot, in.c, in.a, 0, 0};
  XlangVMOpFlow flow = load_module_slot(
      module_load, module, runtime, regs, globals_module, globals,
      instr_cache[ip], result, raise_name_error, raise_exception_value);
  if (flow != XlangVMOpFlow::Next) return flow;
  const ir::Instr attr_load{ir::Op::LoadAttr, in.dst, in.c, in.b, 0};
  return load_attr(
      attr_load, fn, module, module_owner, runtime, regs, instr_cache,
      native_call_args, ip, result, execution_lock,
      std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
      std::forward<PushFrame>(push_frame),
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
}

template <typename RaiseUnboundLocalError, typename RaiseRuntimeError,
          typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow load_local_get_item(
    const ir::Instr& in, const ir::Function& fn, Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs, XlangVMSmallValueBuffer& locals,
    XlangVMInstrCache& cache, RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.a >= locals.size() || in.c >= regs.size()) {
    result.errors.push_back("invalid local getitem receiver");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name +
               "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.c], locals[in.a]);
  const ir::Instr item_load{ir::Op::GetItem, in.dst, in.c, in.b, 0};
  return get_item(
      item_load, runtime, regs, cache,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
}

} // namespace xlang3::xlang_vm::ops
