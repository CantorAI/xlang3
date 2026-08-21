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

#include <cstddef>

/*
Dummy op examples for future xlang_vm_op_rows.h edits.

This header is not included by the VM build. It is a code-shaped reference for
future maintainers and LLM edits. Both examples intentionally accept the full
row-local contract so the available loop locals/helpers are visible in code.
Real op handlers should pass only the values they actually use.
*/

namespace xlang3::xlang_vm::ops {

template <
    typename Instr,
    typename Function,
    typename Module,
    typename ModuleOwner,
    typename Registers,
    typename Locals,
    typename Cells,
    typename Closure,
    typename ExceptionHandlers,
    typename NativeCallArgs,
    typename GlobalsModule,
    typename Globals,
    typename GlobalVersion,
    typename InstrCache,
    typename Runtime,
    typename RuntimeResultValue,
    typename CurrentException,
    typename ExecutionLock,
    typename Generator,
    typename Frames,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue,
    typename NormalizeException,
    typename ExceptionMatches,
    typename FinishFrame,
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename CallBuiltinTypeConstructor,
    typename AnalyzeConstMethod,
    typename AnalyzeArgBinaryFunction,
    typename ExecuteArgBinaryFunction,
    typename AnalyzeSelfBinaryMethod,
    typename ExecuteSelfBinaryMethod,
    typename AnalyzeSelfSlotMethod,
    typename ExecuteSelfSlotMethod,
    typename AnalyzeSelfSlotConstSumMethod,
    typename ExecuteSelfSlotConstSumMethod,
    typename AnalyzeSlotConstructor,
    typename ExecuteSlotConstructor>
XLANG3_HOT_INLINE void dummy_example_fast_op(
    const Instr& in,
    const Function& fn,
    const Module& module,
    ModuleOwner& module_owner,
    size_t& ip,
    Registers& regs,
    Locals& locals,
    Cells& cells,
    Closure& fn_obj_closure,
    ExceptionHandlers& exception_handlers,
    NativeCallArgs& native_call_args,
    GlobalsModule& globals_module,
    Globals& globals,
    GlobalVersion& globals_version,
    InstrCache& instr_cache,
    Runtime& runtime,
    RuntimeResultValue& result,
    CurrentException& current_exception,
    ExecutionLock& execution_lock,
    Generator* generator,
    Frames& frames,
    size_t& frame_count,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    NormalizeException&& normalize_exception,
    ExceptionMatches&& exception_matches,
    FinishFrame&& finish_frame,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor,
    AnalyzeConstMethod&& analyze_const_method,
    AnalyzeArgBinaryFunction&& analyze_arg_binary_function,
    ExecuteArgBinaryFunction&& execute_arg_binary_function,
    AnalyzeSelfBinaryMethod&& analyze_self_binary_method,
    ExecuteSelfBinaryMethod&& execute_self_binary_method,
    AnalyzeSelfSlotMethod&& analyze_self_slot_method,
    ExecuteSelfSlotMethod&& execute_self_slot_method,
    AnalyzeSelfSlotConstSumMethod&& analyze_self_slot_const_sum_method,
    ExecuteSelfSlotConstSumMethod&& execute_self_slot_const_sum_method,
    AnalyzeSlotConstructor&& analyze_slot_constructor,
    ExecuteSlotConstructor&& execute_slot_constructor) {
  (void)fn;
  (void)module;
  (void)module_owner;
  (void)ip;
  (void)locals;
  (void)cells;
  (void)fn_obj_closure;
  (void)exception_handlers;
  (void)native_call_args;
  (void)globals_module;
  (void)globals;
  (void)globals_version;
  (void)instr_cache;
  (void)runtime;
  (void)result;
  (void)current_exception;
  (void)execution_lock;
  (void)generator;
  (void)frames;
  (void)frame_count;
  (void)raise_runtime_error;
  (void)raise_exception_value;
  (void)normalize_exception;
  (void)exception_matches;
  (void)finish_frame;
  (void)make_generator_if_needed;
  (void)push_frame;
  (void)call_builtin_type_constructor;
  (void)analyze_const_method;
  (void)analyze_arg_binary_function;
  (void)execute_arg_binary_function;
  (void)analyze_self_binary_method;
  (void)execute_self_binary_method;
  (void)analyze_self_slot_method;
  (void)execute_self_slot_method;
  (void)analyze_self_slot_const_sum_method;
  (void)execute_self_slot_const_sum_method;
  (void)analyze_slot_constructor;
  (void)execute_slot_constructor;

  regs[in.dst] = regs[in.a];
}

template <
    typename Instr,
    typename Function,
    typename Module,
    typename ModuleOwner,
    typename Registers,
    typename Locals,
    typename Cells,
    typename Closure,
    typename ExceptionHandlers,
    typename NativeCallArgs,
    typename GlobalsModule,
    typename Globals,
    typename GlobalVersion,
    typename InstrCache,
    typename Runtime,
    typename RuntimeResultValue,
    typename CurrentException,
    typename ExecutionLock,
    typename Generator,
    typename Frames,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue,
    typename NormalizeException,
    typename ExceptionMatches,
    typename FinishFrame,
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename CallBuiltinTypeConstructor,
    typename AnalyzeConstMethod,
    typename AnalyzeArgBinaryFunction,
    typename ExecuteArgBinaryFunction,
    typename AnalyzeSelfBinaryMethod,
    typename ExecuteSelfBinaryMethod,
    typename AnalyzeSelfSlotMethod,
    typename ExecuteSelfSlotMethod,
    typename AnalyzeSelfSlotConstSumMethod,
    typename ExecuteSelfSlotConstSumMethod,
    typename AnalyzeSlotConstructor,
    typename ExecuteSlotConstructor>
XLANG3_HOT_INLINE XlangVMOpFlow dummy_example_flow_op(
    const Instr& in,
    const Function& fn,
    const Module& module,
    ModuleOwner& module_owner,
    size_t& ip,
    Registers& regs,
    Locals& locals,
    Cells& cells,
    Closure& fn_obj_closure,
    ExceptionHandlers& exception_handlers,
    NativeCallArgs& native_call_args,
    GlobalsModule& globals_module,
    Globals& globals,
    GlobalVersion& globals_version,
    InstrCache& instr_cache,
    Runtime& runtime,
    RuntimeResultValue& result,
    CurrentException& current_exception,
    ExecutionLock& execution_lock,
    Generator* generator,
    Frames& frames,
    size_t& frame_count,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    NormalizeException&& normalize_exception,
    ExceptionMatches&& exception_matches,
    FinishFrame&& finish_frame,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor,
    AnalyzeConstMethod&& analyze_const_method,
    AnalyzeArgBinaryFunction&& analyze_arg_binary_function,
    ExecuteArgBinaryFunction&& execute_arg_binary_function,
    AnalyzeSelfBinaryMethod&& analyze_self_binary_method,
    ExecuteSelfBinaryMethod&& execute_self_binary_method,
    AnalyzeSelfSlotMethod&& analyze_self_slot_method,
    ExecuteSelfSlotMethod&& execute_self_slot_method,
    AnalyzeSelfSlotConstSumMethod&& analyze_self_slot_const_sum_method,
    ExecuteSelfSlotConstSumMethod&& execute_self_slot_const_sum_method,
    AnalyzeSlotConstructor&& analyze_slot_constructor,
    ExecuteSlotConstructor&& execute_slot_constructor) {
  (void)module;
  (void)module_owner;
  (void)ip;
  (void)locals;
  (void)cells;
  (void)fn_obj_closure;
  (void)exception_handlers;
  (void)native_call_args;
  (void)globals_module;
  (void)globals;
  (void)globals_version;
  (void)instr_cache;
  (void)runtime;
  (void)current_exception;
  (void)execution_lock;
  (void)generator;
  (void)frames;
  (void)frame_count;
  (void)raise_runtime_error;
  (void)raise_exception_value;
  (void)normalize_exception;
  (void)exception_matches;
  (void)finish_frame;
  (void)make_generator_if_needed;
  (void)push_frame;
  (void)call_builtin_type_constructor;
  (void)analyze_const_method;
  (void)analyze_arg_binary_function;
  (void)execute_arg_binary_function;
  (void)analyze_self_binary_method;
  (void)execute_self_binary_method;
  (void)analyze_self_slot_method;
  (void)execute_self_slot_method;
  (void)analyze_self_slot_const_sum_method;
  (void)execute_self_slot_const_sum_method;
  (void)analyze_slot_constructor;
  (void)execute_slot_constructor;

  if (in.a >= fn.constants.size()) {
    result.errors.push_back("invalid example op");
    return XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(regs[in.dst], fn.constants[in.a]);
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops