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
#include "../xlang_vm_op_switch.h"

#include "xlang3/generator.h"

#ifndef XLANG3_EMBEDDED
#include "task_objects.h"
#endif

#include <string>
#include <vector>

namespace xlang3::xlang_vm::ops {

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow await_op(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
#ifndef XLANG3_EMBEDDED
  std::string await_error;
  if (!xlang_task_await_value(runtime, regs[in.a], regs[in.dst], await_error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(await_error.empty() ? "await failed" : await_error)
               ? XlangVMOpFlow::ContinueLoop
               : XlangVMOpFlow::ReturnResult;
  }
#else
  value_assign_fast(regs[in.dst], regs[in.a]);
#endif
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow yield_from(
    RaiseRuntimeError&& raise_runtime_error) {
  return raise_runtime_error("internal yield from was not lowered") ? XlangVMOpFlow::ContinueLoop
                                                                   : XlangVMOpFlow::ReturnResult;
}

XLANG3_HOT_INLINE void pop() {}

} // namespace xlang3::xlang_vm::ops
