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

#include "xlang_vm_arithmetic.h"

#include "xlang3/interpreter.h"
#include "xlang3/object_model.h"

namespace xlang3 {

struct XlangVMSelfBinaryMethodSpec {
  uint32_t lhs_slot = 0;
  uint32_t rhs_slot = 0;
  ir::Op op = ir::Op::Add;
};

struct XlangVMArgBinaryFunctionSpec {
  uint32_t lhs_arg = 0;
  uint32_t rhs_arg = 0;
  ir::Op op = ir::Op::Add;
  uint32_t next_arg = 0;
  ir::Op next_op = ir::Op::Add;
  bool has_next = false;
};

inline bool xlang_vm_analyze_self_binary_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    XlangVMSelfBinaryMethodSpec& spec) {
  const ir::Module* method_module = &current_module;
  if (fn_obj.module != nullptr) {
    method_module = fn_obj.module.get();
  }
  if (fn_obj.function_id >= method_module->functions.size()) {
    return false;
  }
  const auto& method = method_module->functions[fn_obj.function_id];
  if (method.params.size() != 1 || method.code.size() < 6) {
    return false;
  }
  const auto& load_self_lhs = method.code[0];
  const auto& load_lhs = method.code[1];
  const auto& load_self_rhs = method.code[2];
  const auto& load_rhs = method.code[3];
  const auto& binary = method.code[4];
  const auto& ret = method.code[5];
  if (load_self_lhs.op != ir::Op::LoadLocal || load_self_lhs.a != 0 ||
      load_self_rhs.op != ir::Op::LoadLocal || load_self_rhs.a != 0 ||
      load_lhs.op != ir::Op::LoadInstanceSlot || load_lhs.a != load_self_lhs.dst ||
      load_rhs.op != ir::Op::LoadInstanceSlot || load_rhs.a != load_self_rhs.dst ||
      ret.op != ir::Op::Return || ret.a != binary.dst) {
    return false;
  }
  if (!xlang_vm_is_inline_binary_op(binary.op)) {
    return false;
  }
  if (binary.a != load_lhs.dst || binary.b != load_rhs.dst) {
    return false;
  }
  spec.lhs_slot = load_lhs.b;
  spec.rhs_slot = load_rhs.b;
  spec.op = binary.op;
  return true;
}

inline bool xlang_vm_analyze_arg_binary_function(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    uint32_t argc,
    XlangVMArgBinaryFunctionSpec& spec) {
  const ir::Module* function_module = &current_module;
  if (fn_obj.module != nullptr) {
    function_module = fn_obj.module.get();
  }
  if (fn_obj.function_id >= function_module->functions.size()) {
    return false;
  }
  const auto& function = function_module->functions[fn_obj.function_id];
  if (function.params.size() != argc || function.free_vars.size() != 0 || function.cell_slots.size() != 0 ||
      function.code.size() < 4) {
    return false;
  }
  const auto& load_lhs = function.code[0];
  const auto& load_rhs = function.code[1];
  const auto& binary = function.code[2];
  const auto& ret = function.code[3];
  if (load_lhs.op != ir::Op::LoadLocal || load_rhs.op != ir::Op::LoadLocal ||
      load_lhs.a >= argc || load_rhs.a >= argc || !xlang_vm_is_inline_binary_op(binary.op) ||
      binary.a != load_lhs.dst || binary.b != load_rhs.dst) {
    return false;
  }
  spec.lhs_arg = load_lhs.a;
  spec.rhs_arg = load_rhs.a;
  spec.op = binary.op;
  if (ret.op == ir::Op::Return && ret.a == binary.dst) {
    spec.has_next = false;
    return true;
  }
  if (function.code.size() < 6) {
    return false;
  }
  const auto& load_next = function.code[3];
  const auto& next_binary = function.code[4];
  const auto& next_ret = function.code[5];
  if (load_next.op != ir::Op::LoadLocal || load_next.a >= argc ||
      !xlang_vm_is_inline_binary_op(next_binary.op) ||
      next_binary.a != binary.dst || next_binary.b != load_next.dst ||
      next_ret.op != ir::Op::Return || next_ret.a != next_binary.dst) {
    return false;
  }
  spec.next_arg = load_next.a;
  spec.next_op = next_binary.op;
  spec.has_next = true;
  return true;
}

inline bool xlang_vm_execute_arg_binary_function(
    CallArgsView args,
    const XlangVMArgBinaryFunctionSpec& spec,
    Value& out,
    std::string& error) {
  if (spec.lhs_arg >= args.size() || spec.rhs_arg >= args.size()) {
    error = "invalid inline function arg";
    return false;
  }
  if (!xlang_vm_execute_binary_op(spec.op, args.get(spec.lhs_arg), args.get(spec.rhs_arg), out, error)) {
    return false;
  }
  if (!spec.has_next) {
    return true;
  }
  if (spec.next_arg >= args.size()) {
    error = "invalid inline function arg";
    return false;
  }
  Value temp;
  value_assign_fast(temp, out);
  return xlang_vm_execute_binary_op(spec.next_op, temp, args.get(spec.next_arg), out, error);
}

XLANG3_HOT_INLINE bool xlang_vm_execute_self_binary_method(
    const InstanceObject& instance,
    const XlangVMSelfBinaryMethodSpec& spec,
    Value& out,
    std::string& error) {
  if (spec.lhs_slot >= instance_slot_count(&instance) || spec.rhs_slot >= instance_slot_count(&instance)) {
    error = "invalid instance slot load";
    return false;
  }
  const auto& lhs = instance_slot_at(&instance, spec.lhs_slot);
  const auto& rhs = instance_slot_at(&instance, spec.rhs_slot);
  if (lhs.tag == ValueTag::Invalid || rhs.tag == ValueTag::Invalid) {
    error = "object has no attribute";
    return false;
  }
  switch (spec.op) {
    case ir::Op::Add:
    case ir::Op::Sub:
    case ir::Op::Mul:
    case ir::Op::Div:
    case ir::Op::Mod:
      return xlang_vm_execute_binary_op(spec.op, lhs, rhs, out, error);
    default:
      error = "unsupported inline method operation";
      return false;
  }
}

using SelfBinaryMethodSpec = XlangVMSelfBinaryMethodSpec;
using ArgBinaryFunctionSpec = XlangVMArgBinaryFunctionSpec;

inline bool analyze_self_binary_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    SelfBinaryMethodSpec& spec) {
  return xlang_vm_analyze_self_binary_method(current_module, fn_obj, spec);
}

inline bool analyze_arg_binary_function(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    uint32_t argc,
    ArgBinaryFunctionSpec& spec) {
  return xlang_vm_analyze_arg_binary_function(current_module, fn_obj, argc, spec);
}

inline bool execute_arg_binary_function(
    CallArgsView args,
    const ArgBinaryFunctionSpec& spec,
    Value& out,
    std::string& error) {
  return xlang_vm_execute_arg_binary_function(args, spec, out, error);
}

XLANG3_HOT_INLINE bool execute_self_binary_method(
    const InstanceObject& instance,
    const SelfBinaryMethodSpec& spec,
    Value& out,
    std::string& error) {
  return xlang_vm_execute_self_binary_method(instance, spec, out, error);
}

} // namespace xlang3
