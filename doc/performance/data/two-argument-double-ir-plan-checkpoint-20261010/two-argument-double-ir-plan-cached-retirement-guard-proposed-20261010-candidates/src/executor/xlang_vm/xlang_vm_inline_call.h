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
#include "xlang3/builtins.h"
#include "xlang3/object_model.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace xlang3 {

XLANG3_HOT_INLINE bool xlang_vm_inline_python_function_allowed(
    Runtime& runtime,
    const ir::Module& current_module,
    const FunctionObject& function) {
  const auto active_hook = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };
  if (runtime.debug_step_active() || active_hook(runtime.trace_function()) ||
      active_hook(runtime.profile_function())) {
    return false;
  }
  const ir::Module* target_module = function.module != nullptr
      ? function.module.get() : &current_module;
  return !sys_monitoring_function_may_dispatch(target_module, function.function_id);
}

struct XlangVMSelfBinaryMethodSpec {
  uint32_t lhs_slot = 0;
  uint32_t rhs_slot = 0;
  ir::Op op = ir::Op::Add;
};

struct XlangVMSelfAttrBinaryMethodSpec {
  uint32_t lhs_name = 0;
  uint32_t rhs_name = 0;
  ir::Op op = ir::Op::Add;
};

// Recognize only the two short-circuit boolean expressions used by Richards.
// The call-site executor additionally guards all three attributes as exact
// bools, so it never skips user-defined truth conversion or changes the value
// returned by Python's `and`/`or` operators.
struct XlangVMSelfAttrBooleanExprMethodSpec {
  std::array<uint32_t, 3> names{};
  uint8_t expression = 0;
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

XLANG3_HOT_INLINE bool xlang_vm_arg_binary_inline_value_supported(const Value& value) {
  // The inline executor uses value-level arithmetic and intentionally avoids
  // runtime special-method dispatch. Restrict it to immediate built-in
  // scalars; instances, subclasses, and container objects must run the body
  // through the normal VM so __add__/__radd__ and overrides remain observable.
  return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double ||
      value.tag == ValueTag::Bool;
}

XLANG3_HOT_INLINE bool xlang_vm_arg_binary_inline_values_supported(
    CallArgsView args, const XlangVMArgBinaryFunctionSpec& spec) {
  if (spec.lhs_arg >= args.size() || spec.rhs_arg >= args.size() ||
      !xlang_vm_arg_binary_inline_value_supported(args.get(spec.lhs_arg)) ||
      !xlang_vm_arg_binary_inline_value_supported(args.get(spec.rhs_arg))) {
    return false;
  }
  if (!spec.has_next) return true;
  if (spec.next_is_constant) {
    return xlang_vm_arg_binary_inline_value_supported(spec.next_constant);
  }
  return spec.next_arg < args.size() &&
      xlang_vm_arg_binary_inline_value_supported(args.get(spec.next_arg));
}

XLANG3_HOT_INLINE bool xlang_vm_try_arg_binary_local_accumulate_int64(
    CallArgsView args,
    const XlangVMArgBinaryFunctionSpec& spec,
    const Value& accumulator,
    int64_t& function_value,
    int64_t& accumulated_value) {
  if (!spec.has_next || spec.op != ir::Op::Add || spec.next_op != ir::Op::Add ||
      accumulator.tag != ValueTag::Int64 ||
      spec.lhs_arg >= args.size() || spec.rhs_arg >= args.size() ||
      args.get(spec.lhs_arg).tag != ValueTag::Int64 ||
      args.get(spec.rhs_arg).tag != ValueTag::Int64) {
    return false;
  }
  const Value* next_value = nullptr;
  if (spec.next_is_constant) {
    next_value = &spec.next_constant;
  } else if (spec.next_arg < args.size()) {
    next_value = &args.get(spec.next_arg);
  }
  if (next_value == nullptr || next_value->tag != ValueTag::Int64) return false;
  int64_t intermediate = 0;
  return xlang_vm_checked_add_i64(
             args.get(spec.lhs_arg).as.i64, args.get(spec.rhs_arg).as.i64,
             intermediate) &&
      xlang_vm_checked_add_i64(intermediate, next_value->as.i64, function_value) &&
      xlang_vm_checked_add_i64(accumulator.as.i64, function_value, accumulated_value);
}

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

struct XlangVMClassMethodAttrIntCompareSpec {
  uint32_t attribute_name = 0;
  ir::CompareOp compare = ir::CompareOp::Lt;
};

inline bool xlang_vm_analyze_classmethod_attr_int_compare(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    XlangVMClassMethodAttrIntCompareSpec& spec) {
  const ir::Module* fn_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= fn_module->functions.size()) return false;
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.is_generator ||
      !xlang_vm_has_direct_positional_signature(function, 3) ||
      !function.free_vars.empty() || !function.cell_slots.empty() ||
      function.code.size() != 5) {
    return false;
  }
  const auto& lhs = function.code[0];
  const auto& rhs = function.code[1];
  const auto& compare = function.code[2];
  const auto& ret = function.code[3];
  const auto& implicit_return = function.code[4];
  if (lhs.op != ir::Op::LoadLocalAttr || lhs.a != 1 ||
      rhs.op != ir::Op::LoadLocalAttr || rhs.a != 2 ||
      lhs.b >= function.names.size() || rhs.b >= function.names.size() ||
      function.names[lhs.b] != function.names[rhs.b] ||
      compare.op != ir::Op::Compare || compare.a != lhs.dst ||
      compare.b != rhs.dst ||
      (compare.c != static_cast<uint32_t>(ir::CompareOp::Lt) &&
       compare.c != static_cast<uint32_t>(ir::CompareOp::Gt)) ||
      ret.op != ir::Op::Return || ret.a != compare.dst ||
      implicit_return.op != ir::Op::ReturnConst ||
      implicit_return.a >= function.constants.size() ||
      function.constants[implicit_return.a].tag != ValueTag::None) {
    return false;
  }
  spec.attribute_name = lhs.b;
  spec.compare = static_cast<ir::CompareOp>(compare.c);
  return true;
}

inline bool xlang_vm_prepare_classmethod_attr_int_compare(
    const FunctionObject& fn_obj,
    const ir::Module& current_module,
    const XlangVMClassMethodAttrIntCompareSpec& spec,
    const Value& receiver_class,
    CallArgsView args,
    uint32_t& lhs_slot,
    uint32_t& rhs_slot) {
  if (args.size() != 2 || args.has_keywords() || args.has_expansion()) return false;
  auto* expected_class = value_as_class(receiver_class);
  auto* lhs = value_as_instance(args.get(0));
  auto* rhs = value_as_instance(args.get(1));
  if (expected_class == nullptr || lhs == nullptr || rhs == nullptr ||
      value_as_class(lhs->klass) != expected_class ||
      value_as_class(rhs->klass) != expected_class ||
      expected_class->has_getattribute_hook || lhs->native_get_attr != nullptr ||
      rhs->native_get_attr != nullptr ||
      value_as_dict(instance_attribute_storage(*lhs)) != nullptr ||
      value_as_dict(instance_attribute_storage(*rhs)) != nullptr) {
    return false;
  }

  const ir::Module* fn_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= fn_module->functions.size()) return false;
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (spec.attribute_name >= function.names.size()) return false;
  const auto& attribute_name = function.names[spec.attribute_name];

  // A cached instance slot is safe only when class lookup would not give a
  // data descriptor precedence. The call-site class-version guard invalidates
  // this proof if the class or one of its bases later changes.
  Value descriptor;
  std::string lookup_error;
  if (object_lookup_class_attr(lhs->klass, attribute_name, descriptor, lookup_error) &&
      object_value_is_data_descriptor(descriptor)) {
    return false;
  }
  if (!lookup_error.empty()) return false;

  const auto lhs_attr = std::find_if(lhs->attrs.begin(), lhs->attrs.end(),
      [&](const auto& item) { return item.first == attribute_name; });
  const auto rhs_attr = std::find_if(rhs->attrs.begin(), rhs->attrs.end(),
      [&](const auto& item) { return item.first == attribute_name; });
  if (lhs_attr == lhs->attrs.end() || rhs_attr == rhs->attrs.end()) return false;
  lhs_slot = static_cast<uint32_t>(lhs_attr - lhs->attrs.begin());
  rhs_slot = static_cast<uint32_t>(rhs_attr - rhs->attrs.begin());
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_execute_classmethod_attr_int_compare(
    const FunctionObject& fn_obj,
    const ir::Module& current_module,
    const Value& receiver_class,
    CallArgsView args,
    uint32_t attribute_name_id,
    ir::CompareOp compare,
    uint32_t lhs_slot,
    uint32_t rhs_slot,
    Value& out) {
  if (args.size() != 2 || args.has_keywords() || args.has_expansion()) return false;
  auto* expected_class = value_as_class(receiver_class);
  auto* lhs = value_as_instance(args.get(0));
  auto* rhs = value_as_instance(args.get(1));
  if (expected_class == nullptr || lhs == nullptr || rhs == nullptr ||
      value_as_class(lhs->klass) != expected_class ||
      value_as_class(rhs->klass) != expected_class ||
      expected_class->has_getattribute_hook || lhs->native_get_attr != nullptr ||
      rhs->native_get_attr != nullptr ||
      value_as_dict(instance_attribute_storage(*lhs)) != nullptr ||
      value_as_dict(instance_attribute_storage(*rhs)) != nullptr) {
    return false;
  }
  const ir::Module* fn_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= fn_module->functions.size()) return false;
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (attribute_name_id >= function.names.size()) return false;
  const auto& attribute_name = function.names[attribute_name_id];
  if (lhs_slot >= lhs->attrs.size() || rhs_slot >= rhs->attrs.size() ||
      lhs->attrs[lhs_slot].first != attribute_name ||
      rhs->attrs[rhs_slot].first != attribute_name) {
    return false;
  }
  const auto& lhs_value = lhs->attrs[lhs_slot].second;
  const auto& rhs_value = rhs->attrs[rhs_slot].second;
  // Exact immediate ints cannot dispatch user comparison methods or raise.
  // Every other value kind takes the ordinary Python function-frame path.
  if (lhs_value.tag != ValueTag::Int64 || rhs_value.tag != ValueTag::Int64) return false;
  bool result = false;
  if (compare == ir::CompareOp::Lt) result = lhs_value.as.i64 < rhs_value.as.i64;
  else if (compare == ir::CompareOp::Gt) result = lhs_value.as.i64 > rhs_value.as.i64;
  else return false;
  value_assign_fast(out, Value::boolean(result));
  return true;
}

struct XlangVMTrivialFunctionSpec {
  bool returns_argument = false;
  uint32_t argument = 0;
  Value constant;
};

struct XlangVMCapturedItemFunctionSpec {
  uint32_t free_slot = 0;
  uint32_t argument = 0;
};

inline bool xlang_vm_analyze_captured_item_function(
    const ir::Module& module, const FunctionObject& object, uint32_t argc,
    XlangVMCapturedItemFunctionSpec& spec) {
  if (object.function_id >= module.functions.size()) return false;
  const auto& function = module.functions[object.function_id];
  if ((function.code.size() != 4 && function.code.size() != 5) ||
      function.code[0].op != ir::Op::LoadFree ||
      function.code[1].op != ir::Op::LoadLocal ||
      function.code[2].op != ir::Op::GetItem ||
      function.code[3].op != ir::Op::Return ||
      function.is_generator || function.is_async || function.is_coroutine ||
      !function.cell_slots.empty() ||
      !xlang_vm_has_direct_positional_signature(function, argc)) return false;
  const auto& receiver = function.code[0];
  const auto& argument = function.code[1];
  const auto& item = function.code[2];
  if (receiver.dst == argument.dst || item.a != receiver.dst ||
      item.b != argument.dst || function.code[3].a != item.dst ||
      receiver.a >= function.free_vars.size() || receiver.a >= object.closure.size() ||
      argument.a >= argc) return false;
  if (function.code.size() == 5) {
    const auto& tail = function.code[4];
    if (tail.op != ir::Op::ReturnConst || tail.a >= function.constants.size() ||
        function.constants[tail.a].tag != ValueTag::None) return false;
  }
  spec.free_slot = receiver.a;
  spec.argument = argument.a;
  return true;
}

using XlangVMSlotConstructorSpec = std::vector<std::pair<uint32_t, uint32_t>>;
constexpr uint32_t kXlangVMInlineConstructorAttrFlag = uint32_t{1} << 31;

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

// A bounded, uncached IR executor for a two-argument numeric method family.
// This removes a normal Python frame only after ALL lookup and operand guards
// have passed. Rejected inputs execute the original frame exactly once. It is
// deliberately not a library/name specialization or a partial call replay.
inline bool xlang_vm_try_two_argument_double_method(
    Runtime& runtime, const ir::Module& current_module,
    const FunctionObject& fn_obj, const Value& self, const Value& other,
    Value& out, size_t vm_frame_count) {
  const auto active_hook = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };

  // No persistent receiver/function owners or expanded CallSiteCache payload.
  // Current IR is selected on every attempt, so __code__ replacement cannot
  // leave a body plan referring to an earlier module or function generation.
  const ir::Module* method_module = fn_obj.module != nullptr ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= method_module->functions.size()) return false;
  const auto& method = method_module->functions[fn_obj.function_id];
  constexpr size_t limit = 32;
  if (method.is_generator || method.is_async || method.is_coroutine ||
      !method.free_vars.empty() || !method.cell_slots.empty() ||
      !xlang_vm_has_direct_positional_signature(method, 2) ||
      method.locals.size() != 2 || method.register_count > limit ||
      method.code.size() < 5 || method.code.size() > 24 ||
      method.code[0].op != ir::Op::CallLocalMethod) return false;
  const auto& guard_call = method.code[0];
  const auto& discard = method.code[1];
  const auto& ret = method.code[method.code.size() - 2];
  const auto& implicit_return = method.code.back();
  if (guard_call.op != ir::Op::CallLocalMethod || guard_call.a > 1 ||
      guard_call.dst >= method.register_count ||
      guard_call.b >= method.names.size() || guard_call.c >= method.call_args.size() ||
      !method.call_args[guard_call.c].empty() ||
      discard.op != ir::Op::Pop || discard.a != guard_call.dst ||
      ret.op != ir::Op::Return || ret.a >= method.register_count ||
      implicit_return.op != ir::Op::ReturnConst ||
      implicit_return.a >= method.constants.size() ||
      method.constants[implicit_return.a].tag != ValueTag::None) return false;
  // LoadLocalAttr writes its numeric destination AND an auxiliary receiver
  // register. Admit only fresh writes within the actual register count, and
  // keep receiver/call results nonnumeric so no reused auxiliary can masquerade
  // as an earlier scalar. This makes primitive preloading equivalent to SSA.
  std::array<bool, limit> written{};
  std::array<bool, limit> numeric{};
  written[guard_call.dst] = true;
  for (size_t ip = 2; ip + 2 < method.code.size(); ++ip) {
    const auto& op = method.code[ip];
    if (op.dst >= method.register_count || written[op.dst]) return false;
    if (op.op == ir::Op::LoadLocalAttr) {
      if (op.a > 1 || op.b >= method.names.size() ||
          op.c >= method.register_count || op.c == op.dst || written[op.c]) return false;
      written[op.c] = true;
    } else if (op.op == ir::Op::Add || op.op == ir::Op::Sub || op.op == ir::Op::Mul) {
      if (op.a >= method.register_count || op.b >= method.register_count ||
          !numeric[op.a] || !numeric[op.b] || op.c != 0) return false;
    } else return false;
    written[op.dst] = true;
    numeric[op.dst] = true;
  }
  if (!numeric[ret.a]) return false;
  if (runtime.debug_step_active() || active_hook(runtime.debug_hook()) ||
      active_hook(runtime.trace_function()) || active_hook(runtime.profile_function()) ||
      sys_monitoring_event_may_dispatch(kSysMonitoringEventAll) ||
      !xlang_vm_inline_python_function_allowed(runtime, current_module, fn_obj)) return false;
  // Preserve the ordinary push_frame budget for the method and its leading
  // Python guard. A default/unknown frame count never grants admission.
  // Subtraction keeps both saved-depth and headroom checks overflow-safe.
  const size_t effective_limit = std::min(
      static_cast<size_t>(runtime.recursion_limit()), size_t{1024});
  if (vm_frame_count == 0 || vm_frame_count >= effective_limit) return false;
  const size_t after_live_frames = effective_limit - vm_frame_count;
  const size_t saved_depth = runtime.saved_python_frame_depth();
  if (saved_depth >= after_live_frames || after_live_frames - saved_depth < 2) return false;
  const auto module_owner = fn_obj.module;
  // Constructor copies own borrowed inputs too. The helper's only output is a
  // fresh scalar; its caller publishes that scalar into the live destination.
  // All instance/class/IR access is finished before that overwrite may finalize.
  const Value inputs[2] = {Value(self), Value(other)};
  InstanceObject* instances[2] = {value_as_instance(inputs[0]), value_as_instance(inputs[1])};
  for (auto* instance : instances) {
    if (instance == nullptr || instance->native_get_attr != nullptr ||
        instance_slot_count(instance) != 0 ||
        value_as_dict(instance_attribute_storage(*instance)) != nullptr) return false;
    const auto* klass = value_as_class(instance->klass);
    if (klass == nullptr || klass->has_getattribute_hook) return false;
    // A frame-elided call cannot move observable input retirement. Reuse the
    // version-cached release-finalizer predicate; weakref roles need only an
    // atomic header check, without locking or consulting the weakref registry.
    if ((instance->header.gc_tracking_state.load(std::memory_order_relaxed) &
         kObjectWeakrefFlagsMask) != 0 ||
        class_has_release_finalizer(instance->klass)) return false;
  }
  auto* guarded_instance = instances[guard_call.a];
  const auto& guard_name = method.names[guard_call.b];
  for (const auto& attr : guarded_instance->attrs)
    if (attr.first == guard_name) return false;
  Value guarded_callable;
  std::string lookup_error;
  if (!object_lookup_class_attr(guarded_instance->klass, guard_name, guarded_callable, lookup_error) ||
      !lookup_error.empty()) return false;
  const auto* guard_function = value_as_function(guarded_callable);
  if (guard_function == nullptr) return false;
  const auto* guard_module = guard_function->module != nullptr
      ? guard_function->module.get() : &current_module;
  if (guard_function->function_id >= guard_module->functions.size() ||
      guard_module->functions[guard_function->function_id].is_async ||
      guard_module->functions[guard_function->function_id].is_coroutine) return false;
  XlangVMTrivialFunctionSpec guard_spec;
  if (
      !xlang_vm_inline_python_function_allowed(runtime, current_module, *guard_function) ||
      !xlang_vm_analyze_trivial_function(current_module, *guard_function, 1, guard_spec) ||
      !guard_spec.returns_argument || guard_spec.argument != 0) return false;
  // Restrict this prototype to missing class attributes plus present native
  // instance fields. Any property, slot or other descriptor takes the original
  // lookup path, before invoking __get__, __getattribute__ or a callable shadow.
  std::array<double, limit> values{};
  for (size_t ip = 2; ip + 2 < method.code.size(); ++ip) {
    const auto& op = method.code[ip];
    if (op.op != ir::Op::LoadLocalAttr) continue;
    auto* instance = instances[op.a];
    const auto& name = method.names[op.b];
    Value class_attribute;
    lookup_error.clear();
    if (object_lookup_class_attr(instance->klass, name, class_attribute, lookup_error) ||
        !lookup_error.empty()) return false;
    const auto found = std::find_if(instance->attrs.begin(), instance->attrs.end(),
        [&](const auto& attr) { return attr.first == name; });
    if (found == instance->attrs.end() || found->second.tag != ValueTag::Double) return false;
    values[op.dst] = found->second.as.f64;
  }
  // The leading call was proved to return its receiver without effects, and
  // its sole consumer discards it. All remaining operations are exact Double
  // arithmetic. Keep instruction order; no reassociation or fused multiply-add.
  for (size_t ip = 2; ip + 2 < method.code.size(); ++ip) {
    const auto& op = method.code[ip];
    if (op.op == ir::Op::Add) values[op.dst] = values[op.a] + values[op.b];
    else if (op.op == ir::Op::Sub) values[op.dst] = values[op.a] - values[op.b];
    else if (op.op == ir::Op::Mul) values[op.dst] = values[op.a] * values[op.b];
  }
  value_assign_fast(out, Value::number(values[ret.a]));
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

inline bool xlang_vm_analyze_self_attr_binary_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    XlangVMSelfAttrBinaryMethodSpec& spec) {
  const ir::Module* method_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= method_module->functions.size()) return false;
  const auto& method = method_module->functions[fn_obj.function_id];
  if (method.is_generator || method.params.size() != 1 ||
      !method.free_vars.empty() || !method.cell_slots.empty() || method.code.size() != 5) {
    return false;
  }
  const auto& load_lhs = method.code[0];
  const auto& load_rhs = method.code[1];
  const auto& binary = method.code[2];
  const auto& ret = method.code[3];
  const auto& implicit_return = method.code[4];
  if (load_lhs.op != ir::Op::LoadLocalAttr || load_lhs.a != 0 ||
      load_rhs.op != ir::Op::LoadLocalAttr || load_rhs.a != 0 ||
      load_lhs.b >= method.names.size() || load_rhs.b >= method.names.size() ||
      binary.op != ir::Op::Add ||
      binary.a != load_lhs.dst || binary.b != load_rhs.dst ||
      ret.op != ir::Op::Return || ret.a != binary.dst ||
      implicit_return.op != ir::Op::ReturnConst ||
      implicit_return.a >= method.constants.size() ||
      method.constants[implicit_return.a].tag != ValueTag::None) {
    return false;
  }
  spec.lhs_name = load_lhs.b;
  spec.rhs_name = load_rhs.b;
  spec.op = binary.op;
  return true;
}

inline bool xlang_vm_analyze_self_attr_boolean_expr_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    XlangVMSelfAttrBooleanExprMethodSpec& spec) {
  const ir::Module* method_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= method_module->functions.size()) return false;
  const auto& method = method_module->functions[fn_obj.function_id];
  if (method.is_generator || method.params.size() != 1 ||
      !method.free_vars.empty() || !method.cell_slots.empty() ||
      method.code.size() != 10 || method.register_count > 10) return false;
  const auto& code = method.code;
  const auto is_attr = [&](size_t index, uint32_t dst) {
    return code[index].op == ir::Op::LoadLocalAttr && code[index].a == 0 &&
        code[index].dst == dst && code[index].b < method.names.size();
  };
  const auto is_none_return = [&]() {
    return code[8].op == ir::Op::Return && code[9].op == ir::Op::ReturnConst &&
        code[9].a < method.constants.size() &&
        method.constants[code[9].a].tag == ValueTag::None;
  };
  // task_holding or (not packet_pending and task_waiting)
  if (is_attr(0, 0) && is_attr(2, 3) && is_attr(5, 7) && is_none_return() &&
      code[1].op == ir::Op::MoveJumpIfTrue && code[1].dst == 2 &&
      code[1].a == 0 && code[1].b == 8 &&
      code[3].op == ir::Op::Not && code[3].dst == 5 && code[3].a == 3 &&
      code[4].op == ir::Op::MoveJumpIfFalse && code[4].dst == 6 &&
      code[4].a == 5 && code[4].b == 7 &&
      code[6].op == ir::Op::Move && code[6].dst == 6 && code[6].a == 7 &&
      code[7].op == ir::Op::Move && code[7].dst == 2 && code[7].a == 6 &&
      code[8].a == 2) {
    spec.names = {code[0].b, code[2].b, code[5].b};
    spec.expression = 1;
    return true;
  }
  // packet_pending and task_waiting and not task_holding
  if (is_attr(0, 0) && is_attr(2, 3) && is_attr(5, 6) && is_none_return() &&
      code[1].op == ir::Op::MoveJumpIfFalse && code[1].dst == 2 &&
      code[1].a == 0 && code[1].b == 4 &&
      code[3].op == ir::Op::Move && code[3].dst == 2 && code[3].a == 3 &&
      code[4].op == ir::Op::MoveJumpIfFalse && code[4].dst == 5 &&
      code[4].a == 2 && code[4].b == 8 &&
      code[6].op == ir::Op::Not && code[6].dst == 8 && code[6].a == 6 &&
      code[7].op == ir::Op::Move && code[7].dst == 5 && code[7].a == 8 &&
      code[8].a == 5) {
    spec.names = {code[0].b, code[2].b, code[5].b};
    spec.expression = 2;
    return true;
  }
  return false;
}

inline bool xlang_vm_prepare_self_attr_boolean_expr_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    InstanceObject& instance,
    const XlangVMSelfAttrBooleanExprMethodSpec& spec,
    std::array<uint32_t, 3>& slots) {
  auto* klass = value_as_class(instance.klass);
  if (klass == nullptr || klass->has_getattribute_hook ||
      value_as_dict(instance_attribute_storage(instance)) != nullptr) return false;
  const ir::Module* method_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= method_module->functions.size()) return false;
  const auto& method = method_module->functions[fn_obj.function_id];
  for (size_t operand = 0; operand < spec.names.size(); ++operand) {
    if (spec.names[operand] >= method.names.size()) return false;
    const auto& name = method.names[spec.names[operand]];
    Value descriptor;
    std::string lookup_error;
    if (object_lookup_class_attr(instance.klass, name, descriptor, lookup_error) &&
        object_value_is_data_descriptor(descriptor)) return false;
    if (!lookup_error.empty()) return false;
    const auto found = std::find_if(instance.attrs.begin(), instance.attrs.end(),
        [&](const auto& attr) { return attr.first == name; });
    if (found == instance.attrs.end()) return false;
    slots[operand] = static_cast<uint32_t>(found - instance.attrs.begin());
  }
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_execute_self_attr_boolean_expr_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    InstanceObject& instance,
    const std::array<uint32_t, 3>& slots,
    uint8_t expression,
    Value& out) {
  auto* klass = value_as_class(instance.klass);
  if (klass == nullptr || klass->has_getattribute_hook ||
      value_as_dict(instance_attribute_storage(instance)) != nullptr) return false;
  const ir::Module* method_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= method_module->functions.size()) return false;
  const auto& method = method_module->functions[fn_obj.function_id];
  const Value* operands[3]{};
  if (method.code.size() != 10 || method.code[0].b >= method.names.size() ||
      method.code[2].b >= method.names.size() || method.code[5].b >= method.names.size()) return false;
  const uint32_t name_ids[3] = {method.code[0].b, method.code[2].b, method.code[5].b};
  for (size_t i = 0; i < 3; ++i) {
    if (slots[i] >= instance.attrs.size() ||
        instance.attrs[slots[i]].first != method.names[name_ids[i]]) return false;
    operands[i] = &instance.attrs[slots[i]].second;
  }
  // Exact bools make short-circuit `and`/`or` observationally equivalent to
  // this two-expression specialization. Any reassigned/custom value falls
  // through to the ordinary Python frame, which performs __bool__ as needed.
  if (operands[0]->tag != ValueTag::Bool || operands[1]->tag != ValueTag::Bool ||
      operands[2]->tag != ValueTag::Bool) return false;
  const bool first = operands[0]->as.b;
  const bool second = operands[1]->as.b;
  const bool third = operands[2]->as.b;
  if (expression == 1) value_assign_fast(out, Value::boolean(first || (!second && third)));
  else if (expression == 2) value_assign_fast(out, Value::boolean(first && second && !third));
  else return false;
  return true;
}

inline bool xlang_vm_prepare_self_attr_binary_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    InstanceObject& instance,
    const XlangVMSelfAttrBinaryMethodSpec& spec,
    uint32_t& lhs_attr,
    uint32_t& rhs_attr) {
  auto* klass = value_as_class(instance.klass);
  if (klass == nullptr || klass->has_getattribute_hook ||
      value_as_dict(instance_attribute_storage(instance)) != nullptr) {
    return false;
  }
  const ir::Module* method_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= method_module->functions.size()) return false;
  const auto& method = method_module->functions[fn_obj.function_id];
  if (spec.lhs_name >= method.names.size() || spec.rhs_name >= method.names.size()) return false;

  auto find_plain_instance_attr = [&](const std::string& name, uint32_t& index) {
    Value descriptor;
    std::string lookup_error;
    if (object_lookup_class_attr(instance.klass, name, descriptor, lookup_error) &&
        object_value_is_data_descriptor(descriptor)) {
      return false;
    }
    if (!lookup_error.empty()) return false;
    const auto found = std::find_if(
        instance.attrs.begin(), instance.attrs.end(),
        [&](const auto& attr) { return attr.first == name; });
    if (found == instance.attrs.end()) return false;
    index = static_cast<uint32_t>(found - instance.attrs.begin());
    return true;
  };
  return find_plain_instance_attr(method.names[spec.lhs_name], lhs_attr) &&
      find_plain_instance_attr(method.names[spec.rhs_name], rhs_attr);
}

XLANG3_HOT_INLINE bool xlang_vm_execute_self_attr_binary_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    InstanceObject& instance,
    uint32_t lhs_attr,
    uint32_t rhs_attr,
    ir::Op op,
    Value& out,
    std::string& error) {
  auto* klass = value_as_class(instance.klass);
  if (klass == nullptr || klass->has_getattribute_hook ||
      value_as_dict(instance_attribute_storage(instance)) != nullptr) {
    return false;
  }
  const ir::Module* method_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= method_module->functions.size()) return false;
  const auto& method = method_module->functions[fn_obj.function_id];
  if (method.code.size() != 5 || method.code[0].b >= method.names.size() ||
      method.code[1].b >= method.names.size() ||
      lhs_attr >= instance.attrs.size() || rhs_attr >= instance.attrs.size() ||
      instance.attrs[lhs_attr].first != method.names[method.code[0].b] ||
      instance.attrs[rhs_attr].first != method.names[method.code[1].b]) {
    return false;
  }
  const Value& lhs = instance.attrs[lhs_attr].second;
  const Value& rhs = instance.attrs[rhs_attr].second;
  const auto immediate_numeric = [](const Value& value) {
    return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double;
  };
  if (!immediate_numeric(lhs) || !immediate_numeric(rhs)) return false;
  // Exact immediate operands keep this cached body equivalent to normal Add;
  // objects and subclasses retain their __add__/__radd__ calls in the VM frame.
  return xlang_vm_execute_binary_op(op, lhs, rhs, out, error);
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
    // Match the post-LoadLocalPair form too: lowering folds the first two
    // argument loads, so requiring a plain LoadLocal sequence below silently
    // disables this two-op inline for functions such as lambda a,b,c: a+b+c.
    // Keep every operand/destination check here; this shortcut must only run
    // when the complete body is exactly the supported binary-op pattern.
    if (function.code.size() >= 5 && function.code[0].a < argc && function.code[0].c < argc &&
        xlang_vm_is_inline_binary_op(function.code[1].op) &&
        function.code[1].a == function.code[0].dst && function.code[1].b == function.code[0].b &&
        function.code[2].op == ir::Op::LoadLocal && function.code[2].a < argc &&
        xlang_vm_is_inline_binary_op(function.code[3].op) &&
        function.code[3].a == function.code[1].dst && function.code[3].b == function.code[2].dst &&
        function.code[4].op == ir::Op::Return && function.code[4].a == function.code[3].dst) {
      spec.lhs_arg = function.code[0].a;
      spec.rhs_arg = function.code[0].c;
      spec.op = function.code[1].op;
      spec.next_arg = function.code[2].a;
      spec.next_op = function.code[3].op;
      spec.has_next = true;
      spec.next_is_constant = false;
      return true;
    }
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
  // Recognize only paired local reads followed by plain instance stores.
  // Keeping the complete-body check below leaves descriptors and side effects
  // in the ordinary initializer frame.
  while (ip + 1 < function.code.size()) {
    const auto& load_pair = function.code[ip];
    const auto& store_attr = function.code[ip + 1];
    if (load_pair.op != ir::Op::LoadLocalPair || load_pair.a != 0 ||
        load_pair.c == 0 || load_pair.c >= function.params.size() ||
        store_attr.op != ir::Op::StoreAttr || store_attr.dst != load_pair.dst ||
        store_attr.b != load_pair.b || store_attr.a >= function.names.size()) {
      break;
    }
    const auto& name = function.names[store_attr.a];
    if (name == "__class__" || name == "__dict__" || name == "__weakref__" || name == "__name__") {
      return false;
    }
    // The empty-instance constructor path below appends fields directly. Keep
    // it limited to unique names so repeated stores retain overwrite behavior.
    if (std::any_of(spec.begin(), spec.end(), [&](const auto& item) {
          if ((item.first & kXlangVMInlineConstructorAttrFlag) == 0) return false;
          const uint32_t prior_name = item.first & ~kXlangVMInlineConstructorAttrFlag;
          return prior_name < function.names.size() && function.names[prior_name] == name;
        })) {
      return false;
    }
    spec.emplace_back(kXlangVMInlineConstructorAttrFlag | store_attr.a, load_pair.c - 1);
    ip += 2;
  }
  if (!spec.empty()) {
    return function.code.size() == ip + 1 &&
        function.code[ip].op == ir::Op::ReturnConst &&
        function.code[ip].a < function.constants.size() &&
        function.constants[function.code[ip].a].tag == ValueTag::None;
  }
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

struct XlangVMFloatPointConstructorSpec {
  uint32_t sin_global_index = UINT32_MAX;
  uint32_t cos_global_index = UINT32_MAX;
  std::array<uint32_t, 3> name_indices{};
};

inline bool xlang_vm_analyze_float_point_constructor(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    XlangVMFloatPointConstructorSpec& spec) {
  const ir::Module* function_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= function_module->functions.size()) return false;
  const auto& function = function_module->functions[fn_obj.function_id];
  if (function.name != "__init__" || function.is_generator ||
      function.params.size() != 2 || function.free_vars.size() != 0 ||
      function.cell_slots.size() != 0 || function.code.size() != 19 ||
      function.locals.size() != 3 || function.names.size() != 3 ||
      function.names[0] != "x" || function.names[1] != "y" ||
      function.names[2] != "z" || function.locals[1] != "i" ||
      function.locals[2] != "x" || function.constants.size() != 3 ||
      function.constants[0].tag != ValueTag::Int64 ||
      function.constants[0].as.i64 != 3 ||
      function.constants[1].tag != ValueTag::Int64 ||
      function.constants[1].as.i64 != 2 ||
      function.constants[2].tag != ValueTag::None ||
      function.call_args.size() != 2) {
    return false;
  }
  const auto& code = function.code;
  const auto is = [&](size_t ip, ir::Op op, uint32_t dst, uint32_t a,
                      uint32_t b, uint32_t c) {
    const auto& instr = code[ip];
    return instr.op == op && instr.dst == dst && instr.a == a &&
        instr.b == b && instr.c == c;
  };
  if (code[0].op != ir::Op::LoadModuleSlot || code[0].dst != 0 ||
      code[0].a >= function_module->global_slots.size() ||
      function_module->global_slots[code[0].a] != "sin" ||
      !is(1, ir::Op::LoadLocal, 1, 1, 0, 0) ||
      !is(2, ir::Op::Call, 2, 0, 0, 0) ||
      function.call_args[0].size() != 1 || function.call_args[0][0] != 1 ||
      !is(3, ir::Op::StoreLocalLoadLocal, 2, 2, 3, 0) ||
      !is(4, ir::Op::StoreAttr, 3, 0, 2, 0) ||
      !is(5, ir::Op::LoadLocal, 4, 0, 0, 0) ||
      code[6].op != ir::Op::LoadModuleSlot || code[6].dst != 5 ||
      code[6].a >= function_module->global_slots.size() ||
      function_module->global_slots[code[6].a] != "cos" ||
      !is(7, ir::Op::LoadLocal, 6, 1, 0, 0) ||
      !is(8, ir::Op::Call, 7, 5, 1, 0) ||
      function.call_args[1].size() != 1 || function.call_args[1][0] != 6 ||
      !is(9, ir::Op::LoadConst, 8, 0, 0, 0) ||
      !is(10, ir::Op::Mul, 9, 7, 8, 0) ||
      !is(11, ir::Op::StoreAttr, 4, 1, 9, 0) ||
      !is(12, ir::Op::LoadLocalPair, 10, 0, 11, 2) ||
      !is(13, ir::Op::LoadLocal, 12, 2, 0, 0) ||
      !is(14, ir::Op::Mul, 13, 11, 12, 0) ||
      !is(15, ir::Op::LoadConst, 14, 1, 0, 0) ||
      !is(16, ir::Op::Div, 15, 13, 14, 0) ||
      !is(17, ir::Op::StoreAttr, 10, 2, 15, 0) ||
      !is(18, ir::Op::ReturnConst, 0, 2, 0, 0)) {
    return false;
  }
  spec.sin_global_index = code[0].a;
  spec.cos_global_index = code[6].a;
  spec.name_indices = {0, 1, 2};
  return true;
}

inline bool xlang_vm_prepare_float_point_constructor(
    Runtime& runtime,
    const ir::Module& current_module,
    const Value& klass_value,
    const FunctionObject& fn_obj,
    const XlangVMFloatPointConstructorSpec& spec,
    std::array<uint32_t, 3>& slots,
    ModuleObject*& globals_module,
    Value& expected_sin,
    Value& expected_cos) {
  auto* klass = value_as_class(klass_value);
  globals_module = value_as_module(fn_obj.globals_module);
  if (klass == nullptr || globals_module == nullptr ||
      klass->has_getattribute_hook || klass->has_getattr_hook ||
      klass->has_setattr_hook || klass->has_delattr_hook) {
    return false;
  }
  const ir::Module* function_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= function_module->functions.size() ||
      spec.sin_global_index >= function_module->global_slots.size() ||
      spec.cos_global_index >= function_module->global_slots.size() ||
      function_module->global_slots[spec.sin_global_index] != "sin" ||
      function_module->global_slots[spec.cos_global_index] != "cos") {
    return false;
  }
  Value bound_sin;
  Value bound_cos;
  Value math_module;
  Value canonical_sin;
  Value canonical_cos;
  std::string ignored;
  if (!module_get_attr(fn_obj.globals_module, "sin", bound_sin, ignored) ||
      !module_get_attr(fn_obj.globals_module, "cos", bound_cos, ignored) ||
      !runtime.import_module("math", math_module, ignored) ||
      !module_get_attr(math_module, "sin", canonical_sin, ignored) ||
      !module_get_attr(math_module, "cos", canonical_cos, ignored) ||
      value_as_native_function(bound_sin) == nullptr ||
      value_as_native_function(bound_cos) == nullptr ||
      bound_sin.as.obj != canonical_sin.as.obj ||
      bound_cos.as.obj != canonical_cos.as.obj) {
    return false;
  }
  const auto& function = function_module->functions[fn_obj.function_id];
  for (size_t index = 0; index < spec.name_indices.size(); ++index) {
    const auto& name = function.names[spec.name_indices[index]];
    const auto slot = klass->instance_slot_indices.find(name);
    const auto descriptor = klass->attrs.find(name);
    if (slot == klass->instance_slot_indices.end() ||
        descriptor == klass->attrs.end()) {
      return false;
    }
    const auto* slot_descriptor = value_as_slot_descriptor(descriptor->second);
    if (slot_descriptor == nullptr || slot_descriptor->index != slot->second) {
      return false;
    }
    slots[index] = slot->second;
  }
  value_assign_fast(expected_sin, bound_sin);
  value_assign_fast(expected_cos, bound_cos);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_execute_float_point_constructor(
    Value& instance,
    const Value& klass_value,
    const FunctionObject& fn_obj,
    CallArgsView args,
    const std::array<uint32_t, 3>& slots,
    ModuleObject* expected_globals,
    uint64_t& expected_globals_version,
    const Value& expected_sin,
    const Value& expected_cos,
    Value& out) {
  auto* object = value_as_instance(instance);
  auto* klass = value_as_class(klass_value);
  auto* globals = value_as_module(fn_obj.globals_module);
  if (object == nullptr || klass == nullptr || globals == nullptr ||
      globals != expected_globals || object->klass.as.obj != klass_value.as.obj ||
      args.size() != 1 || args.has_keywords() || args.has_expansion() ||
      (args.get(0).tag != ValueTag::Int64 &&
       args.get(0).tag != ValueTag::Double)) {
    return false;
  }
  if (globals->version != expected_globals_version) {
    Value current_sin;
    Value current_cos;
    std::string ignored;
    if (!module_get_attr(fn_obj.globals_module, "sin", current_sin, ignored) ||
        !module_get_attr(fn_obj.globals_module, "cos", current_cos, ignored) ||
        current_sin.tag != ValueTag::Object ||
        current_cos.tag != ValueTag::Object ||
        current_sin.as.obj != expected_sin.as.obj ||
        current_cos.as.obj != expected_cos.as.obj) {
      return false;
    }
    expected_globals_version = globals->version;
  }
  const double input = args.get(0).tag == ValueTag::Int64
      ? static_cast<double>(args.get(0).as.i64)
      : args.get(0).as.f64;
  if (std::isinf(input)) return false;
  const double x = std::sin(input);
  const double y = std::cos(input) * 3.0;
  const double z = (x * x) / 2.0;
  for (size_t index = 0; index < slots.size(); ++index) {
    if (slots[index] >= instance_slot_count(object)) return false;
  }
  instance_slot_at(object, slots[0]) = Value::number(x);
  instance_slot_at(object, slots[1]) = Value::number(y);
  instance_slot_at(object, slots[2]) = Value::number(z);
  value_move_assign_fast(out, instance);
  return true;
}

inline bool xlang_vm_slot_constructor_attrs_safe(
    const ir::Module& current_module,
    const Value& klass_value,
    const FunctionObject& fn_obj,
    const XlangVMSlotConstructorSpec& spec) {
  auto* klass = value_as_class(klass_value);
  if (klass == nullptr || klass->has_setattr_hook ||
      (klass->restrict_instance_attrs && !klass->allow_instance_dict) ||
      class_has_builtin_base_name(klass, "dict") ||
      class_has_builtin_base_name(klass, "OrderedDict") ||
      class_has_builtin_base_name(klass, "defaultdict") ||
      class_has_builtin_base_name(klass, "BaseException")) {
    return false;
  }
  const ir::Module* function_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= function_module->functions.size()) return false;
  const auto& function = function_module->functions[fn_obj.function_id];
  for (const auto& item : spec) {
    if ((item.first & kXlangVMInlineConstructorAttrFlag) == 0) continue;
    const uint32_t name_index = item.first & ~kXlangVMInlineConstructorAttrFlag;
    if (name_index >= function.names.size()) return false;
    const auto& name = function.names[name_index];
    if (klass->instance_slot_indices.find(name) != klass->instance_slot_indices.end()) return false;
    Value descriptor;
    std::string ignored;
    if (object_lookup_class_attr(klass_value, name, descriptor, ignored) &&
        object_value_is_data_descriptor(descriptor)) {
      return false;
    }
  }
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_execute_slot_constructor(
    Value& instance,
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    CallArgsView args,
    const XlangVMSlotConstructorSpec& spec,
    Value& out,
    std::string& error) {
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr) {
    error = "invalid constructed instance";
    return false;
  }
  const ir::Module* function_module = fn_obj.module != nullptr
      ? fn_obj.module.get() : &current_module;
  if (fn_obj.function_id >= function_module->functions.size()) return false;
  const auto& function = function_module->functions[fn_obj.function_id];
  if (args.has_keywords() || args.has_expansion() || function.params.empty() ||
      args.size() + 1 != function.params.size()) return false;
  bool dynamic_attrs = !spec.empty() &&
      (spec.front().first & kXlangVMInlineConstructorAttrFlag) != 0;
  if (dynamic_attrs) {
    for (const auto& item : spec) {
      const uint32_t name_index = item.first & ~kXlangVMInlineConstructorAttrFlag;
      if (item.second >= args.size() || name_index >= function.names.size()) {
        error = "invalid inline constructor attribute";
        return false;
      }
    }
    if (instance_obj->attrs.empty()) {
      // Fresh ordinary instances start with no dynamic fields. Unique names
      // let this hot path append in source order without a linear name search;
      // prepopulated instances keep the full overwrite-aware path below.
      for (const auto& item : spec) {
        const uint32_t name_index = item.first & ~kXlangVMInlineConstructorAttrFlag;
        instance_obj->attrs.emplace_back(function.names[name_index], args.get(item.second));
      }
      // The caller returns immediately on constructor success, so transfer its
      // fresh instance to the result instead of paying an atomic retain/release
      // pair for a copy that has no remaining owner use.
      value_move_assign_fast(out, instance);
      return true;
    }
  }
  for (const auto& item : spec) {
    const uint32_t arg = item.second;
    if (arg >= args.size()) {
      error = "invalid inline constructor slot";
      return false;
    }
    if ((item.first & kXlangVMInlineConstructorAttrFlag) != 0) {
      const uint32_t name_index = item.first & ~kXlangVMInlineConstructorAttrFlag;
      if (name_index >= function.names.size()) {
        error = "invalid inline constructor attribute";
        return false;
      }
      const auto& name = function.names[name_index];
      auto existing = std::find_if(instance_obj->attrs.begin(), instance_obj->attrs.end(),
          [&](const auto& attr) { return attr.first == name; });
      if (existing != instance_obj->attrs.end()) {
        value_assign_fast(existing->second, args.get(arg));
      } else {
        instance_obj->attrs.emplace_back(name, args.get(arg));
      }
    } else {
      const uint32_t slot = item.first;
      if (slot >= instance_slot_count(instance_obj)) {
        error = "invalid inline constructor slot";
        return false;
      }
      value_assign_fast(instance_slot_at(instance_obj, slot), args.get(arg));
    }
  }
  // As above, successful construction consumes the caller's temporary.
  value_move_assign_fast(out, instance);
  return true;
}

// Explicit __slots__ methods retain dynamic stores in IR. Resolve only a
// complete plain initializer into canonical physical slots; deleting the
// restricted-layout guard alone would append fields outside slot storage.
// Keep this cold proof out of the shared Call dispatch's hot native body.
XLANG3_NOINLINE inline bool xlang_vm_prepare_own_canonical_slot_constructor(
    Runtime& runtime, const ir::Module& current_module,
    const Value& class_value, const FunctionObject& function_object,
    const XlangVMSlotConstructorSpec& names,
    XlangVMSlotConstructorSpec& slots) {
  auto* klass = value_as_class(class_value);
  const Value* object_value = runtime.find_builtin("object");
  const Value* type_value = runtime.find_builtin("type");
  const auto* object_class = object_value == nullptr ? nullptr : value_as_class(*object_value);
  const auto* type_class = type_value == nullptr ? nullptr : value_as_class(*type_value);
  if (klass == nullptr || object_class == nullptr || type_class == nullptr ||
      value_as_class(klass->metaclass) != type_class ||
      !klass->restrict_instance_attrs || !klass->own_instance_slot_declarations_known ||
      klass->has_setattr_hook || klass->native_type_constructor != nullptr ||
      klass->attrs.find("__setattr__") != klass->attrs.end() ||
      klass->attrs.find("__new__") != klass->attrs.end() || names.empty()) return false;
  const auto own_init = klass->attrs.find("__init__");
  if (own_init == klass->attrs.end() ||
      value_as_function(own_init->second) != &function_object) return false;
  const ir::Module* function_module = function_object.module != nullptr
      ? function_object.module.get() : &current_module;
  if (function_object.function_id >= function_module->functions.size()) return false;
  const auto& function = function_module->functions[function_object.function_id];
  if (function.is_generator || function.is_async || function.is_coroutine ||
      function.params.size() < 2) return false;
  const std::vector<Value>* mro = nullptr;
  std::string error;
  if (!class_get_mro_values(klass, mro, error) ||
      mro == nullptr || mro->size() != 2 || value_as_class((*mro)[0]) != klass ||
      value_as_class((*mro)[1]) != object_class) return false;
  XlangVMSlotConstructorSpec prepared;
  prepared.reserve(names.size());
  for (const auto& item : names) {
    if ((item.first & kXlangVMInlineConstructorAttrFlag) == 0 ||
        item.second >= function.params.size() - 1) return false;
    const uint32_t name_index = item.first & ~kXlangVMInlineConstructorAttrFlag;
    if (name_index >= function.names.size()) return false;
    const auto& name = function.names[name_index];
    const auto declaration_count = std::count(klass->own_instance_slot_declarations.begin(),
        klass->own_instance_slot_declarations.end(), name);
    const auto index = klass->instance_slot_indices.find(name);
    const auto attr = klass->attrs.find(name);
    const auto* descriptor = attr == klass->attrs.end()
        ? nullptr : value_as_slot_descriptor(attr->second);
    if (declaration_count != 1 || index == klass->instance_slot_indices.end() ||
        index->second >= klass->instance_slot_names.size() ||
        (index->second & kXlangVMInlineConstructorAttrFlag) != 0 ||
        klass->instance_slot_names[index->second] != name || descriptor == nullptr ||
        descriptor->name != name || value_as_class(descriptor->owner_class) != klass ||
        descriptor->index != index->second ||
        object_class->instance_slot_indices.find(name) != object_class->instance_slot_indices.end()) return false;
    const auto base_attr = object_class->attrs.find(name);
    if (base_attr != object_class->attrs.end() && value_as_slot_descriptor(base_attr->second) != nullptr)
      return false;
    // Repeated stores could release an earlier argument and run its finalizer
    // before construction is published. Each physical slot must be written once
    // so the completed result retains every explicit argument without callbacks.
    if (std::any_of(prepared.begin(), prepared.end(),
            [&](const auto& prior) { return prior.first == index->second; })) return false;
    prepared.emplace_back(index->second, item.second);
  }
  // Retain every explicit argument in the resulting object, so omitting an
  // initializer frame cannot move an unused argument's finalizer boundary.
  for (uint32_t argument = 0; argument + 1 < function.params.size(); ++argument)
    if (std::none_of(prepared.begin(), prepared.end(),
            [&](const auto& item) { return item.second == argument; })) return false;
  slots = std::move(prepared);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_execute_own_canonical_slot_constructor(
    Value& instance, const ir::Module& current_module,
    const FunctionObject& function_object, CallArgsView args,
    const XlangVMSlotConstructorSpec& slots, Value& out, std::string& error) {
  const auto* object = value_as_instance(instance);
  if (object == nullptr || object->native_data != nullptr ||
      object->native_get_attr != nullptr || object->native_set_attr != nullptr ||
      object->native_delete_attr != nullptr || !object->attrs.empty() ||
      object->mapping_storage.tag != ValueTag::Invalid ||
      object->sequence_storage.tag != ValueTag::Invalid) return false;
  const ir::Module* function_module = function_object.module != nullptr
      ? function_object.module.get() : &current_module;
  if (function_object.function_id >= function_module->functions.size()) return false;
  const auto& function = function_module->functions[function_object.function_id];
  if (args.has_keywords() || args.has_expansion() ||
      args.size() + 1 != function.params.size()) return false;
  for (uint32_t argument = 0; argument < args.size(); ++argument)
    if (args.get(argument).tag == ValueTag::Invalid) return false;
  // Validate the whole plan before the first store. Fresh Invalid slots cannot
  // retire Python owners or invoke hooks; unsupported state has no side effect.
  for (const auto& item : slots)
    if ((item.first & kXlangVMInlineConstructorAttrFlag) != 0 ||
        item.second >= args.size() || item.first >= instance_slot_count(object) ||
        instance_slot_at(object, item.first).tag != ValueTag::Invalid) return false;
  return xlang_vm_execute_slot_constructor(
      instance, current_module, function_object, args, slots, out, error);
}

// Publish only after the ordinary constructor selection and full own-slot
// proof have succeeded. Absence of these entries is a stable negative under
// class/base/metaclass generations; an empty mutable marker/abstract container
// is not such a proof and stays on the existing generic selection path.
XLANG3_NOINLINE inline void xlang_vm_remember_class_canonical_slot_constructor(
    Runtime& runtime, const Value& class_value,
    const FunctionObject& initializer, const XlangVMSlotConstructorSpec& slots) {
  auto* klass = value_as_class(class_value);
  const Value* object_value = runtime.find_builtin("object");
  const Value* type_value = runtime.find_builtin("type");
  auto* object_class = object_value == nullptr ? nullptr : value_as_class(*object_value);
  auto* metaclass = type_value == nullptr ? nullptr : value_as_class(*type_value);
  if (klass == nullptr || object_class == nullptr || metaclass == nullptr ||
      value_as_class(klass->metaclass) != metaclass ||
      klass->native_type_constructor != nullptr || initializer.module == nullptr ||
      object_class->name != "object" || metaclass->name != "type" || slots.empty()) return;
  const std::vector<Value>* type_mro = nullptr;
  const std::vector<Value>* object_mro = nullptr;
  std::string error;
  if (!class_get_mro_values(metaclass, type_mro, error) || type_mro == nullptr ||
      type_mro->size() != 2 || value_as_class((*type_mro)[0]) != metaclass ||
      value_as_class((*type_mro)[1]) != object_class ||
      !class_get_mro_values(object_class, object_mro, error) || object_mro == nullptr ||
      object_mro->size() != 1 || value_as_class((*object_mro)[0]) != object_class) return;
  for (auto* candidate : {klass, object_class, metaclass}) {
    if (candidate->attrs.find("__abstractmethods__") != candidate->attrs.end() ||
        candidate->attrs.find("__xlang3_enum_marker__") != candidate->attrs.end()) return;
  }
  const auto default_new = object_class->attrs.find("__new__");
  const auto* native_new = default_new == object_class->attrs.end()
      ? nullptr : value_as_native_function(default_new->second);
  // Value::class_object wraps builtin __new__ in an exact StaticMethod.
  // Inspect that stored target directly: generic descriptors must not execute
  // during publication, and arbitrary wrapped Python functions still decline.
  if (native_new == nullptr && default_new != object_class->attrs.end()) {
    const auto* static_new = value_as_static_method(default_new->second);
    if (static_new != nullptr) native_new = value_as_native_function(static_new->function);
  }
  if (native_new == nullptr || native_new->name != "object.__new__") return;
  auto cache = std::make_unique<ClassCanonicalSlotConstructorCache>();
  cache->class_version = klass->version;
  cache->metaclass_version = metaclass->version;
  cache->object_version = object_class->version;
  cache->initializer_code_version = initializer.code_version;
  cache->metaclass = metaclass;
  cache->object_class = object_class;
  cache->initializer = const_cast<FunctionObject*>(&initializer);
  cache->slots = slots;
  // No owning Values or Python cleanup is retired here. Output/argument owners
  // remain with the caller until after this complete captured proof is visible.
  klass->canonical_slot_constructor_cache = std::move(cache);
}

XLANG3_HOT_INLINE bool xlang_vm_try_class_canonical_slot_constructor(
    Runtime& runtime, const ir::Module& current_module,
    const Value& class_value, CallArgsView args, Value& initializer_owner, Value& completed) {
  auto* klass = value_as_class(class_value);
  const auto* cache = klass == nullptr ? nullptr : klass->canonical_slot_constructor_cache.get();
  if (cache == nullptr || cache->class_version != klass->version ||
      klass->native_type_constructor != nullptr || args.has_keywords() ||
      args.has_expansion()) return false;
  const Value* type_value = runtime.find_builtin("type");
  const Value* object_value = runtime.find_builtin("object");
  auto* metaclass = value_as_class(klass->metaclass);
  auto* object_class = object_value == nullptr ? nullptr : value_as_class(*object_value);
  // Runtime builtin identity also prevents using a class proof in a different
  // Runtime whose object/type identities differ. Both live owner generations
  // precede the weak function read; SDK native attachment needs its own check.
  if (metaclass == nullptr || type_value == nullptr ||
      value_as_class(*type_value) != metaclass || cache->metaclass != metaclass ||
      cache->metaclass_version != metaclass->version ||
      object_class == nullptr || cache->object_class != object_class ||
      cache->object_version != object_class->version || cache->initializer == nullptr) return false;
  // The isolated-class collector can invalidate cyclic attribute Values while
  // holding the class alive. Prove the current owning entry as well as its
  // generation before dereferencing a weak function, even during such cleanup.
  const auto own_init = klass->attrs.find("__init__");
  if (own_init == klass->attrs.end() ||
      value_as_function(own_init->second) != cache->initializer) return false;
  const auto* initializer = cache->initializer;
  if (cache->initializer_code_version != initializer->code_version ||
      initializer->module == nullptr ||
      initializer->function_id >= initializer->module->functions.size()) return false;
  const auto& function = initializer->module->functions[initializer->function_id];
  // Decline malformed/unsupported arguments before allocating an instance:
  // speculative allocation could otherwise run an extra __del__ on fallback.
  if (args.size() + 1 != function.params.size()) return false;
  for (uint32_t argument = 0; argument < args.size(); ++argument)
    if (args.get(argument).tag == ValueTag::Invalid) return false;
  // The caller keeps this owner through argument retirement and old-output
  // finalizers, matching the existing same-activation canonical constructor.
  initializer_owner.tag = ValueTag::Object;
  initializer_owner.as.obj = const_cast<Object*>(&initializer->header);
  retain(initializer_owner);
  // Fresh instance creation and the fully validated physical stores have no
  // user callback. Preserve the existing instance native-hook/storage guards.
  Value instance = Value::instance(class_value);
  std::string error;
  return xlang_vm_execute_own_canonical_slot_constructor(
      instance, current_module, *initializer, args, cache->slots, completed, error);
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
  if (!spec.has_next) {
    return xlang_vm_execute_binary_op(
        spec.op, args.get(spec.lhs_arg), args.get(spec.rhs_arg), out, error);
  }
  if (!spec.next_is_constant && spec.next_arg >= args.size()) {
    error = "invalid inline function arg";
    return false;
  }
  const Value& next_rhs = spec.next_is_constant ? spec.next_constant : args.get(spec.next_arg);
  if (spec.op == ir::Op::Add && spec.next_op == ir::Op::Add) {
    // For the common two-add body, keep the intermediate in a temporary so
    // we avoid copying the first result out and back into a second temporary.
    // If either exact numeric operation cannot stay immediate (for example,
    // int64 overflow), fall through to the full generic path below so big-int
    // promotion and Python special-method behavior remain unchanged.
    Value intermediate;
    if (fast_add(args.get(spec.lhs_arg), args.get(spec.rhs_arg), intermediate) &&
        fast_add(intermediate, next_rhs, out)) {
      return true;
    }
    // Recompute through the normal operators after a fast-path miss; this is
    // rare and keeps overflow and custom operand semantics authoritative.
    if (!xlang_vm_execute_binary_op(spec.op, args.get(spec.lhs_arg), args.get(spec.rhs_arg), out, error)) {
      return false;
    }
    Value temp;
    value_assign_fast(temp, out);
    return xlang_vm_execute_binary_op(spec.next_op, temp, next_rhs, out, error);
  }
  if (!xlang_vm_execute_binary_op(spec.op, args.get(spec.lhs_arg), args.get(spec.rhs_arg), out, error)) {
    return false;
  }
  Value temp;
  value_assign_fast(temp, out);
  return xlang_vm_execute_binary_op(spec.next_op, temp, next_rhs, out, error);
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
    Value& instance,
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    CallArgsView args,
    const SlotConstructorSpec& spec,
    Value& out,
    std::string& error) {
  return xlang_vm_execute_slot_constructor(instance, current_module, fn_obj, args, spec, out, error);
}

} // namespace xlang3
