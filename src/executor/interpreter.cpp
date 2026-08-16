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
#include "xlang3/interpreter.h"

#include "xlang3/attribute.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cstddef>
#include <new>
#include <sstream>

namespace xlang3 {

namespace {

std::string compare_name(ir::CompareOp op) {
  switch (op) {
    case ir::CompareOp::Eq: return "==";
    case ir::CompareOp::Ne: return "!=";
    case ir::CompareOp::Lt: return "<";
    case ir::CompareOp::Le: return "<=";
    case ir::CompareOp::Gt: return ">";
    case ir::CompareOp::Ge: return ">=";
  }
  return "?";
}

XLANG3_HOT_INLINE bool value_is_number(const Value& value) {
  return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double;
}

XLANG3_HOT_INLINE double value_to_double_fast(const Value& value) {
  return value.tag == ValueTag::Int64 ? static_cast<double>(value.as.i64) : value.as.f64;
}

XLANG3_HOT_INLINE bool fast_add(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 + rhs.as.i64);
    return true;
  }
  if (value_is_number(lhs) && value_is_number(rhs)) {
    value_set_number(out, value_to_double_fast(lhs) + value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool fast_sub(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 - rhs.as.i64);
    return true;
  }
  if (value_is_number(lhs) && value_is_number(rhs)) {
    value_set_number(out, value_to_double_fast(lhs) - value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool fast_mul(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 * rhs.as.i64);
    return true;
  }
  if (value_is_number(lhs) && value_is_number(rhs)) {
    value_set_number(out, value_to_double_fast(lhs) * value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool fast_div(const Value& lhs, const Value& rhs, Value& out, bool& divide_by_zero) {
  divide_by_zero = false;
  if (!value_is_number(lhs) || !value_is_number(rhs)) {
    return false;
  }
  const double divisor = value_to_double_fast(rhs);
  if (divisor == 0.0) {
    divide_by_zero = true;
    return false;
  }
  value_set_number(out, value_to_double_fast(lhs) / divisor);
  return true;
}

XLANG3_HOT_INLINE bool fast_mod(const Value& lhs, const Value& rhs, Value& out, bool& modulo_by_zero) {
  modulo_by_zero = false;
  if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
    return false;
  }
  if (rhs.as.i64 == 0) {
    modulo_by_zero = true;
    return false;
  }
  int64_t result = lhs.as.i64 % rhs.as.i64;
  if (result != 0 && ((result < 0) != (rhs.as.i64 < 0))) {
    result += rhs.as.i64;
  }
  value_set_int64(out, result);
  return true;
}

XLANG3_HOT_INLINE bool fast_compare(ir::CompareOp op, const Value& lhs, const Value& rhs, Value& out) {
  if (!value_is_number(lhs) || !value_is_number(rhs)) {
    return false;
  }
  const double a = value_to_double_fast(lhs);
  const double b = value_to_double_fast(rhs);
  bool compare_result = false;
  switch (op) {
    case ir::CompareOp::Eq: compare_result = a == b; break;
    case ir::CompareOp::Ne: compare_result = a != b; break;
    case ir::CompareOp::Lt: compare_result = a < b; break;
    case ir::CompareOp::Le: compare_result = a <= b; break;
    case ir::CompareOp::Gt: compare_result = a > b; break;
    case ir::CompareOp::Ge: compare_result = a >= b; break;
  }
  value_set_bool(out, compare_result);
  return true;
}

class SmallValueBuffer {
public:
  SmallValueBuffer() = default;

  SmallValueBuffer(size_t size, const Value& fill) : size_(size) {
    if (size_ <= kInlineCount) {
      data_ = inline_data();
      uses_inline_ = true;
      for (size_t i = 0; i < size_; ++i) {
        new (data_ + i) Value(fill);
      }
      return;
    }
    heap_.assign(size_, fill);
    data_ = heap_.data();
  }

  SmallValueBuffer(const SmallValueBuffer&) = delete;
  SmallValueBuffer& operator=(const SmallValueBuffer&) = delete;
  SmallValueBuffer(SmallValueBuffer&& other) noexcept {
    move_from(std::move(other));
  }
  SmallValueBuffer& operator=(SmallValueBuffer&& other) noexcept = delete;

  ~SmallValueBuffer() {
    destroy_inline();
  }

  void reset(size_t size, const Value& fill) {
    if (size <= kInlineCount) {
      if (!uses_inline_) {
        heap_.clear();
        data_ = inline_data();
        uses_inline_ = true;
        for (size_t i = 0; i < size; ++i) {
          new (data_ + i) Value(fill);
        }
        size_ = size;
        return;
      }
      const size_t shared = size < size_ ? size : size_;
      for (size_t i = 0; i < shared; ++i) {
        value_assign_fast(data_[i], fill);
      }
      for (size_t i = shared; i < size; ++i) {
        new (data_ + i) Value(fill);
      }
      for (size_t i = size; i < size_; ++i) {
        data_[i].~Value();
      }
      size_ = size;
      return;
    }

    destroy_inline();
    uses_inline_ = false;
    heap_.assign(size, fill);
    data_ = heap_.data();
    size_ = size;
  }

  XLANG3_HOT_INLINE size_t size() const {
    return size_;
  }

  XLANG3_HOT_INLINE Value* data() {
    return data_;
  }

  XLANG3_HOT_INLINE const Value* data() const {
    return data_;
  }

  XLANG3_HOT_INLINE Value& operator[](size_t index) {
    return data_[index];
  }

  XLANG3_HOT_INLINE const Value& operator[](size_t index) const {
    return data_[index];
  }

private:
  static constexpr size_t kInlineCount = 64;

  XLANG3_HOT_INLINE Value* inline_data() {
    return reinterpret_cast<Value*>(inline_storage_);
  }

  void move_from(SmallValueBuffer&& other) {
    size_ = other.size_;
    uses_inline_ = other.uses_inline_;
    if (other.uses_inline_) {
      data_ = inline_data();
      for (size_t i = 0; i < size_; ++i) {
        new (data_ + i) Value(std::move(other.data_[i]));
        other.data_[i].~Value();
      }
      other.size_ = 0;
      other.data_ = nullptr;
      other.uses_inline_ = false;
      return;
    }

    heap_ = std::move(other.heap_);
    data_ = heap_.data();
    other.size_ = 0;
    other.data_ = nullptr;
  }

  void destroy_inline() {
    if (!uses_inline_) {
      return;
    }
    for (size_t i = 0; i < size_; ++i) {
      data_[i].~Value();
    }
    size_ = 0;
    data_ = nullptr;
    uses_inline_ = false;
  }

  size_t size_ = 0;
  Value* data_ = nullptr;
  bool uses_inline_ = false;
  alignas(Value) std::byte inline_storage_[sizeof(Value) * kInlineCount];
  std::vector<Value> heap_;
};

enum class CallSiteKind : uint8_t {
  Empty,
  UserFunction,
  NativeFunction,
  UserConstructor,
  NativeConstructor,
  InlineSelfBinaryMethod,
  InlineArgBinaryFunction,
};

enum class AttrSiteKind : uint8_t {
  Empty,
  InstanceAttr,
  InstanceSlot,
};

struct CallSiteCache {
  Object* callee_object = nullptr;
  CallSiteKind kind = CallSiteKind::Empty;
  FunctionObject* function = nullptr;
  NativeFunctionObject* native = nullptr;
  uint64_t class_version = 0;
  uint32_t lhs_slot = 0;
  uint32_t rhs_slot = 0;
  ir::Op inline_op = ir::Op::Add;
  uint32_t inline_function_id = 0;
  uint32_t next_arg = 0;
  ir::Op next_op = ir::Op::Add;
  bool has_next = false;
};

struct AttrSiteCache {
  uint32_t index = 0;
  AttrSiteKind kind = AttrSiteKind::Empty;
};

enum class FrameReturnMode : uint8_t {
  StoreReturnValue,
  StoreConstructedInstance,
};

enum class ExceptionHandlerKind : uint8_t {
  Except,
  With,
};

struct ExceptionHandler {
  uint32_t ip = 0;
  ExceptionHandlerKind kind = ExceptionHandlerKind::Except;
  uint32_t manager_reg = 0;
};

struct VMUnwind {};

struct VMFrame {
  const ir::Module* module = nullptr;
  const ir::Function* fn = nullptr;
  const std::vector<Value>* closure = nullptr;
  Value globals_module;
  std::shared_ptr<const ir::Module> module_owner;
  uint32_t return_dst = 0;
  bool has_caller = false;
  FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue;
  Value continuation_value;
  size_t ip = 0;

  SmallValueBuffer locals;
  SmallValueBuffer cells;
  SmallValueBuffer regs;

  std::vector<ExceptionHandler> exception_handlers;
  std::vector<Value> global_value_cache;
  std::vector<uint32_t> global_slot_cache;
  std::vector<uint64_t> global_cache_versions;
  std::vector<uint8_t> global_cache_kind;
  std::vector<CallSiteCache> call_site_cache;
  std::vector<AttrSiteCache> attr_site_cache;
  std::vector<Value> native_call_args;

  VMFrame(
      const ir::Module& frame_module,
      uint32_t function_id,
      CallArgsView args,
      const std::vector<Value>& frame_closure,
      Value frame_globals_module,
      std::shared_ptr<const ir::Module> frame_module_owner,
      uint32_t frame_return_dst,
      bool frame_has_caller,
      FrameReturnMode frame_return_mode = FrameReturnMode::StoreReturnValue,
      Value frame_continuation_value = Value::invalid())
      : module(&frame_module),
        fn(&frame_module.functions[function_id]),
        closure(&frame_closure),
        globals_module(std::move(frame_globals_module)),
        module_owner(std::move(frame_module_owner)),
        return_dst(frame_return_dst),
        has_caller(frame_has_caller),
        return_mode(frame_return_mode),
        continuation_value(std::move(frame_continuation_value)),
        locals(fn->locals.size(), Value::none()),
        cells(fn->cell_slots.size(), Value::invalid()),
        regs(fn->register_count, Value::invalid()),
        global_value_cache(fn->names.size(), Value::invalid()),
        global_slot_cache(fn->names.size(), 0),
        global_cache_versions(fn->names.size(), 0),
        global_cache_kind(fn->names.size(), 0),
        attr_site_cache(fn->code.size()) {
    for (size_t i = 0; i < args.size(); ++i) {
      value_assign_fast(locals[i], args.get(i));
    }
    uint32_t max_call_arg_count = 0;
    for (const auto& arg_regs : fn->call_args) {
      if (arg_regs.size() > max_call_arg_count) {
        max_call_arg_count = static_cast<uint32_t>(arg_regs.size());
      }
    }
    if (max_call_arg_count != 0) {
      native_call_args.reserve(static_cast<size_t>(max_call_arg_count) + 1);
      call_site_cache.resize(fn->code.size());
    }
  }

  void reset(
      const ir::Module& frame_module,
      uint32_t function_id,
      CallArgsView args,
      const std::vector<Value>& frame_closure,
      Value frame_globals_module,
      std::shared_ptr<const ir::Module> frame_module_owner,
      uint32_t frame_return_dst,
      bool frame_has_caller,
      FrameReturnMode frame_return_mode = FrameReturnMode::StoreReturnValue,
      Value frame_continuation_value = Value::invalid()) {
    const ir::Function* old_fn = fn;
    module = &frame_module;
    fn = &frame_module.functions[function_id];
    closure = &frame_closure;
    globals_module = std::move(frame_globals_module);
    module_owner = std::move(frame_module_owner);
    return_dst = frame_return_dst;
    has_caller = frame_has_caller;
    return_mode = frame_return_mode;
    continuation_value = std::move(frame_continuation_value);
    ip = 0;

    locals.reset(fn->locals.size(), Value::none());
    cells.reset(fn->cell_slots.size(), Value::invalid());
    regs.reset(fn->register_count, Value::invalid());
    exception_handlers.clear();
    native_call_args.clear();

    if (old_fn != fn) {
      global_value_cache.assign(fn->names.size(), Value::invalid());
      global_slot_cache.assign(fn->names.size(), 0);
      global_cache_versions.assign(fn->names.size(), 0);
      global_cache_kind.assign(fn->names.size(), 0);
      call_site_cache.clear();
      attr_site_cache.assign(fn->code.size(), {});
      uint32_t max_call_arg_count = 0;
      for (const auto& arg_regs : fn->call_args) {
        if (arg_regs.size() > max_call_arg_count) {
          max_call_arg_count = static_cast<uint32_t>(arg_regs.size());
        }
      }
      if (max_call_arg_count != 0) {
        native_call_args.reserve(static_cast<size_t>(max_call_arg_count) + 1);
        call_site_cache.resize(fn->code.size());
      }
    }

    for (size_t i = 0; i < args.size(); ++i) {
      value_assign_fast(locals[i], args.get(i));
    }
  }
};

XLANG3_NOINLINE bool load_attr_cached(
    const Value& object,
    const std::string& name,
    AttrSiteCache& cache,
    Value& out,
    std::string& error) {
  if (auto* instance = value_as_instance(object)) {
    if (cache.kind == AttrSiteKind::InstanceSlot && cache.index < instance_slot_count(instance)) {
      const auto& slot_value = instance_slot_at(instance, cache.index);
      if (slot_value.tag != ValueTag::Invalid) {
        value_assign_fast(out, slot_value);
        return true;
      }
      error = "object has no attribute '" + name + "'";
      return false;
    }
    if (cache.kind == AttrSiteKind::InstanceAttr && cache.index < instance->attrs.size() &&
        instance->attrs[cache.index].first == name) {
      value_assign_fast(out, instance->attrs[cache.index].second);
      return true;
    }
    if (auto* klass = value_as_class(instance->klass)) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
        const auto& slot_value = instance_slot_at(instance, slot_it->second);
        if (slot_value.tag == ValueTag::Invalid) {
          error = "object has no attribute '" + name + "'";
          return false;
        }
        cache.index = slot_it->second;
        cache.kind = AttrSiteKind::InstanceSlot;
        value_assign_fast(out, slot_value);
        return true;
      }
    }
    for (size_t attr_i = 0; attr_i < instance->attrs.size(); ++attr_i) {
      if (instance->attrs[attr_i].first == name) {
        cache.index = static_cast<uint32_t>(attr_i);
        cache.kind = AttrSiteKind::InstanceAttr;
        value_assign_fast(out, instance->attrs[attr_i].second);
        return true;
      }
    }
  }
  return attribute_get(object, name, out, error);
}

XLANG3_NOINLINE bool store_attr_cached(
    Value& object,
    const std::string& name,
    const Value& value,
    AttrSiteCache& cache,
    std::string& error) {
  if (auto* instance = value_as_instance(object)) {
    if (cache.kind == AttrSiteKind::InstanceSlot && cache.index < instance_slot_count(instance)) {
      value_assign_fast(instance_slot_at(instance, cache.index), value);
      return true;
    }
    if (cache.kind == AttrSiteKind::InstanceAttr && cache.index < instance->attrs.size() &&
        instance->attrs[cache.index].first == name) {
      value_assign_fast(instance->attrs[cache.index].second, value);
      return true;
    }
    if (auto* klass = value_as_class(instance->klass)) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
        cache.index = slot_it->second;
        cache.kind = AttrSiteKind::InstanceSlot;
        value_assign_fast(instance_slot_at(instance, slot_it->second), value);
        return true;
      }
    }
    for (size_t attr_i = 0; attr_i < instance->attrs.size(); ++attr_i) {
      if (instance->attrs[attr_i].first == name) {
        cache.index = static_cast<uint32_t>(attr_i);
        cache.kind = AttrSiteKind::InstanceAttr;
        value_assign_fast(instance->attrs[attr_i].second, value);
        return true;
      }
    }
    instance->attrs.push_back(std::make_pair(name, value));
    cache.index = static_cast<uint32_t>(instance->attrs.size() - 1);
    cache.kind = AttrSiteKind::InstanceAttr;
    return true;
  }
  return attribute_set(object, name, value, error);
}

struct SelfBinaryMethodSpec {
  uint32_t lhs_slot = 0;
  uint32_t rhs_slot = 0;
  ir::Op op = ir::Op::Add;
};

struct ArgBinaryFunctionSpec {
  uint32_t lhs_arg = 0;
  uint32_t rhs_arg = 0;
  ir::Op op = ir::Op::Add;
  uint32_t next_arg = 0;
  ir::Op next_op = ir::Op::Add;
  bool has_next = false;
};

bool is_inline_binary_op(ir::Op op) {
  return op == ir::Op::Add || op == ir::Op::Sub || op == ir::Op::Mul || op == ir::Op::Div || op == ir::Op::Mod;
}

bool execute_binary_op(ir::Op op, const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  switch (op) {
    case ir::Op::Add:
      if (fast_add(lhs, rhs, out)) return true;
      return value_add(lhs, rhs, out, error);
    case ir::Op::Sub:
      if (fast_sub(lhs, rhs, out)) return true;
      return value_sub(lhs, rhs, out, error);
    case ir::Op::Mul:
      if (fast_mul(lhs, rhs, out)) return true;
      return value_mul(lhs, rhs, out, error);
    case ir::Op::Div: {
      bool divide_by_zero = false;
      if (fast_div(lhs, rhs, out, divide_by_zero)) return true;
      if (divide_by_zero) {
        error = "division by zero";
        return false;
      }
      return value_div(lhs, rhs, out, error);
    }
    case ir::Op::Mod: {
      bool modulo_by_zero = false;
      if (fast_mod(lhs, rhs, out, modulo_by_zero)) return true;
      if (modulo_by_zero) {
        error = "integer modulo by zero";
        return false;
      }
      return value_mod(lhs, rhs, out, error);
    }
    default:
      error = "unsupported inline function operation";
      return false;
  }
}

bool analyze_self_binary_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    SelfBinaryMethodSpec& spec) {
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
  if (!is_inline_binary_op(binary.op)) {
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

bool analyze_arg_binary_function(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    uint32_t argc,
    ArgBinaryFunctionSpec& spec) {
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
      load_lhs.a >= argc || load_rhs.a >= argc || !is_inline_binary_op(binary.op) ||
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
      !is_inline_binary_op(next_binary.op) ||
      next_binary.a != binary.dst || next_binary.b != load_next.dst ||
      next_ret.op != ir::Op::Return || next_ret.a != next_binary.dst) {
    return false;
  }
  spec.next_arg = load_next.a;
  spec.next_op = next_binary.op;
  spec.has_next = true;
  return true;
}

bool execute_arg_binary_function(
    CallArgsView args,
    const ArgBinaryFunctionSpec& spec,
    Value& out,
    std::string& error) {
  if (spec.lhs_arg >= args.size() || spec.rhs_arg >= args.size()) {
    error = "invalid inline function arg";
    return false;
  }
  if (!execute_binary_op(spec.op, args.get(spec.lhs_arg), args.get(spec.rhs_arg), out, error)) {
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
  return execute_binary_op(spec.next_op, temp, args.get(spec.next_arg), out, error);
}

XLANG3_HOT_INLINE bool execute_self_binary_method(
    const InstanceObject& instance,
    const SelfBinaryMethodSpec& spec,
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
      return execute_binary_op(spec.op, lhs, rhs, out, error);
    default:
      error = "unsupported inline method operation";
      return false;
  }
}

} // namespace

Interpreter::Interpreter(Runtime& runtime) : runtime_(runtime) {}

RuntimeResult Interpreter::run(const ir::Module& module) {
  static const std::vector<Value> empty_closure;
  return run_function(module, module.entry, {}, empty_closure, Value::invalid(), nullptr);
}

RuntimeResult Interpreter::run_module(const ir::Module& module, Value globals_module) {
  return run_module(module, std::move(globals_module), nullptr);
}

RuntimeResult Interpreter::run_module(
    const ir::Module& module,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner) {
  static const std::vector<Value> empty_closure;
  return run_function(module, module.entry, {}, empty_closure, std::move(globals_module), std::move(module_owner));
}

RuntimeResult Interpreter::run_function_value(FunctionObject* function, CallArgsView args) {
  RuntimeResult result;
  if (function == nullptr || function->module == nullptr) {
    result.errors.push_back("function has no module");
    return result;
  }
  return run_function(
      *function->module,
      function->function_id,
      args,
      function->closure,
      function->globals_module,
      function->module);
}

RuntimeResult Interpreter::run_function(
    const ir::Module& module,
    uint32_t function_id,
    CallArgsView args,
    const std::vector<Value>& fn_obj_closure,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner) {
  RuntimeResult result;
  if (function_id >= module.functions.size()) {
    result.errors.push_back("invalid function id");
    return result;
  }
  const auto& fn = module.functions[function_id];
  if (args.size() != fn.params.size()) {
    result.errors.push_back("function '" + fn.name + "' expected " + std::to_string(fn.params.size()) +
                            " arguments, got " + std::to_string(args.size()));
    return result;
  }

  std::vector<VMFrame> frames;
  frames.reserve(64);
  frames.emplace_back(
      module, function_id, args, fn_obj_closure, std::move(globals_module), std::move(module_owner), 0, false);
  size_t frame_count = 1;

  auto push_frame = [&](const ir::Module& call_module,
                        uint32_t call_function_id,
                        CallArgsView call_args,
                        const std::vector<Value>& closure,
                        Value call_globals_module,
                        std::shared_ptr<const ir::Module> call_module_owner,
                        uint32_t return_dst,
                        FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue,
                        Value continuation_value = Value::invalid()) -> bool {
    if (call_function_id >= call_module.functions.size()) {
      result.errors.push_back("invalid function id");
      return false;
    }
    const auto& call_fn = call_module.functions[call_function_id];
    if (call_args.size() != call_fn.params.size()) {
      result.errors.push_back("function '" + call_fn.name + "' expected " + std::to_string(call_fn.params.size()) +
                              " arguments, got " + std::to_string(call_args.size()));
      return false;
    }
    if (frame_count < frames.size()) {
      frames[frame_count].reset(call_module, call_function_id, call_args, closure, std::move(call_globals_module),
                                std::move(call_module_owner), return_dst, true, return_mode,
                                std::move(continuation_value));
    } else {
      frames.emplace_back(call_module, call_function_id, call_args, closure, std::move(call_globals_module),
                          std::move(call_module_owner), return_dst, true, return_mode,
                          std::move(continuation_value));
    }
    auto& pushed = frames[frame_count];
    ++frame_count;
    for (size_t i = 0; i < pushed.fn->cell_slots.size(); ++i) {
      if (pushed.fn->cell_slots[i] >= pushed.locals.size()) {
        result.errors.push_back("invalid cell local slot");
        --frame_count;
        return false;
      }
      pushed.cells[i] = Value::cell(pushed.locals[pushed.fn->cell_slots[i]]);
    }
    return true;
  };

  auto finish_frame = [&](const Value& return_value) -> bool {
    VMFrame& finished = frames[frame_count - 1];
    const uint32_t return_dst = finished.return_dst;
    const bool has_caller = finished.has_caller;
    const FrameReturnMode return_mode = finished.return_mode;
    if (!has_caller) {
      value_assign_fast(result.value, return_value);
      --frame_count;
      return false;
    }
    --frame_count;
    Value& target = frames[frame_count - 1].regs[return_dst];
    if (return_mode == FrameReturnMode::StoreConstructedInstance) {
      value_assign_fast(target, finished.continuation_value);
    } else {
      value_assign_fast(target, return_value);
    }
    return true;
  };

  Value current_exception;

  auto normalize_exception = [&](const Value& value) -> Value {
    if (auto* klass = value_as_class(value)) {
      (void)klass;
      return Value::instance(value);
    }
    if (value_as_instance(value) != nullptr) {
      return value;
    }
    return runtime_.make_exception("RuntimeError", value_to_string(value));
  };

  auto dispatch_exception = [&](Value exception) -> bool {
    value_assign_fast(current_exception, exception);
    while (frame_count != 0) {
      auto& handlers = frames[frame_count - 1].exception_handlers;
      if (!handlers.empty()) {
        const auto handler = handlers.back();
        handlers.pop_back();
        frames[frame_count - 1].ip = handler.ip;
        return true;
      }
      --frame_count;
    }
    result.errors.push_back("uncaught exception: " + value_to_string(current_exception));
    return false;
  };

  auto exception_matches = [&](const Value& handler_type) -> bool {
    auto* handler_class = value_as_class(handler_type);
    if (handler_class == nullptr) {
      return false;
    }
    Value exception_type = runtime_.exception_type(current_exception);
    auto* raised_class = value_as_class(exception_type);
    return raised_class != nullptr && class_is_subclass(raised_class, handler_class);
  };

  for (size_t i = 0; i < frames[frame_count - 1].fn->cell_slots.size(); ++i) {
    if (frames[frame_count - 1].fn->cell_slots[i] >= frames[frame_count - 1].locals.size()) {
      result.errors.push_back("invalid cell local slot");
      return result;
    }
    frames[frame_count - 1].cells[i] =
        Value::cell(frames[frame_count - 1].locals[frames[frame_count - 1].fn->cell_slots[i]]);
  }

  while (frame_count != 0) {
    auto& frame = frames[frame_count - 1];
    const auto& module = *frame.module;
    const auto& fn = *frame.fn;
    auto& fn_obj_closure = *frame.closure;
    auto& globals_module = frame.globals_module;
    auto& module_owner = frame.module_owner;
    auto& locals = frame.locals;
    auto& cells = frame.cells;
    auto& regs = frame.regs;
    auto& ip = frame.ip;
    auto& exception_handlers = frame.exception_handlers;
    auto& global_value_cache = frame.global_value_cache;
    auto& global_slot_cache = frame.global_slot_cache;
    auto& global_cache_versions = frame.global_cache_versions;
    auto& global_cache_kind = frame.global_cache_kind;
    auto& call_site_cache = frame.call_site_cache;
    auto& attr_site_cache = frame.attr_site_cache;
    auto& native_call_args = frame.native_call_args;

    auto raise_exception_value = [&](Value exception) -> bool {
      const size_t source_frame = frame_count;
      if (!dispatch_exception(std::move(exception))) {
        return false;
      }
      if (frame_count != source_frame) {
        throw VMUnwind{};
      }
      return true;
    };

    auto raise_runtime_error = [&](const std::string& message) -> bool {
      return raise_exception_value(runtime_.make_exception("RuntimeError", message));
    };

    try {
    for (;;) {
      if (ip >= fn.code.size()) {
        Value none = Value::none();
        if (!finish_frame(none)) {
          return result;
        }
        goto switch_frame;
      }

      const auto& in = fn.code[ip];
      switch (in.op) {
      case ir::Op::LoadConst:
        if (in.a >= fn.constants.size()) {
          result.errors.push_back("invalid constant index");
          return result;
        }
        value_assign_fast(regs[in.dst], fn.constants[in.a]);
        break;
      case ir::Op::LoadLocal:
        if (in.a >= locals.size()) {
          result.errors.push_back("invalid local slot");
          return result;
        }
        value_assign_fast(regs[in.dst], locals[in.a]);
        break;
      case ir::Op::StoreLocal:
        if (in.dst >= locals.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid local store");
          return result;
        }
        value_assign_fast(locals[in.dst], regs[in.a]);
        break;
      case ir::Op::MoveLocal:
        if (in.dst >= locals.size() || in.a >= locals.size()) {
          result.errors.push_back("invalid local move");
          return result;
        }
        value_assign_fast(locals[in.dst], locals[in.a]);
        break;
      case ir::Op::AddLocalConst: {
        if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= fn.constants.size()) {
          result.errors.push_back("invalid local const add");
          return result;
        }
        const auto& lhs = locals[in.a];
        const auto& rhs = fn.constants[in.b];
        if (!fast_add(lhs, rhs, locals[in.dst])) {
          std::string error;
          if (!value_add(lhs, rhs, locals[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::AddLocalLocal: {
        if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= locals.size()) {
          result.errors.push_back("invalid local local add");
          return result;
        }
        const auto& lhs = locals[in.a];
        const auto& rhs = locals[in.b];
        if (!fast_add(lhs, rhs, locals[in.dst])) {
          std::string error;
          if (!value_add(lhs, rhs, locals[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::LoadCell: {
        if (in.a >= cells.size()) {
          result.errors.push_back("invalid cell slot");
          return result;
        }
        auto* cell = value_as_cell(cells[in.a]);
        if (cell == nullptr) {
          result.errors.push_back("invalid cell object");
          return result;
        }
        value_assign_fast(regs[in.dst], cell->value);
        break;
      }
      case ir::Op::StoreCell: {
        if (in.dst >= cells.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid cell store");
          return result;
        }
        auto* cell = value_as_cell(cells[in.dst]);
        if (cell == nullptr) {
          result.errors.push_back("invalid cell object");
          return result;
        }
        value_assign_fast(cell->value, regs[in.a]);
        value_assign_fast(locals[fn.cell_slots[in.dst]], regs[in.a]);
        break;
      }
      case ir::Op::LoadCellObject:
        if (in.a >= cells.size()) {
          result.errors.push_back("invalid cell object slot");
          return result;
        }
        value_assign_fast(regs[in.dst], cells[in.a]);
        break;
      case ir::Op::LoadFree: {
        if (in.a >= fn_obj_closure.size()) {
          result.errors.push_back("invalid free slot");
          return result;
        }
        auto* cell = value_as_cell(fn_obj_closure[in.a]);
        if (cell == nullptr) {
          result.errors.push_back("invalid free cell");
          return result;
        }
        value_assign_fast(regs[in.dst], cell->value);
        break;
      }
      case ir::Op::StoreFree: {
        if (in.dst >= fn_obj_closure.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid free store");
          return result;
        }
        auto* cell = value_as_cell(fn_obj_closure[in.dst]);
        if (cell == nullptr) {
          result.errors.push_back("invalid free cell");
          return result;
        }
        value_assign_fast(cell->value, regs[in.a]);
        break;
      }
      case ir::Op::LoadFreeObject:
        if (in.a >= fn_obj_closure.size()) {
          result.errors.push_back("invalid free object slot");
          return result;
        }
        value_assign_fast(regs[in.dst], fn_obj_closure[in.a]);
        break;
      case ir::Op::LoadGlobal: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid global name");
          return result;
        }
        auto* globals_module_obj = value_as_module(globals_module);
        const uint64_t globals_version = globals_module_obj != nullptr ? globals_module_obj->version : globals_version_;
        if (global_cache_kind[in.a] != 0) {
          if (globals_module_obj != nullptr && global_cache_kind[in.a] == 1) {
            const auto slot = global_slot_cache[in.a];
            if (slot < globals_module_obj->slots.size()) {
              value_assign_fast(regs[in.dst], globals_module_obj->slots[slot]);
              break;
            }
          } else if (global_cache_kind[in.a] == 2 && global_cache_versions[in.a] == globals_version) {
            value_assign_fast(regs[in.dst], global_value_cache[in.a]);
            break;
          }
        }
        const auto& name = fn.names[in.a];
        if (globals_module_obj != nullptr) {
          std::string error;
          uint32_t slot = 0;
          if (module_find_attr_slot(globals_module, name, slot, error)) {
            value_assign_fast(regs[in.dst], globals_module_obj->slots[slot]);
            global_slot_cache[in.a] = slot;
            global_cache_kind[in.a] = 1;
            break;
          }
          if (const auto* builtin = runtime_.find_builtin(name)) {
            value_assign_fast(regs[in.dst], *builtin);
            value_assign_fast(global_value_cache[in.a], regs[in.dst]);
            global_cache_versions[in.a] = globals_module_obj->version;
            global_cache_kind[in.a] = 2;
          } else {
            if (raise_runtime_error("name '" + name + "' is not defined")) continue;
            return result;
          }
        } else if (auto it = globals_.find(name); it != globals_.end()) {
          value_assign_fast(regs[in.dst], it->second);
          value_assign_fast(global_value_cache[in.a], regs[in.dst]);
          global_cache_versions[in.a] = globals_version_;
          global_cache_kind[in.a] = 2;
        } else if (const auto* builtin = runtime_.find_builtin(name)) {
          value_assign_fast(regs[in.dst], *builtin);
          value_assign_fast(global_value_cache[in.a], regs[in.dst]);
          global_cache_versions[in.a] = globals_version_;
          global_cache_kind[in.a] = 2;
        } else {
          if (raise_runtime_error("name '" + name + "' is not defined")) continue;
          return result;
        }
        break;
      }
      case ir::Op::StoreGlobal:
        if (in.dst >= fn.names.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid global store");
          return result;
        }
        if (value_as_module(globals_module) != nullptr) {
          std::string error;
          if (!module_set_attr(globals_module, fn.names[in.dst], regs[in.a], error)) {
            result.errors.push_back(error);
            return result;
          }
        } else {
          value_assign_fast(globals_[fn.names[in.dst]], regs[in.a]);
          ++globals_version_;
        }
        if (auto* globals_module_obj = value_as_module(globals_module)) {
          std::string error;
          uint32_t slot = 0;
          if (module_find_attr_slot(globals_module, fn.names[in.dst], slot, error)) {
            global_slot_cache[in.dst] = slot;
            global_cache_kind[in.dst] = 1;
          }
          global_cache_versions[in.dst] = globals_module_obj->version;
        } else {
          value_assign_fast(global_value_cache[in.dst], regs[in.a]);
          global_cache_versions[in.dst] = globals_version_;
          global_cache_kind[in.dst] = 2;
        }
        break;
      case ir::Op::ImportModule: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid module name");
          return result;
        }
        std::string error;
        if (!runtime_.import_module(fn.names[in.a], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::ImportFrom: {
        if (in.a >= fn.names.size() || in.b >= fn.names.size()) {
          result.errors.push_back("invalid from import");
          return result;
        }
        std::string error;
        if (!runtime_.import_from(fn.names[in.a], fn.names[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::RawBlock: {
        if (in.dst >= fn.raw_blocks.size()) {
          result.errors.push_back("invalid raw block index");
          return result;
        }
        const auto& raw = fn.raw_blocks[in.dst];
        RawBlockContext context;
        context.get_var = [&](const std::string& name, Value& out, std::string& error) -> bool {
          for (size_t i = 0; i < fn.locals.size(); ++i) {
            if (fn.locals[i] == name && i < locals.size()) {
              value_assign_fast(out, locals[i]);
              return true;
            }
          }
          for (size_t i = 0; i < fn.free_vars.size(); ++i) {
            if (fn.free_vars[i] == name && i < fn_obj_closure.size()) {
              auto* cell = value_as_cell(fn_obj_closure[i]);
              if (cell != nullptr) {
                value_assign_fast(out, cell->value);
                return true;
              }
            }
          }
          if (value_as_module(globals_module) != nullptr) {
            if (module_get_attr(globals_module, name, out, error)) {
              return true;
            }
          } else if (auto it = globals_.find(name); it != globals_.end()) {
            value_assign_fast(out, it->second);
            return true;
          }
          if (const auto* builtin = runtime_.find_builtin(name)) {
            value_assign_fast(out, *builtin);
            return true;
          }
          error = "name '" + name + "' is not defined";
          return false;
        };
        context.set_var = [&](const std::string& name, const Value& value, std::string& error) -> bool {
          for (size_t i = 0; i < fn.locals.size(); ++i) {
            if (fn.locals[i] == name && i < locals.size()) {
              value_assign_fast(locals[i], value);
              return true;
            }
          }
          if (value_as_module(globals_module) != nullptr) {
            return module_set_attr(globals_module, name, value, error);
          }
          value_assign_fast(globals_[name], value);
          ++globals_version_;
          return true;
        };
        std::string error;
        if (!runtime_.execute_raw_block(context, raw.language, raw.provider, raw.body, error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::LoadAttr: {
        if (in.b >= fn.names.size()) {
          result.errors.push_back("invalid attribute name");
          return result;
        }
        std::string error;
        if (!load_attr_cached(regs[in.a], fn.names[in.b], attr_site_cache[ip], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::StoreAttr: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid attribute name");
          return result;
        }
        std::string error;
        if (!store_attr_cached(regs[in.dst], fn.names[in.a], regs[in.b], attr_site_cache[ip], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::LoadInstanceSlot: {
        auto* instance = value_as_instance(regs[in.a]);
        if (instance == nullptr || in.b >= instance_slot_count(instance)) {
          if (raise_runtime_error("invalid instance slot load")) continue;
          return result;
        }
        const auto& slot_value = instance_slot_at(instance, in.b);
        if (slot_value.tag == ValueTag::Invalid) {
          if (raise_runtime_error("object has no attribute")) continue;
          return result;
        }
        value_assign_fast(regs[in.dst], slot_value);
        break;
      }
      case ir::Op::StoreInstanceSlot: {
        auto* instance = value_as_instance(regs[in.dst]);
        if (instance == nullptr || in.a >= instance_slot_count(instance)) {
          if (raise_runtime_error("invalid instance slot store")) continue;
          return result;
        }
        value_assign_fast(instance_slot_at(instance, in.a), regs[in.b]);
        break;
      }
      case ir::Op::MakeClass: {
        if (in.a >= fn.names.size() || in.b >= fn.class_attrs.size() || in.c >= fn.class_instance_slots.size()) {
          result.errors.push_back("invalid class data");
          return result;
        }
        std::vector<std::pair<std::string, Value>> attrs;
        attrs.reserve(fn.class_attrs[in.b].size());
        for (const auto& attr : fn.class_attrs[in.b]) {
          if (attr.second >= regs.size()) {
            result.errors.push_back("invalid class attr register");
            return result;
          }
          attrs.push_back(std::make_pair(attr.first, regs[attr.second]));
        }
        regs[in.dst] =
            Value::class_object(fn.names[in.a], std::move(attrs), Value::invalid(), fn.class_instance_slots[in.c]);
        break;
      }
      case ir::Op::MakeFunction: {
        if (in.b >= fn.function_closures.size()) {
          result.errors.push_back("invalid function closure list");
          return result;
        }
        std::vector<Value> closure;
        closure.reserve(fn.function_closures[in.b].size());
        for (const auto reg : fn.function_closures[in.b]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid closure register");
            return result;
          }
          closure.push_back(regs[reg]);
        }
        regs[in.dst] = Value::function(in.a, std::move(closure), globals_module, module_owner);
        break;
      }
      case ir::Op::MakeTuple: {
        if (in.a >= fn.tuple_items.size()) {
          result.errors.push_back("invalid tuple item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.tuple_items[in.a].size());
        for (const auto reg : fn.tuple_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid tuple item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::tuple(std::move(items));
        break;
      }
      case ir::Op::MakeList: {
        if (in.a >= fn.list_items.size()) {
          result.errors.push_back("invalid list item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.list_items[in.a].size());
        for (const auto reg : fn.list_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid list item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::list(std::move(items));
        break;
      }
      case ir::Op::MakeDict: {
        if (in.a >= fn.dict_items.size()) {
          result.errors.push_back("invalid dict item list");
          return result;
        }
        std::vector<std::pair<Value, Value>> items;
        items.reserve(fn.dict_items[in.a].size());
        for (const auto& pair : fn.dict_items[in.a]) {
          if (pair.first >= regs.size() || pair.second >= regs.size()) {
            result.errors.push_back("invalid dict item register");
            return result;
          }
          items.push_back(std::make_pair(regs[pair.first], regs[pair.second]));
        }
        regs[in.dst] = Value::dict(std::move(items));
        break;
      }
      case ir::Op::MakeSet: {
        if (in.a >= fn.set_items.size()) {
          result.errors.push_back("invalid set item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.set_items[in.a].size());
        for (const auto reg : fn.set_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid set item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::set(std::move(items));
        break;
      }
      case ir::Op::ListAppend: {
        std::string error;
        if (!sequence_list_append(regs[in.dst], regs[in.a], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::GetItem: {
        std::string error;
        if (!sequence_get_item(regs[in.a], regs[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::SetItem: {
        std::string error;
        if (!sequence_set_item(regs[in.dst], regs[in.a], regs[in.b], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::GetIter: {
        std::string error;
        if (!sequence_get_iter(regs[in.a], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::IterNext: {
        std::string error;
        bool done = false;
        if (!sequence_iter_next(regs[in.a], done, regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        if (done) {
          ip = in.b;
          continue;
        }
        break;
      }
      case ir::Op::ForRangeConstLocalNext: {
        if (in.a >= locals.size() || in.b >= locals.size() || in.c >= fn.range_specs.size()) {
          result.errors.push_back("invalid fused range loop");
          return result;
        }
        auto& current = locals[in.b];
        const auto& spec = fn.range_specs[in.c];
        if (current.tag != ValueTag::Int64 ||
            spec.first >= fn.constants.size() ||
            spec.second >= fn.constants.size() ||
            fn.constants[spec.first].tag != ValueTag::Int64 ||
            fn.constants[spec.second].tag != ValueTag::Int64) {
          result.errors.push_back("invalid fused range state");
          return result;
        }
        const int64_t value = current.as.i64;
        const int64_t stop = fn.constants[spec.first].as.i64;
        const int64_t step = fn.constants[spec.second].as.i64;
        const bool done = step > 0 ? value >= stop : value <= stop;
        if (done) {
          ip = in.dst;
          continue;
        }
        value_set_int64(locals[in.a], value);
        value_set_int64(current, value + step);
        break;
      }
      case ir::Op::Add: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_add(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_add(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Sub: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_sub(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_sub(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Mul: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_mul(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_mul(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Div: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        bool divide_by_zero = false;
        if (!fast_div(lhs, rhs, regs[in.dst], divide_by_zero)) {
          if (divide_by_zero) {
            if (raise_runtime_error("division by zero")) continue;
            return result;
          }
          std::string error;
          if (!value_div(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Mod: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        bool modulo_by_zero = false;
        if (!fast_mod(lhs, rhs, regs[in.dst], modulo_by_zero)) {
          if (modulo_by_zero) {
            if (raise_runtime_error("integer modulo by zero")) continue;
            return result;
          }
          std::string error;
          if (!value_mod(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::BoolAnd:
        value_set_bool(regs[in.dst], value_truthy(regs[in.a]) && value_truthy(regs[in.b]));
        break;
      case ir::Op::BoolOr:
        value_set_bool(regs[in.dst], value_truthy(regs[in.a]) || value_truthy(regs[in.b]));
        break;
      case ir::Op::Compare: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        const auto op = static_cast<ir::CompareOp>(in.c);
        if (!fast_compare(op, lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_compare(compare_name(op), lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Not:
        value_set_bool(regs[in.dst], !value_truthy(regs[in.a]));
        break;
      case ir::Op::Neg:
        if (regs[in.a].tag == ValueTag::Int64) {
          value_set_int64(regs[in.dst], -regs[in.a].as.i64);
        } else if (regs[in.a].tag == ValueTag::Double) {
          value_set_number(regs[in.dst], -regs[in.a].as.f64);
        } else {
          if (raise_runtime_error("unsupported operand for unary -")) continue;
          return result;
        }
        break;
      case ir::Op::Jump:
        ip = in.dst;
        continue;
      case ir::Op::JumpIfFalse:
        if (!value_truthy(regs[in.a])) {
          ip = in.dst;
          continue;
        }
        break;
      case ir::Op::JumpIfLocalConstFalse: {
        if (in.a >= locals.size() || in.b >= fn.constants.size()) {
          result.errors.push_back("invalid local const jump");
          return result;
        }
        Value compare_result;
        const auto op = static_cast<ir::CompareOp>(in.c);
        if (!fast_compare(op, locals[in.a], fn.constants[in.b], compare_result)) {
          std::string error;
          if (!value_compare(compare_name(op), locals[in.a], fn.constants[in.b], compare_result, error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        if (!value_truthy(compare_result)) {
          ip = in.dst;
          continue;
        }
        break;
      }
      case ir::Op::SetupExcept:
        exception_handlers.push_back({in.dst, ExceptionHandlerKind::Except, 0});
        break;
      case ir::Op::SetupWith:
        exception_handlers.push_back({in.dst, ExceptionHandlerKind::With, in.a});
        break;
      case ir::Op::PopExcept:
        if (exception_handlers.empty()) {
          result.errors.push_back("invalid exception handler pop");
          return result;
        }
        exception_handlers.pop_back();
        break;
      case ir::Op::Raise:
        if (in.a >= regs.size()) {
          result.errors.push_back("invalid raise value");
          return result;
        }
        if (!raise_exception_value(normalize_exception(regs[in.a]))) return result;
        continue;
      case ir::Op::Reraise:
        if (current_exception.tag == ValueTag::Invalid) {
          result.errors.push_back("invalid reraised exception");
          return result;
        }
        if (!raise_exception_value(current_exception)) return result;
        continue;
      case ir::Op::ClearException:
        value_set_invalid(current_exception);
        break;
      case ir::Op::LoadException:
        value_assign_fast(regs[in.dst], current_exception);
        break;
      case ir::Op::LoadExceptionType:
        regs[in.dst] = runtime_.exception_type(current_exception);
        break;
      case ir::Op::MatchException:
        value_set_bool(regs[in.dst], exception_matches(regs[in.a]));
        break;
      case ir::Op::CallMethod: {
        if (in.b >= fn.names.size() || in.c >= fn.call_args.size()) {
          result.errors.push_back("invalid method call");
          return result;
        }
        const auto& name = fn.names[in.b];
        const auto& call_arg_regs = fn.call_args[in.c];
        if (name == "append" && call_arg_regs.size() == 1) {
          if (auto* list = value_as_list(regs[in.a])) {
            list->items.push_back(regs[call_arg_regs[0]]);
            value_set_none(regs[in.dst]);
            break;
          }
        }

        CallArgsView call_args;
        call_args.registers = regs.data();
        call_args.register_args = &call_arg_regs;
        bool pushed_frame = false;
        auto materialize_native_args = [&](CallArgsView values) -> const Value* {
          native_call_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          return native_call_args.data();
        };
        auto call_user_function = [&](FunctionObject* fn_obj, CallArgsView values, Value& out) -> bool {
          (void)out;
          const ir::Module* call_module = &module;
          auto call_module_owner = module_owner;
          if (fn_obj->module != nullptr) {
            call_module = fn_obj->module.get();
            call_module_owner = fn_obj->module;
          }
          ++ip;
          if (!push_frame(*call_module, fn_obj->function_id, values, fn_obj->closure, fn_obj->globals_module,
                          std::move(call_module_owner), in.dst)) {
            return false;
          }
          pushed_frame = true;
          return true;
        };
        auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          const Value* native_args = materialize_native_args(values);
          if (native->callback == nullptr ||
              !native->callback(
                  runtime_,
                  native_args,
                  static_cast<uint32_t>(values.size()),
                  native_result,
                  error,
                  native->user_data)) {
            Value pending;
            if (runtime_.take_pending_exception(pending)) {
              if (raise_exception_value(std::move(pending))) return false;
              return false;
            }
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };

        if (auto* instance = value_as_instance(regs[in.a])) {
          if (auto* klass = value_as_class(instance->klass)) {
            CallArgsView method_args = call_args;
            method_args.leading = &regs[in.a];
            method_args.leading_count = 1;
            if (!call_site_cache.empty()) {
              auto& cache = call_site_cache[ip];
              if (cache.callee_object == &klass->header && cache.class_version == klass->version) {
                if (cache.kind == CallSiteKind::UserFunction) {
                  if (!call_user_function(cache.function, method_args, regs[in.dst])) {
                    if (!result.errors.empty()) return result;
                    continue;
                  }
                  if (pushed_frame) goto switch_frame;
                  break;
                }
                if (cache.kind == CallSiteKind::NativeFunction) {
                  if (!call_native_function(cache.native, method_args, regs[in.dst])) {
                    if (!result.errors.empty()) return result;
                    continue;
                  }
                  break;
                }
                if (cache.kind == CallSiteKind::InlineSelfBinaryMethod && call_arg_regs.empty()) {
                  SelfBinaryMethodSpec spec;
                  spec.lhs_slot = cache.lhs_slot;
                  spec.rhs_slot = cache.rhs_slot;
                  spec.op = cache.inline_op;
                  std::string error;
                  if (!execute_self_binary_method(*instance, spec, regs[in.dst], error)) {
                    if (raise_runtime_error(error)) continue;
                    return result;
                  }
                  break;
                }
              }
            }
            auto method_it = klass->attrs.find(name);
            if (method_it != klass->attrs.end()) {
              if (auto* native = value_as_native_function(method_it->second)) {
                if (!call_site_cache.empty()) {
                  auto& cache = call_site_cache[ip];
                  cache.callee_object = &klass->header;
                  cache.kind = CallSiteKind::NativeFunction;
                  cache.function = nullptr;
                  cache.native = native;
                  cache.class_version = klass->version;
                }
                if (!call_native_function(native, method_args, regs[in.dst])) {
                  if (!result.errors.empty()) return result;
                  continue;
                }
                break;
              }
              if (auto* fn_obj = value_as_function(method_it->second)) {
                SelfBinaryMethodSpec inline_spec;
                if (call_arg_regs.empty() && analyze_self_binary_method(module, *fn_obj, inline_spec)) {
                  if (!call_site_cache.empty()) {
                    auto& cache = call_site_cache[ip];
                    cache.callee_object = &klass->header;
                    cache.kind = CallSiteKind::InlineSelfBinaryMethod;
                    cache.function = fn_obj;
                    cache.native = nullptr;
                    cache.class_version = klass->version;
                    cache.lhs_slot = inline_spec.lhs_slot;
                    cache.rhs_slot = inline_spec.rhs_slot;
                    cache.inline_op = inline_spec.op;
                  }
                  std::string error;
                  if (!execute_self_binary_method(*instance, inline_spec, regs[in.dst], error)) {
                    if (raise_runtime_error(error)) continue;
                    return result;
                  }
                  break;
                }
                if (!call_site_cache.empty()) {
                  auto& cache = call_site_cache[ip];
                  cache.callee_object = &klass->header;
                  cache.kind = CallSiteKind::UserFunction;
                  cache.function = fn_obj;
                  cache.native = nullptr;
                  cache.class_version = klass->version;
                }
                if (!call_user_function(fn_obj, method_args, regs[in.dst])) {
                  if (!result.errors.empty()) return result;
                  continue;
                }
                if (pushed_frame) goto switch_frame;
                break;
              }
            }
          }
        }

        Value method;
        std::string attr_error;
        if (!attribute_get(regs[in.a], name, method, attr_error)) {
          if (raise_runtime_error(attr_error)) continue;
          return result;
        }

        if (auto* bound = value_as_bound_method(method)) {
          CallArgsView bound_args = call_args;
          bound_args.leading = &bound->self;
          bound_args.leading_count = 1;
          if (auto* native = value_as_native_function(bound->function)) {
            if (!call_native_function(native, bound_args, regs[in.dst])) {
              if (!result.errors.empty()) return result;
              continue;
            }
          } else if (auto* fn_obj = value_as_function(bound->function)) {
            if (!call_user_function(fn_obj, bound_args, regs[in.dst])) {
              if (!result.errors.empty()) return result;
              continue;
            }
            if (pushed_frame) goto switch_frame;
          } else {
            if (raise_runtime_error("object is not callable")) continue;
            return result;
          }
        } else if (auto* native = value_as_native_function(method)) {
          if (!call_native_function(native, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (auto* fn_obj = value_as_function(method)) {
          if (!call_user_function(fn_obj, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
          if (pushed_frame) goto switch_frame;
        } else if (auto* klass = value_as_class(method)) {
          Value instance = Value::instance(method);
          CallArgsView init_args = call_args;
          init_args.leading = &instance;
          init_args.leading_count = 1;
          Value init_value;
          std::string init_error;
          if (object_get_attr(method, "__init__", init_value, init_error)) {
            if (auto* native = value_as_native_function(init_value)) {
              Value ignored;
              if (!call_native_function(native, init_args, ignored)) {
                if (!result.errors.empty()) return result;
                continue;
              }
              value_assign_fast(regs[in.dst], instance);
            } else if (auto* init_fn = value_as_function(init_value)) {
              const ir::Module* call_module = &module;
              auto call_module_owner = module_owner;
              if (init_fn->module != nullptr) {
                call_module = init_fn->module.get();
                call_module_owner = init_fn->module;
              }
              Value constructed_instance;
              value_assign_fast(constructed_instance, instance);
              ++ip;
              if (!push_frame(*call_module, init_fn->function_id, init_args, init_fn->closure, init_fn->globals_module,
                              std::move(call_module_owner), in.dst,
                              FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
                return result;
              }
              goto switch_frame;
            } else {
              if (raise_runtime_error("__init__ is not callable")) continue;
              return result;
            }
          } else {
            if (call_args.size() != 0) {
              if (raise_runtime_error("class construction expected no arguments")) continue;
              return result;
            }
            value_assign_fast(regs[in.dst], instance);
          }
        } else {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        }
        break;
      }
      case ir::Op::Call: {
        if (in.b >= fn.call_args.size()) {
          result.errors.push_back("invalid call arg list");
          return result;
        }
        const auto& call_arg_regs = fn.call_args[in.b];
        CallArgsView call_args;
        call_args.registers = regs.data();
        call_args.register_args = &call_arg_regs;
        if (in.a < regs.size() && regs[in.a].tag == ValueTag::Invalid) {
          result.errors.push_back("invalid callee");
          return result;
        }
        const auto& callee = regs[in.a];
        bool pushed_frame = false;
        auto materialize_native_args = [&](CallArgsView values) -> const Value* {
          native_call_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          return native_call_args.data();
        };
        auto call_user_function = [&](FunctionObject* fn_obj, CallArgsView values, Value& out) -> bool {
          (void)out;
          const ir::Module* call_module = &module;
          auto call_module_owner = module_owner;
          if (fn_obj->module != nullptr) {
            call_module = fn_obj->module.get();
            call_module_owner = fn_obj->module;
          }
          ++ip;
          if (!push_frame(*call_module, fn_obj->function_id, values, fn_obj->closure, fn_obj->globals_module,
                          std::move(call_module_owner), in.dst)) {
            return false;
          }
          pushed_frame = true;
          return true;
        };
        auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          const Value* native_args = materialize_native_args(values);
          if (native->callback == nullptr ||
              !native->callback(
                  runtime_,
                  native_args,
                  static_cast<uint32_t>(values.size()),
                  native_result,
                  error,
                  native->user_data)) {
            Value pending;
            if (runtime_.take_pending_exception(pending)) {
              if (raise_exception_value(std::move(pending))) return false;
              return false;
            }
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };
        if (!call_site_cache.empty() && callee.tag == ValueTag::Object && callee.as.obj != nullptr) {
          auto& cache = call_site_cache[ip];
            if (cache.callee_object == callee.as.obj) {
            if (cache.kind == CallSiteKind::UserFunction) {
              if (!call_user_function(cache.function, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
              if (pushed_frame) goto switch_frame;
              break;
            }
            if (cache.kind == CallSiteKind::NativeFunction) {
              if (!call_native_function(cache.native, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
              break;
            }
            if (cache.kind == CallSiteKind::InlineArgBinaryFunction) {
              ArgBinaryFunctionSpec spec;
              spec.lhs_arg = cache.lhs_slot;
              spec.rhs_arg = cache.rhs_slot;
              spec.op = cache.inline_op;
              spec.next_arg = cache.next_arg;
              spec.next_op = cache.next_op;
              spec.has_next = cache.has_next;
              std::string error;
              if (!execute_arg_binary_function(call_args, spec, regs[in.dst], error)) {
                if (raise_runtime_error(error)) continue;
                return result;
              }
              break;
            }
            if (cache.kind == CallSiteKind::UserConstructor || cache.kind == CallSiteKind::NativeConstructor) {
              auto* cached_class = value_as_class(callee);
              if (cached_class == nullptr || cache.class_version != cached_class->version) {
                cache.kind = CallSiteKind::Empty;
              } else {
              Value instance = Value::instance(callee);
              CallArgsView init_args = call_args;
              init_args.leading = &instance;
              init_args.leading_count = 1;
              if (cache.kind == CallSiteKind::UserConstructor) {
                auto* fn_obj = cache.function;
                const ir::Module* call_module = &module;
                auto call_module_owner = module_owner;
                if (fn_obj->module != nullptr) {
                  call_module = fn_obj->module.get();
                  call_module_owner = fn_obj->module;
                }
                Value constructed_instance;
                value_assign_fast(constructed_instance, instance);
                ++ip;
                if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->globals_module,
                                std::move(call_module_owner), in.dst,
                                FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
                  return result;
                }
                goto switch_frame;
              }
              Value ignored;
              if (!call_native_function(cache.native, init_args, ignored)) {
                if (!result.errors.empty()) return result;
                continue;
              }
              value_assign_fast(regs[in.dst], instance);
              break;
              }
            }
          }
        }
        auto call_callable_value = [&](const Value& function_value, CallArgsView values, Value& out) -> bool {
          if (auto* native = value_as_native_function(function_value)) {
            return call_native_function(native, values, out);
          }

          auto* fn_obj = value_as_function(function_value);
          if (fn_obj == nullptr) {
            if (raise_runtime_error("object is not callable")) return false;
            return false;
          }
          return call_user_function(fn_obj, values, out);
        };
        if (auto* fn_obj = value_as_function(callee)) {
          ArgBinaryFunctionSpec inline_spec;
          if (analyze_arg_binary_function(module, *fn_obj, static_cast<uint32_t>(call_args.size()), inline_spec)) {
            if (!call_site_cache.empty() && callee.tag == ValueTag::Object) {
              auto& cache = call_site_cache[ip];
              cache.callee_object = callee.as.obj;
              cache.kind = CallSiteKind::InlineArgBinaryFunction;
              cache.function = fn_obj;
              cache.native = nullptr;
              cache.class_version = 0;
              cache.lhs_slot = inline_spec.lhs_arg;
              cache.rhs_slot = inline_spec.rhs_arg;
              cache.inline_op = inline_spec.op;
              cache.next_arg = inline_spec.next_arg;
              cache.next_op = inline_spec.next_op;
              cache.has_next = inline_spec.has_next;
            }
            std::string error;
            if (!execute_arg_binary_function(call_args, inline_spec, regs[in.dst], error)) {
              if (raise_runtime_error(error)) continue;
              return result;
            }
            break;
          }
          if (!call_site_cache.empty() && callee.tag == ValueTag::Object) {
            auto& cache = call_site_cache[ip];
            cache.callee_object = callee.as.obj;
            cache.kind = CallSiteKind::UserFunction;
            cache.function = fn_obj;
            cache.native = nullptr;
            cache.class_version = 0;
          }
          if (!call_user_function(fn_obj, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
          if (pushed_frame) goto switch_frame;
        } else if (auto* bound = value_as_bound_method(callee)) {
          CallArgsView bound_args = call_args;
          bound_args.leading = &bound->self;
          bound_args.leading_count = 1;
          if (!call_callable_value(bound->function, bound_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
          if (pushed_frame) goto switch_frame;
        } else if (auto* klass = value_as_class(callee)) {
          Value instance = Value::instance(callee);
          CallArgsView init_args = call_args;
          init_args.leading = &instance;
          init_args.leading_count = 1;
          if (!call_site_cache.empty()) {
            auto& cache = call_site_cache[ip];
            if (cache.callee_object == callee.as.obj && cache.class_version == klass->version) {
              if (cache.kind == CallSiteKind::UserConstructor) {
                const auto* fn_obj = cache.function;
                const ir::Module* call_module = &module;
                auto call_module_owner = module_owner;
                if (fn_obj->module != nullptr) {
                  call_module = fn_obj->module.get();
                  call_module_owner = fn_obj->module;
                }
                Value constructed_instance;
                value_assign_fast(constructed_instance, instance);
                ++ip;
                if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->globals_module,
                                std::move(call_module_owner), in.dst,
                                FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
                  return result;
                }
                goto switch_frame;
              }
              if (cache.kind == CallSiteKind::NativeConstructor) {
                Value ignored;
                if (!call_native_function(cache.native, init_args, ignored)) {
                  if (!result.errors.empty()) return result;
                  continue;
                }
                value_assign_fast(regs[in.dst], instance);
                break;
              }
            }
          }
          Value init_value;
          std::string init_error;
          if (object_get_attr(callee, "__init__", init_value, init_error)) {
            if (auto* native = value_as_native_function(init_value)) {
              if (!call_site_cache.empty()) {
                auto& cache = call_site_cache[ip];
                cache.callee_object = callee.as.obj;
                cache.kind = CallSiteKind::NativeConstructor;
                cache.function = nullptr;
                cache.native = native;
                cache.class_version = klass->version;
              }
              Value ignored;
              if (!call_native_function(native, init_args, ignored)) {
                if (!result.errors.empty()) return result;
                continue;
              }
              value_assign_fast(regs[in.dst], instance);
            } else if (auto* fn_obj = value_as_function(init_value)) {
              if (!call_site_cache.empty()) {
                auto& cache = call_site_cache[ip];
                cache.callee_object = callee.as.obj;
                cache.kind = CallSiteKind::UserConstructor;
                cache.function = fn_obj;
                cache.native = nullptr;
                cache.class_version = klass->version;
              }
              const ir::Module* call_module = &module;
              auto call_module_owner = module_owner;
              if (fn_obj->module != nullptr) {
                call_module = fn_obj->module.get();
                call_module_owner = fn_obj->module;
              }
              Value constructed_instance;
              value_assign_fast(constructed_instance, instance);
              ++ip;
              if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->globals_module,
                              std::move(call_module_owner), in.dst,
                              FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
                return result;
              }
              goto switch_frame;
            } else {
              if (raise_runtime_error("__init__ is not callable")) continue;
              return result;
            }
          } else {
            if (call_args.size() != 0) {
              if (raise_runtime_error("class construction expected no arguments")) continue;
              return result;
            }
            value_assign_fast(regs[in.dst], instance);
          }
        } else if (auto* native = value_as_native_function(callee)) {
          if (!call_site_cache.empty() && callee.tag == ValueTag::Object) {
            auto& cache = call_site_cache[ip];
            cache.callee_object = callee.as.obj;
            cache.kind = CallSiteKind::NativeFunction;
            cache.function = nullptr;
            cache.native = native;
            cache.class_version = 0;
          }
          if (!call_native_function(native, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (callee.tag == ValueTag::Object) {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        } else if (callee.tag == ValueTag::Invalid) {
          if (raise_runtime_error("invalid callee")) continue;
          return result;
        } else {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        }
        break;
      }
      case ir::Op::Pop:
        break;
      case ir::Op::Return:
        {
          Value return_value;
          value_assign_fast(return_value, regs[in.a]);
          if (!finish_frame(return_value)) {
            return result;
          }
        }
        goto switch_frame;
      }
      ++ip;
    }
    } catch (const VMUnwind&) {
      goto switch_frame;
    }
switch_frame:
    continue;
  }

  value_set_none(result.value);
  return result;
}

} // namespace xlang3
