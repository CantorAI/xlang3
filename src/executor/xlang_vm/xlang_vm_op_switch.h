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

#include "xlang3/compiler.h"

/*
XlangVM opcode switch contract
Author: Shawn Xiong

The VM loop includes xlang_vm_op_rows.h inside switch(in.op). Each row expands
through one of these macros:

  XLANG3_VM_FAST(op, call)
    For op implementations that cannot change VM control flow. The expanded
    switch case only executes call and break; there are no flow checks.

  XLANG3_VM_FLOW(op, call)
    For op implementations that may need the caller's continue, return result,
    or goto switch_frame behavior. The op returns XlangVMOpFlow.

Both row styles call real header-visible inline/template functions. The op
implementation files must not contain case labels and must not use macro-body
line continuations. Complex jumps are represented by XlangVMOpFlow.

Rows are expanded inside Interpreter::run_function. Future LLM/code changes
should treat xlang_vm_op_rows.h as the only place that maps an IR opcode to an
op handler call. Each row must pass only the loop locals that handler actually
uses. Do not add a catch-all context parameter just to shorten the row.

The row call sites may use these loop locals and helpers when needed:

  Instruction and code:
    in, fn, module, module_owner, ip

  Current frame storage:
    regs, locals, cells, fn_obj_closure, exception_handlers, native_call_args

  Module/global state:
    globals_module, globals_, globals_version_

  Inline caches:
    instr_cache

  Runtime/result/error flow:
    runtime_, result, current_exception, execution_lock, generator,
    frames, frame_count

  Local loop helpers:
    raise_runtime_error, raise_exception_value, normalize_exception,
    exception_matches, finish_frame, make_generator_if_needed, push_frame,
    call_builtin_type_constructor

  Inline analysis/execution helpers:
    analyze_const_method, analyze_arg_binary_function,
    execute_arg_binary_function, analyze_self_binary_method,
    execute_self_binary_method, analyze_self_slot_method,
    execute_self_slot_method, analyze_self_slot_const_sum_method,
    execute_self_slot_const_sum_method, analyze_slot_constructor,
    execute_slot_constructor

When adding a new op:
  1. Put the handler in the matching ops/xlang_vm_ops_*.h file.
  2. Make hot handlers XLANG3_HOT_INLINE.
  3. Add exactly one row in xlang_vm_op_rows.h.
  4. Pass the smallest useful subset from the list above.
  5. Keep xlang_vm_loop.cpp as the owner of the loop, not of op bodies.

Pattern recognition rule:
  Do not add one-off hardcoded checks for a specific Python function or method
  inside generic op handlers. If an optimization recognizes a function,
  method, constructor, or operation pattern, add a type/builtin registry entry
  that resolves to a small id, store that id in the relevant cache, and dispatch
  from the id on the hot path. Generic call/attr/container ops should ask the
  registry/cache, not compare specific names directly.
*/

namespace xlang3 {

enum class XlangVMOpFlow {
  Next,
  ContinueLoop,
  ReturnResult,
  SwitchFrame,
};

} // namespace xlang3



#define XLANG3_VM_FAST(op_name, call_expr) \
  case ir::Op::op_name: {                  \
    call_expr;                             \
    break;                                 \
  }

#define XLANG3_VM_FLOW(op_name, call_expr)                         \
  case ir::Op::op_name: {                                          \
    const XlangVMOpFlow flow = call_expr;                          \
    if (flow != XlangVMOpFlow::Next) {                             \
      if (flow == XlangVMOpFlow::ContinueLoop) continue;           \
      if (flow == XlangVMOpFlow::ReturnResult) return result;      \
      goto switch_frame;                                           \
    }                                                              \
    break;                                                         \
  }
