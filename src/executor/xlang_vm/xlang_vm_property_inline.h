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

#include "xlang_frame.h"
#include "xlang_vm_arithmetic.h"

#include "xlang3/compiler.h"
#include "xlang3/object_model.h"

#include <string>

namespace xlang3 {

struct InlinePropertyAccess {
  uint32_t slot = 0;
  ir::Op op = ir::Op::Add;
  Value constant;
  bool has_const = false;
};

XLANG3_HOT_INLINE bool module_for_function(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    const ir::Module*& out) {
  out = fn_obj.module != nullptr ? fn_obj.module.get() : &current_module;
  return fn_obj.function_id < out->functions.size();
}

XLANG3_HOT_INLINE bool analyze_property_getter(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    InlinePropertyAccess& spec) {
  const ir::Module* fn_module = nullptr;
  if (!module_for_function(current_module, fn_obj, fn_module)) {
    return false;
  }
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.params.size() != 1 || function.free_vars.size() != 0 || function.cell_slots.size() != 0 ||
      (function.code.size() != 3 && function.code.size() != 5)) {
    return false;
  }
  const auto& load_self = function.code[0];
  const auto& load_slot = function.code[1];
  if (load_self.op != ir::Op::LoadLocal || load_self.a != 0 ||
      load_slot.op != ir::Op::LoadInstanceSlot || load_slot.a != load_self.dst) {
    return false;
  }
  spec.slot = load_slot.b;
  const auto& maybe_return = function.code[2];
  if (function.code.size() == 3 && maybe_return.op == ir::Op::Return && maybe_return.a == load_slot.dst) {
    spec.has_const = false;
    return true;
  }
  if (function.code.size() != 5) {
    return false;
  }
  const auto& load_const = function.code[2];
  const auto& binary = function.code[3];
  const auto& ret = function.code[4];
  if (load_const.op != ir::Op::LoadConst || load_const.a >= function.constants.size() ||
      ret.op != ir::Op::Return || ret.a != binary.dst ||
      binary.a != load_slot.dst || binary.b != load_const.dst ||
      (binary.op != ir::Op::Add && binary.op != ir::Op::Sub)) {
    return false;
  }
  spec.op = binary.op;
  value_assign_fast(spec.constant, function.constants[load_const.a]);
  spec.has_const = true;
  return true;
}

XLANG3_HOT_INLINE bool analyze_property_setter(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    InlinePropertyAccess& spec) {
  const ir::Module* fn_module = nullptr;
  if (!module_for_function(current_module, fn_obj, fn_module)) {
    return false;
  }
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.params.size() != 2 || function.free_vars.size() != 0 || function.cell_slots.size() != 0 ||
      function.code.size() < 4) {
    return false;
  }
  const auto& load_self = function.code[0];
  const auto& load_arg = function.code[1];
  if (load_self.op != ir::Op::LoadLocal || load_self.a != 0 || load_arg.op != ir::Op::LoadLocal || load_arg.a != 1) {
    return false;
  }
  size_t store_index = 2;
  uint32_t value_reg = load_arg.dst;
  if (function.code[2].op == ir::Op::LoadConst) {
    if (function.code.size() < 6 || function.code[2].a >= function.constants.size()) {
      return false;
    }
    const auto& binary = function.code[3];
    if ((binary.op != ir::Op::Add && binary.op != ir::Op::Sub) ||
        binary.a != load_arg.dst || binary.b != function.code[2].dst) {
      return false;
    }
    spec.op = binary.op;
    value_assign_fast(spec.constant, function.constants[function.code[2].a]);
    spec.has_const = true;
    value_reg = binary.dst;
    store_index = 4;
  } else {
    spec.has_const = false;
  }
  const auto& store = function.code[store_index];
  if (store.op != ir::Op::StoreInstanceSlot || store.dst != load_self.dst || store.b != value_reg) {
    return false;
  }
  spec.slot = store.a;
  return true;
}

XLANG3_HOT_INLINE bool analyze_property_deleter(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    InlinePropertyAccess& spec) {
  const ir::Module* fn_module = nullptr;
  if (!module_for_function(current_module, fn_obj, fn_module)) {
    return false;
  }
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.params.size() != 1 || function.code.size() < 3) {
    return false;
  }
  const auto& load_self = function.code[0];
  const auto& load_value = function.code[1];
  const auto& store = function.code[2];
  if (load_self.op != ir::Op::LoadLocal || load_self.a != 0 ||
      load_value.op != ir::Op::LoadConst || load_value.a >= function.constants.size() ||
      store.op != ir::Op::StoreInstanceSlot || store.dst != load_self.dst || store.b != load_value.dst) {
    return false;
  }
  spec.slot = store.a;
  value_assign_fast(spec.constant, function.constants[load_value.a]);
  return true;
}

XLANG3_HOT_INLINE bool execute_inline_property_getter(
    const InstanceObject& instance,
    const AttrSiteCache& cache,
    Value& out,
    std::string& error) {
  if (cache.getter_slot >= instance_slot_count(&instance)) {
    error = "invalid property slot";
    return false;
  }
  const auto& slot_value = instance_slot_at(&instance, cache.getter_slot);
  if (slot_value.tag == ValueTag::Invalid) {
    error = "object has no attribute";
    return false;
  }
  if (!cache.getter_has_const) {
    value_assign_fast(out, slot_value);
    return true;
  }
  return xlang_vm_execute_binary_op(cache.getter_op, slot_value, cache.getter_const, out, error);
}

XLANG3_HOT_INLINE bool execute_inline_property_getter_spec(
    const InstanceObject& instance,
    const InlinePropertyAccess& spec,
    Value& out,
    std::string& error) {
  if (spec.slot >= instance_slot_count(&instance)) {
    error = "invalid property slot";
    return false;
  }
  const auto& slot_value = instance_slot_at(&instance, spec.slot);
  if (slot_value.tag == ValueTag::Invalid) {
    error = "object has no attribute";
    return false;
  }
  if (!spec.has_const) {
    value_assign_fast(out, slot_value);
    return true;
  }
  return xlang_vm_execute_binary_op(spec.op, slot_value, spec.constant, out, error);
}

XLANG3_HOT_INLINE bool execute_inline_property_setter(
    InstanceObject& instance,
    const AttrSiteCache& cache,
    const Value& value,
    std::string& error) {
  if (cache.setter_slot >= instance_slot_count(&instance)) {
    error = "invalid property slot";
    return false;
  }
  if (!cache.setter_has_const) {
    value_assign_fast(instance_slot_at(&instance, cache.setter_slot), value);
    return true;
  }
  Value computed;
  if (!xlang_vm_execute_binary_op(cache.setter_op, value, cache.setter_const, computed, error)) {
    return false;
  }
  value_assign_fast(instance_slot_at(&instance, cache.setter_slot), computed);
  return true;
}

XLANG3_HOT_INLINE bool execute_inline_property_deleter(
    InstanceObject& instance,
    const AttrSiteCache& cache,
    std::string& error) {
  if (cache.deleter_slot >= instance_slot_count(&instance)) {
    error = "invalid property slot";
    return false;
  }
  value_assign_fast(instance_slot_at(&instance, cache.deleter_slot), cache.deleter_const);
  return true;
}

} // namespace xlang3
