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
  Value next_constant;
  bool has_next = false;
  bool next_is_constant = false;
};

struct XlangVMConditionalArgFunctionSpec {
  uint32_t condition_arg = 0;
  uint32_t true_arg = 0;
  uint32_t false_arg = 0;
  ir::CompareOp compare = ir::CompareOp::Eq;
  ir::Op true_op = ir::Op::Add;
  ir::Op false_op = ir::Op::Add;
  Value condition_constant;
  Value true_constant;
  Value false_constant;
};

inline bool xlang_vm_has_direct_positional_signature(
    const ir::Function& function,
    uint32_t argc) {
  if (function.params.size() != argc) return false;
  if (function.signature.empty()) return true;
  if (function.signature.size() != function.params.size()) return false;
  for (const auto& parameter : function.signature) {
    if (parameter.kind != ir::ParamKind::PosOnly &&
        parameter.kind != ir::ParamKind::PosOrKeyword) {
      return false;
    }
  }
  return true;
}

inline bool xlang_vm_analyze_conditional_arg_function(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    uint32_t argc,
  XlangVMConditionalArgFunctionSpec& out) {
  const ir::Module* fn_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= fn_module->functions.size()) return false;
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.is_generator || function.free_vars.size() != 0 ||
      function.cell_slots.size() != 0 ||
      !xlang_vm_has_direct_positional_signature(function, argc) ||
      function.code.size() < 8) {
    return false;
  }
  const auto& branch = function.code[0];
  if (branch.op != ir::Op::JumpIfLocalConstFalse ||
      branch.a >= argc || branch.b >= function.constants.size() ||
      branch.c > static_cast<uint32_t>(ir::CompareOp::Ge) ||
      branch.dst < 4 || branch.dst + 2 >= function.code.size()) {
    return false;
  }
  auto read_result_branch = [&](size_t start, uint32_t& arg, ir::Op& binary_op,
                                Value& constant) -> bool {
    const auto& load = function.code[start];
    const auto& binary = function.code[start + 1];
    const auto& ret = function.code[start + 2];
    if (load.op != ir::Op::LoadLocalConst || load.a >= argc ||
        load.b >= function.register_count || load.c >= function.constants.size() ||
        !xlang_vm_is_inline_binary_op(binary.op) ||
        binary.a != load.dst || binary.b != load.b ||
        ret.op != ir::Op::Return || ret.a != binary.dst) {
      return false;
    }
    arg = load.a;
    binary_op = binary.op;
    value_assign_fast(constant, function.constants[load.c]);
    return true;
  };
  if (!read_result_branch(1, out.true_arg, out.true_op, out.true_constant) ||
      !read_result_branch(branch.dst, out.false_arg, out.false_op, out.false_constant)) {
    return false;
  }
  out.condition_arg = branch.a;
  out.compare = static_cast<ir::CompareOp>(branch.c);
  value_assign_fast(out.condition_constant, function.constants[branch.b]);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_execute_conditional_arg_function(
    CallArgsView args,
    const XlangVMConditionalArgFunctionSpec& spec,
    Value& out,
    std::string& error) {
  if (spec.condition_arg >= args.size() || spec.true_arg >= args.size() ||
      spec.false_arg >= args.size()) {
    return false;
  }
  Value comparison;
  if (!xlang_vm_fast_compare(
          spec.compare, args.get(spec.condition_arg), spec.condition_constant, comparison)) {
    return false;
  }
  const bool condition = value_truthy(comparison);
  return xlang_vm_execute_binary_op(
      condition ? spec.true_op : spec.false_op,
      args.get(condition ? spec.true_arg : spec.false_arg),
      condition ? spec.true_constant : spec.false_constant,
      out,
      error);
}

struct XlangVMTrivialFunctionSpec {
  bool returns_argument = false;
  uint32_t argument = 0;
  Value constant;
};

using XlangVMSlotConstructorSpec = std::vector<std::pair<uint32_t, uint32_t>>;

inline bool xlang_vm_analyze_trivial_function(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    uint32_t argc,
    XlangVMTrivialFunctionSpec& spec) {
  const ir::Module* function_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= function_module->functions.size()) return false;
  const auto& function = function_module->functions[fn_obj.function_id];
  if (!function.is_generator && xlang_vm_has_direct_positional_signature(function, argc) &&
      function.free_vars.empty() && function.cell_slots.empty() &&
      !function.code.empty()) {
    const auto& direct = function.code[0];
    if (direct.op == ir::Op::ReturnLocal && direct.a < argc) {
      spec.returns_argument = true;
      spec.argument = direct.a;
      value_set_invalid(spec.constant);
      return true;
    }
    if (direct.op == ir::Op::ReturnConst && direct.a < function.constants.size()) {
      spec.returns_argument = false;
      spec.argument = 0;
      value_assign_fast(spec.constant, function.constants[direct.a]);
      return true;
    }
  }
  if (function.is_generator || !xlang_vm_has_direct_positional_signature(function, argc) ||
      !function.free_vars.empty() || !function.cell_slots.empty() ||
      (function.code.size() != 2 && function.code.size() != 4)) {
    return false;
  }
  if (function.code.size() == 4) {
    const auto& implicit_none = function.code[2];
    const auto& implicit_return = function.code[3];
    if (implicit_none.op != ir::Op::LoadConst ||
        implicit_none.a >= function.constants.size() ||
        function.constants[implicit_none.a].tag != ValueTag::None ||
        implicit_return.op != ir::Op::Return ||
        implicit_return.a != implicit_none.dst) {
      return false;
    }
  }
  const auto& load = function.code[0];
  const auto& ret = function.code[1];
  if (ret.op != ir::Op::Return || ret.a != load.dst) return false;
  if (load.op == ir::Op::LoadLocal && load.a < argc) {
    spec.returns_argument = true;
    spec.argument = load.a;
    value_set_invalid(spec.constant);
    return true;
  }
  if (load.op == ir::Op::LoadConst && load.a < function.constants.size()) {
    spec.returns_argument = false;
    spec.argument = 0;
    value_assign_fast(spec.constant, function.constants[load.a]);
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool xlang_vm_execute_trivial_function(
    CallArgsView args,
    const XlangVMTrivialFunctionSpec& spec,
    Value& out) {
  if (spec.returns_argument) {
    if (spec.argument >= args.size()) return false;
    value_assign_fast(out, args.get(spec.argument));
  } else {
    value_assign_fast(out, spec.constant);
  }
  return true;
}

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
  if (!method.is_generator && method.params.size() == 1 && method.code.size() >= 4 &&
      method.code[0].op == ir::Op::LoadLocalInstanceSlot && method.code[0].a == 0 &&
      method.code[1].op == ir::Op::LoadLocalInstanceSlot && method.code[1].a == 0 &&
      xlang_vm_is_inline_binary_op(method.code[2].op) &&
      method.code[2].a == method.code[0].dst && method.code[2].b == method.code[1].dst &&
      method.code[3].op == ir::Op::Return && method.code[3].a == method.code[2].dst) {
    spec.lhs_slot = method.code[0].b;
    spec.rhs_slot = method.code[1].b;
    spec.op = method.code[2].op;
    return true;
  }
  if (method.is_generator || method.params.size() != 1 || method.code.size() < 6) {
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
  if (!function.is_generator && xlang_vm_has_direct_positional_signature(function, argc) &&
      function.free_vars.empty() && function.cell_slots.empty() &&
      function.code.size() >= 4) {
    const auto& first = function.code[0];
    const auto& load_next = function.code[1];
    const auto& next_binary = function.code[2];
    const auto& ret = function.code[3];
    if (first.op == ir::Op::AddLocalLocal && first.a < argc && first.b < argc &&
        load_next.op == ir::Op::LoadLocalConst && load_next.a == first.dst &&
        load_next.c < function.constants.size() &&
        xlang_vm_is_inline_binary_op(next_binary.op) &&
        next_binary.a == load_next.dst && next_binary.b == load_next.b &&
        ret.op == ir::Op::Return && ret.a == next_binary.dst) {
      spec.lhs_arg = first.a;
      spec.rhs_arg = first.b;
      spec.op = ir::Op::Add;
      spec.next_arg = 0;
      spec.next_op = next_binary.op;
      value_assign_fast(spec.next_constant, function.constants[load_next.c]);
      spec.has_next = true;
      spec.next_is_constant = true;
      return true;
    }
  }
  if (!function.is_generator && xlang_vm_has_direct_positional_signature(function, argc) && function.free_vars.empty() &&
      function.cell_slots.empty() && function.code.size() >= 3 &&
      function.code[0].op == ir::Op::LoadLocalPair) {
    if (function.code[0].a >= argc || function.code[0].c >= argc ||
        !xlang_vm_is_inline_binary_op(function.code[1].op) ||
        function.code[1].a != function.code[0].dst ||
        function.code[1].b != function.code[0].b ||
        function.code[2].op != ir::Op::Return ||
        function.code[2].a != function.code[1].dst) {
      return false;
    }
    spec.lhs_arg = function.code[0].a;
    spec.rhs_arg = function.code[0].c;
    spec.op = function.code[1].op;
    spec.has_next = false;
    spec.next_is_constant = false;
    return true;
  }
  if (function.is_generator || !xlang_vm_has_direct_positional_signature(function, argc) || function.free_vars.size() != 0 ||
      function.cell_slots.size() != 0 || function.code.size() < 4) {
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
    spec.next_is_constant = false;
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
  spec.next_is_constant = false;
  return true;
}

inline bool xlang_vm_analyze_slot_constructor(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    XlangVMSlotConstructorSpec& spec) {
  const ir::Module* function_module = &current_module;
  if (fn_obj.module != nullptr) {
    function_module = fn_obj.module.get();
  }
  if (fn_obj.function_id >= function_module->functions.size()) {
    return false;
  }
  const auto& function = function_module->functions[fn_obj.function_id];
  if (function.params.empty() || function.free_vars.size() != 0 || function.cell_slots.size() != 0) {
    return false;
  }
  if (!function.signature.empty()) {
    if (function.signature.size() != function.params.size()) return false;
    for (const auto& parameter : function.signature) {
      if (parameter.kind != ir::ParamKind::PosOrKeyword ||
          parameter.default_reg != UINT32_MAX) {
        return false;
      }
    }
  }
  spec.clear();
  size_t ip = 0;
  while (ip + 1 < function.code.size()) {
    const auto& load_arg = function.code[ip];
    const auto& store_slot = function.code[ip + 1];
    if (load_arg.op != ir::Op::LoadLocal || load_arg.a == 0 ||
        load_arg.a >= function.params.size() ||
        store_slot.op != ir::Op::StoreLocalInstanceSlot || store_slot.dst != 0 ||
        store_slot.b != load_arg.dst) {
      break;
    }
    spec.push_back(std::make_pair(store_slot.a, load_arg.a - 1));
    ip += 2;
  }
  if (!spec.empty()) {
    if (ip < function.code.size() && function.code[ip].op == ir::Op::ReturnConst) {
      return function.code[ip].a < function.constants.size() &&
          function.constants[function.code[ip].a].tag == ValueTag::None &&
          ip + 1 == function.code.size();
    }
    if (ip + 1 >= function.code.size()) return false;
    const auto& load_none = function.code[ip];
    const auto& ret = function.code[ip + 1];
    return load_none.op == ir::Op::LoadConst && ret.op == ir::Op::Return &&
        ret.a == load_none.dst && ip + 2 == function.code.size();
  }
  while (ip + 2 < function.code.size()) {
    const auto& load_self = function.code[ip];
    const auto& load_arg = function.code[ip + 1];
    const auto& store_slot = function.code[ip + 2];
    if (load_self.op != ir::Op::LoadLocal || load_self.a != 0) {
      break;
    }
    if (load_arg.op != ir::Op::LoadLocal || load_arg.a == 0 || load_arg.a >= function.params.size()) {
      return false;
    }
    if (store_slot.op != ir::Op::StoreInstanceSlot || store_slot.dst != load_self.dst ||
        store_slot.b != load_arg.dst) {
      return false;
    }
    spec.push_back(std::make_pair(store_slot.a, load_arg.a - 1));
    ip += 3;
  }
  if (spec.empty() || ip >= function.code.size()) {
    return false;
  }
  if (function.code[ip].op == ir::Op::ReturnConst) {
    return function.code[ip].a < function.constants.size() &&
        function.constants[function.code[ip].a].tag == ValueTag::None &&
        ip + 1 == function.code.size();
  }
  if (ip + 1 >= function.code.size()) return false;
  const auto& load_none = function.code[ip];
  const auto& ret = function.code[ip + 1];
  if (load_none.op != ir::Op::LoadConst || ret.op != ir::Op::Return || ret.a != load_none.dst) {
    return false;
  }
  return ip + 2 == function.code.size();
}

XLANG3_HOT_INLINE bool xlang_vm_execute_slot_constructor(
    Value klass,
    CallArgsView args,
    const XlangVMSlotConstructorSpec& spec,
    Value& out,
    std::string& error) {
  Value instance = Value::instance(std::move(klass));
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr) {
    error = "invalid constructed instance";
    return false;
  }
  for (const auto& item : spec) {
    const uint32_t slot = item.first;
    const uint32_t arg = item.second;
    if (slot >= instance_slot_count(instance_obj) || arg >= args.size()) {
      error = "invalid inline constructor slot";
      return false;
    }
    value_assign_fast(instance_slot_at(instance_obj, slot), args.get(arg));
  }
  value_assign_fast(out, instance);
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
  if (!spec.next_is_constant && spec.next_arg >= args.size()) {
    error = "invalid inline function arg";
    return false;
  }
  Value temp;
  value_assign_fast(temp, out);
  const Value& rhs = spec.next_is_constant ? spec.next_constant : args.get(spec.next_arg);
  return xlang_vm_execute_binary_op(spec.next_op, temp, rhs, out, error);
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
using SlotConstructorSpec = XlangVMSlotConstructorSpec;

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

inline bool analyze_slot_constructor(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    SlotConstructorSpec& spec) {
  return xlang_vm_analyze_slot_constructor(current_module, fn_obj, spec);
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

XLANG3_HOT_INLINE bool execute_slot_constructor(
    Value klass,
    CallArgsView args,
    const SlotConstructorSpec& spec,
    Value& out,
    std::string& error) {
  return xlang_vm_execute_slot_constructor(std::move(klass), args, spec, out, error);
}

} // namespace xlang3
