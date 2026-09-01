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
#include "../xlang_vm_inline_call.h"
#include "../xlang_vm_names.h"
#include "../xlang_vm_op_switch.h"

#include "runtime_lock.h"

#include "xlang3/attribute.h"
#include "xlang3/builtin_methods.h"
#include "xlang3/builtins.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cctype>
#include <string>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE Value monitoring_code_object_for_function(
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    const ir::Function& fn) {
  if (module_owner == nullptr) {
    return Value::none();
  }
  uint32_t function_id = module.entry;
  for (uint32_t index = 0; index < module.functions.size(); ++index) {
    if (&module.functions[index] == &fn) {
      function_id = index;
      break;
    }
  }
  return Value::code(module_owner, function_id);
}

XLANG3_HOT_INLINE const Value* materialize_native_args(
    CallArgsView values,
    std::vector<Value>& native_call_args) {
  native_call_args.clear();
  native_call_args.reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    native_call_args.push_back(values.get(i));
  }
  return native_call_args.data();
}

XLANG3_HOT_INLINE std::string& xlang_vm_native_error_scratch() {
  thread_local std::string error;
  error.clear();
  return error;
}

template <typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool xlang_vm_raise_not_callable(Runtime& runtime, RaiseExceptionValue&& raise_exception_value) {
  return raise_exception_value(runtime.make_exception("TypeError", "object is not callable"));
}

XLANG3_HOT_INLINE bool xlang_vm_abstract_methods_empty(const Value& methods, std::string& first_name) {
  auto visit = [&first_name](const Value& item) {
    if (first_name.empty()) {
      if (auto* string = value_as_string(item)) {
        first_name = string_object_to_string(*string);
      }
    }
  };
  if (auto* set = value_as_set(methods)) {
    for (const auto& item : set->items) {
      visit(item);
    }
    return set->items.empty();
  }
  if (auto* tuple = value_as_tuple(methods)) {
    for (const auto& item : tuple->items) {
      visit(item);
    }
    return tuple->items.empty();
  }
  if (auto* list = value_as_list(methods)) {
    for (const auto& item : list->items) {
      visit(item);
    }
    return list->items.empty();
  }
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_call_class_is_builtin_module_class(const ClassObject& klass) {
  if (klass.name == XlangVMNames::builtin_module) {
    return true;
  }
  auto it = klass.attrs.find("__module__");
  if (it == klass.attrs.end()) {
    return true;
  }
  auto* module_name = value_as_string(it->second);
  return module_name != nullptr && string_object_to_string(*module_name) == "builtins";
}

XLANG3_HOT_INLINE bool xlang_vm_resolve_class_new_callable(const Value& class_value, ClassObject* klass, Value& out) {
  if (klass == nullptr || xlang_vm_call_class_is_builtin_module_class(*klass) ||
      class_has_builtin_base_name(klass, XlangVMNames::builtin_type)) {
    return false;
  }

  Value new_attr;
  std::string ignored;
  if (!object_lookup_class_attr(class_value, "__new__", new_attr, ignored)) {
    return false;
  }
  if (auto* static_method = value_as_static_method(new_attr)) {
    value_assign_fast(out, static_method->function);
  } else {
    value_assign_fast(out, new_attr);
  }

  if (auto* native = value_as_native_function(out)) {
    if (native->name == "object.__new__" || native->name == "type.__new__") {
      return false;
    }
    return true;
  }
  return value_as_function(out) != nullptr;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool xlang_vm_call_class_new_then_init_sync(
    Runtime& runtime,
    const Value& class_value,
    ClassObject* klass,
    const Value& new_callable,
    CallArgsView call_args,
    Value& out,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (call_args.has_keywords() ||
      call_args.kw_star_arg != UINT32_MAX ||
      (call_args.kw_star_args != nullptr && !call_args.kw_star_args->empty())) {
    return false;
  }
  std::vector<Value> positional_args;
  positional_args.reserve(call_args.size());
  for (size_t i = 0; i < call_args.size(); ++i) {
    positional_args.push_back(call_args.get(i));
  }
  auto expand_star_arg = [&](uint32_t star_reg) -> bool {
    if (call_args.registers == nullptr || star_reg >= UINT32_MAX) {
      return raise_runtime_error("* argument is invalid");
    }
    std::string collect_error;
    if (!runtime_collect_iterable(runtime, call_args.registers[star_reg], positional_args, collect_error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending));
      }
      return raise_runtime_error(collect_error.empty() ? "* argument must be iterable" : collect_error);
    }
    return true;
  };
  if (call_args.star_args != nullptr && !call_args.star_args->empty()) {
    for (uint32_t star_reg : *call_args.star_args) {
      if (!expand_star_arg(star_reg)) {
        return false;
      }
    }
  } else if (call_args.star_arg != UINT32_MAX) {
    if (!expand_star_arg(call_args.star_arg)) {
      return false;
    }
  }

  std::vector<Value> new_args;
  new_args.reserve(positional_args.size() + 1);
  new_args.push_back(class_value);
  for (const auto& arg : positional_args) {
    new_args.push_back(arg);
  }

  Value new_result;
  std::string error;
  if (!runtime_call_callable(
          runtime,
          new_callable,
          new_args.data(),
          static_cast<uint32_t>(new_args.size()),
          new_result,
          error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending));
    }
    return raise_runtime_error(error.empty() ? "__new__ failed" : error);
  }

  auto* instance = value_as_instance(new_result);
  auto* instance_class = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (instance_class != nullptr && klass != nullptr && class_is_subclass(instance_class, klass)) {
    Value init;
    std::string init_error;
    if (object_get_attr(new_result, "__init__", init, init_error)) {
      Value ignored;
      if (!runtime_call_callable(
              runtime,
              init,
              positional_args.data(),
              static_cast<uint32_t>(positional_args.size()),
              ignored,
              error)) {
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending));
        }
        return raise_runtime_error(error.empty() ? "__init__ failed" : error);
      }
    }
  }
  value_assign_fast(out, new_result);
  return true;
}

template <typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool xlang_vm_reject_abstract_class_instantiation(
    Runtime& runtime,
    const Value& class_value,
    ClassObject& klass,
    bool& rejected,
    RaiseExceptionValue&& raise_exception_value) {
  rejected = false;
  Value abstract_methods;
  std::string ignored;
  if (!object_get_attr(class_value, "__abstractmethods__", abstract_methods, ignored)) {
    return true;
  }
  std::string first_name;
  if (xlang_vm_abstract_methods_empty(abstract_methods, first_name)) {
    return true;
  }
  rejected = true;
  std::string message = "Can't instantiate abstract class " + klass.name + " without an implementation for abstract method";
  if (!first_name.empty()) {
    message += " '" + first_name + "'";
  }
  return raise_exception_value(runtime.make_exception("TypeError", message));
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_builtin_method_spec(
    Runtime& runtime,
    const BuiltinMethodSpec& spec,
    const Value& self,
    CallArgsView values,
    std::vector<Value>& native_call_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  CallArgsView method_args = values;
  method_args.leading = &self;
  method_args.leading_count = 1;

  std::string& error = xlang_vm_native_error_scratch();
  bool ok = false;
  if (spec.fast_callback != nullptr) {
    xlang_perf_count_cached_native_fast_call();
    if (!spec.fast_releases_vm_lock) {
      ok = spec.fast_callback(
          runtime,
          method_args.leading,
          method_args.leading_count,
          method_args.registers,
          method_args.register_args == nullptr ? nullptr : method_args.register_args->data(),
          method_args.register_args == nullptr ? 0 : static_cast<uint32_t>(method_args.register_args->size()),
          out,
          error,
          nullptr);
    } else {
      execution_lock.unlock();
      ok = spec.fast_callback(
          runtime,
          method_args.leading,
          method_args.leading_count,
          method_args.registers,
          method_args.register_args == nullptr ? nullptr : method_args.register_args->data(),
          method_args.register_args == nullptr ? 0 : static_cast<uint32_t>(method_args.register_args->size()),
          out,
          error,
          nullptr);
      execution_lock.lock();
    }
  } else {
    Value native_result;
    xlang_perf_count_native_call(false);
    const Value* args = materialize_native_args(method_args, native_call_args);
    execution_lock.unlock();
    ok = spec.callback != nullptr &&
         spec.callback(runtime, args, static_cast<uint32_t>(method_args.size()), native_result, error, nullptr);
    execution_lock.lock();
    if (ok) {
      out = std::move(native_result);
    }
  }

  if (!ok) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      if (raise_exception_value(std::move(pending))) return false;
      return false;
    }
    if (raise_runtime_error(error.empty() ? "builtin method failed" : error)) return false;
    return false;
  }
  return true;
}

XLANG3_HOT_INLINE const Value* materialize_native_call_ex(
    Runtime& runtime,
    CallArgsView values,
    std::vector<Value>& native_call_args,
    std::vector<NativeKeywordArg>& native_keyword_args,
    bool& has_keywords,
    std::string& error) {
  native_call_args.clear();
  native_keyword_args.clear();
  native_call_args.reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    native_call_args.push_back(values.get(i));
  }
  auto expand_star_arg = [&](uint32_t star_reg) -> bool {
    if (values.registers == nullptr || star_reg >= UINT32_MAX) {
      error = "* argument is invalid";
      return false;
    }
    if (!runtime_collect_iterable(runtime, values.registers[star_reg], native_call_args, error)) {
      if (error.empty()) {
        error = "* argument must be iterable";
      }
      return false;
    }
    return true;
  };
  if (values.star_args != nullptr && !values.star_args->empty()) {
    for (uint32_t star_reg : *values.star_args) {
      if (!expand_star_arg(star_reg)) {
        return nullptr;
      }
    }
  } else if (values.star_arg != UINT32_MAX) {
    if (!expand_star_arg(values.star_arg)) {
      return nullptr;
    }
  }
  auto add_keyword = [&](const char* name, const Value* value) -> bool {
    for (const auto& existing : native_keyword_args) {
      if (std::string(existing.name) == name) {
        error = std::string("got multiple values for keyword argument '") + name + "'";
        return false;
      }
    }
    native_keyword_args.push_back(NativeKeywordArg{name, value});
    return true;
  };
  if (values.keyword_args != nullptr) {
    for (const auto& keyword : *values.keyword_args) {
      if (!add_keyword(keyword.name.c_str(), &values.registers[keyword.value_reg])) {
        return nullptr;
      }
    }
  }
  auto expand_kw_star_arg = [&](uint32_t kw_star_reg) -> bool {
    auto* dict = value_as_dict(values.registers[kw_star_reg]);
    if (dict == nullptr) {
      error = "** argument must be dict";
      return false;
    }
    for (const auto& entry : dict->entries) {
      auto* key = value_as_string(entry.first);
      if (key == nullptr) {
        error = "** argument keys must be strings";
        return false;
      }
      if (!add_keyword(string_object_c_str(*key), &entry.second)) {
        return false;
      }
    }
    return true;
  };
  if (values.kw_star_args != nullptr && !values.kw_star_args->empty()) {
    for (uint32_t kw_star_reg : *values.kw_star_args) {
      if (!expand_kw_star_arg(kw_star_reg)) {
        return nullptr;
      }
    }
  } else if (values.kw_star_arg != UINT32_MAX) {
    if (!expand_kw_star_arg(values.kw_star_arg)) {
      return nullptr;
    }
  }
  has_keywords = !native_keyword_args.empty();
  return native_call_args.data();
}





template <
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename CallBuiltinTypeConstructor,
    typename AnalyzeConstMethod,
    typename AnalyzeSelfBinaryMethod,
    typename ExecuteSelfBinaryMethod,
    typename AnalyzeSelfSlotMethod,
    typename ExecuteSelfSlotMethod,
    typename AnalyzeSelfSlotConstSumMethod,
    typename ExecuteSelfSlotConstSumMethod,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_method(
    const ir::Instr& in,
    const ir::Function& fn,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<XlangVMInstrCache>& instr_cache,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor_fn,
    AnalyzeConstMethod&& analyze_const_method_fn,
    AnalyzeSelfBinaryMethod&& analyze_self_binary_method_fn,
    ExecuteSelfBinaryMethod&& execute_self_binary_method_fn,
    AnalyzeSelfSlotMethod&& analyze_self_slot_method_fn,
    ExecuteSelfSlotMethod&& execute_self_slot_method_fn,
    AnalyzeSelfSlotConstSumMethod&& analyze_self_slot_const_sum_method_fn,
    ExecuteSelfSlotConstSumMethod&& execute_self_slot_const_sum_method_fn,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::CallMethod);
  if (in.b >= fn.names.size() || in.c >= fn.call_args.size()) {
    result.errors.push_back("invalid method call");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& name = fn.names[in.b];
  const auto& call_arg_regs = fn.call_args[in.c];

  CallArgsView call_args;
  call_args.registers = regs.value_data();
  call_args.register_args = &call_arg_regs;
  bool pushed_frame = false;
  const bool allow_inline_calls = !runtime.debug_step_active();

  const bool receiver_is_super = value_as_super(regs[in.a]) != nullptr;
  bool receiver_has_direct_method_attr = false;
  if (auto* instance = value_as_instance(regs[in.a])) {
    if (instance->native_get_attr != nullptr) {
      receiver_has_direct_method_attr = true;
    } else {
      if (auto* klass = value_as_class(instance->klass)) {
        auto slot_it = klass->instance_slot_indices.find(name);
        if (slot_it != klass->instance_slot_indices.end() &&
            slot_it->second < instance_slot_count(instance) &&
            instance_slot_at(instance, slot_it->second).tag != ValueTag::Invalid) {
          receiver_has_direct_method_attr = true;
        }
      }
      if (!receiver_has_direct_method_attr) {
        for (const auto& attr : instance->attrs) {
          if (attr.first == name) {
            receiver_has_direct_method_attr = true;
            break;
          }
        }
      }
    }
  }

  if (!receiver_has_direct_method_attr &&
      !receiver_is_super &&
      !instr_cache.empty() &&
      regs[in.a].tag == ValueTag::Object &&
      regs[in.a].as.obj != nullptr) {
    auto& cache = instr_cache[ip].call;
    if (cache.kind == CallSiteKind::BoundNativeFunction &&
        cache.class_version == static_cast<uint64_t>(regs[in.a].as.obj->kind)) {
      CallArgsView bound_args = call_args;
      bound_args.leading = &regs[in.a];
      bound_args.leading_count = 1;
      const bool ok = cache.fast_callback != nullptr
          ? xlang3::xlang_vm::ops::call_cached_native_fast(
                runtime,
                cache.fast_callback,
                cache.native_user_data,
                cache.fast_releases_vm_lock,
                bound_args,
                execution_lock,
                regs[in.dst],
                raise_runtime_error,
                raise_exception_value)
          : xlang3::xlang_vm::ops::call_native_function(
                runtime,
                cache.native,
                bound_args,
                native_call_args,
                execution_lock,
                regs[in.dst],
                raise_runtime_error,
                raise_exception_value);
      if (!ok) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      return XlangVMOpFlow::Next;
    }
    if (cache.kind == CallSiteKind::BuiltinMethodSpec &&
        cache.class_version == static_cast<uint64_t>(regs[in.a].as.obj->kind) &&
        cache.builtin_method != nullptr) {
      if (!call_builtin_method_spec(
              runtime,
              *cache.builtin_method,
              regs[in.a],
              call_args,
              native_call_args,
              execution_lock,
              regs[in.dst],
              raise_runtime_error,
              raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      return XlangVMOpFlow::Next;
    }
  }

  if (!receiver_has_direct_method_attr) {
  if (auto* instance = value_as_instance(regs[in.a])) {
    if (auto* klass = value_as_class(instance->klass)) {
      CallArgsView method_args = call_args;
      method_args.leading = &regs[in.a];
      method_args.leading_count = 1;
      if (!instr_cache.empty()) {
        auto& cache = instr_cache[ip].call;
        if (cache.callee_object == &klass->header && cache.class_version == klass->version) {
          if (cache.kind == CallSiteKind::UserFunction) {
            if (!xlang3::xlang_vm::ops::call_user_function(cache.function, method_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
              if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
              return XlangVMOpFlow::ContinueLoop;
            }
            if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
            return XlangVMOpFlow::Next;
          }
          if (cache.kind == CallSiteKind::NativeFunction) {
            CallArgsView native_args = method_args;
            if (cache.native != nullptr && !cache.native->bind_as_descriptor) {
              native_args = call_args;
            }
            if (!xlang3::xlang_vm::ops::call_native_function(runtime, cache.native, native_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
              if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
              return XlangVMOpFlow::ContinueLoop;
            }
            return XlangVMOpFlow::Next;
          }
          if (allow_inline_calls && cache.kind == CallSiteKind::InlineSelfBinaryMethod && call_arg_regs.empty()) {
            SelfBinaryMethodSpec spec;
            spec.lhs_slot = cache.lhs_slot;
            spec.rhs_slot = cache.rhs_slot;
            spec.op = cache.inline_op;
            std::string error;
            if (!execute_self_binary_method_fn(*instance, spec, regs[in.dst], error)) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
          if (allow_inline_calls && cache.kind == CallSiteKind::InlineConstMethod && call_arg_regs.empty()) {
            value_assign_fast(regs[in.dst], cache.inline_const);
            return XlangVMOpFlow::Next;
          }
          if (allow_inline_calls && cache.kind == CallSiteKind::InlineSelfSlotConstSumMethod && call_arg_regs.empty()) {
            std::string error;
            if (!execute_self_slot_const_sum_method_fn(*instance, cache.lhs_slot, cache.inline_const, regs[in.dst], error)) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
          if (allow_inline_calls && cache.kind == CallSiteKind::InlineSelfSlotMethod && call_arg_regs.empty()) {
            std::string error;
            if (!execute_self_slot_method_fn(*instance, cache.lhs_slot, regs[in.dst], error)) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
          if (allow_inline_calls && cache.kind == CallSiteKind::InlineSmallSelfMethod && call_arg_regs.empty()) {
            bool supported = false;
            std::string error;
            if (!execute_inline_small_self_method(module, *cache.function, regs[in.a], regs[in.dst], supported, error)) {
              if (supported && raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              if (supported) return XlangVMOpFlow::ReturnResult;
            } else {
              return XlangVMOpFlow::Next;
            }
          }
        }
      }
      auto method_it = klass->attrs.find(name);
      if (method_it != klass->attrs.end()) {
        if (auto* native = value_as_native_function(method_it->second)) {
          const CallArgsView& native_args = native->bind_as_descriptor ? method_args : call_args;
          if (!instr_cache.empty()) {
            auto& cache = instr_cache[ip].call;
            cache.callee_object = &klass->header;
            cache.kind = CallSiteKind::NativeFunction;
            cache.function = nullptr;
            cache.native = native;
            cache.class_version = klass->version;
          }
          if (!xlang3::xlang_vm::ops::call_native_function(runtime, native, native_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          return XlangVMOpFlow::Next;
        }
        if (auto* fn_obj = value_as_function(method_it->second)) {
          Value const_value;
          if (allow_inline_calls && call_arg_regs.empty() && analyze_const_method_fn(module, *fn_obj, const_value)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.callee_object = &klass->header;
              cache.kind = CallSiteKind::InlineConstMethod;
              cache.function = fn_obj;
              cache.native = nullptr;
              cache.class_version = klass->version;
              value_assign_fast(cache.inline_const, const_value);
            }
            value_assign_fast(regs[in.dst], const_value);
            return XlangVMOpFlow::Next;
          }
          SelfBinaryMethodSpec inline_spec;
          if (allow_inline_calls && call_arg_regs.empty() && analyze_self_binary_method_fn(module, *fn_obj, inline_spec)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
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
            if (!execute_self_binary_method_fn(*instance, inline_spec, regs[in.dst], error)) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
          if (!instr_cache.empty()) {
            auto& cache = instr_cache[ip].call;
            cache.callee_object = &klass->header;
            cache.kind = CallSiteKind::UserFunction;
            cache.function = fn_obj;
            cache.native = nullptr;
            cache.class_version = klass->version;
          }
          if (allow_inline_calls && call_arg_regs.empty()) {
            uint32_t direct_slot = 0;
            if (analyze_self_slot_method_fn(module, *fn_obj, direct_slot)) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.kind = CallSiteKind::InlineSelfSlotMethod;
                cache.lhs_slot = direct_slot;
              }
              std::string error;
              if (!execute_self_slot_method_fn(*instance, direct_slot, regs[in.dst], error)) {
                if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
                return XlangVMOpFlow::ReturnResult;
              }
              return XlangVMOpFlow::Next;
            }
            uint32_t sum_slot = 0;
            Value sum_const;
            if (analyze_self_slot_const_sum_method_fn(module, *fn_obj, regs[in.a], sum_slot, sum_const)) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.kind = CallSiteKind::InlineSelfSlotConstSumMethod;
                cache.lhs_slot = sum_slot;
                value_assign_fast(cache.inline_const, sum_const);
              }
              std::string error;
              if (!execute_self_slot_const_sum_method_fn(*instance, sum_slot, sum_const, regs[in.dst], error)) {
                if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
                return XlangVMOpFlow::ReturnResult;
              }
              return XlangVMOpFlow::Next;
            }
            bool supported = false;
            std::string error;
            if (execute_inline_small_self_method(module, *fn_obj, regs[in.a], regs[in.dst], supported, error)) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.kind = CallSiteKind::InlineSmallSelfMethod;
              }
              return XlangVMOpFlow::Next;
            }
            if (supported) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
          }
          if (!xlang3::xlang_vm::ops::call_user_function(fn_obj, method_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
          return XlangVMOpFlow::Next;
        }
      }
      Value inherited_method;
      std::string inherited_error;
      if (object_get_class_attr_for_instance(regs[in.a], name, inherited_method, inherited_error)) {
        if (auto* native = value_as_native_function(inherited_method)) {
          if (!instr_cache.empty()) {
            auto& cache = instr_cache[ip].call;
            cache.callee_object = &klass->header;
            cache.kind = CallSiteKind::NativeFunction;
            cache.function = nullptr;
            cache.native = native;
            cache.class_version = klass->version;
          }
          if (!xlang3::xlang_vm::ops::call_native_function(runtime, native, method_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          return XlangVMOpFlow::Next;
        }
        if (auto* fn_obj = value_as_function(inherited_method)) {
          Value const_value;
          if (allow_inline_calls && call_arg_regs.empty() && analyze_const_method_fn(module, *fn_obj, const_value)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.callee_object = &klass->header;
              cache.kind = CallSiteKind::InlineConstMethod;
              cache.function = fn_obj;
              cache.native = nullptr;
              cache.class_version = klass->version;
              value_assign_fast(cache.inline_const, const_value);
            }
            value_assign_fast(regs[in.dst], const_value);
            return XlangVMOpFlow::Next;
          }
          SelfBinaryMethodSpec inline_spec;
          if (allow_inline_calls && call_arg_regs.empty() && analyze_self_binary_method_fn(module, *fn_obj, inline_spec)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
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
            if (!execute_self_binary_method_fn(*instance, inline_spec, regs[in.dst], error)) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
          if (!instr_cache.empty()) {
            auto& cache = instr_cache[ip].call;
            cache.callee_object = &klass->header;
            cache.kind = CallSiteKind::UserFunction;
            cache.function = fn_obj;
            cache.native = nullptr;
            cache.class_version = klass->version;
          }
          if (allow_inline_calls && call_arg_regs.empty()) {
            uint32_t direct_slot = 0;
            if (analyze_self_slot_method_fn(module, *fn_obj, direct_slot)) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.kind = CallSiteKind::InlineSelfSlotMethod;
                cache.lhs_slot = direct_slot;
              }
              std::string error;
              if (!execute_self_slot_method_fn(*instance, direct_slot, regs[in.dst], error)) {
                if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
                return XlangVMOpFlow::ReturnResult;
              }
              return XlangVMOpFlow::Next;
            }
            uint32_t sum_slot = 0;
            Value sum_const;
            if (analyze_self_slot_const_sum_method_fn(module, *fn_obj, regs[in.a], sum_slot, sum_const)) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.kind = CallSiteKind::InlineSelfSlotConstSumMethod;
                cache.lhs_slot = sum_slot;
                value_assign_fast(cache.inline_const, sum_const);
              }
              std::string error;
              if (!execute_self_slot_const_sum_method_fn(*instance, sum_slot, sum_const, regs[in.dst], error)) {
                if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
                return XlangVMOpFlow::ReturnResult;
              }
              return XlangVMOpFlow::Next;
            }
            bool supported = false;
            std::string error;
            if (execute_inline_small_self_method(module, *fn_obj, regs[in.a], regs[in.dst], supported, error)) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.kind = CallSiteKind::InlineSmallSelfMethod;
              }
              return XlangVMOpFlow::Next;
            }
            if (supported) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
          }
          if (!xlang3::xlang_vm::ops::call_user_function(fn_obj, method_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
          return XlangVMOpFlow::Next;
        }
      }
    }
  }

  if (auto* module_object = value_as_module(regs[in.a])) {
    if (!instr_cache.empty()) {
      auto& cache = instr_cache[ip].call;
      if (cache.callee_object == regs[in.a].as.obj &&
          cache.class_version == module_object->version &&
          cache.kind == CallSiteKind::NativeFunction) {
        if (!xlang3::xlang_vm::ops::call_native_function(runtime, cache.native, call_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        return XlangVMOpFlow::Next;
      }
    }

    std::string module_error;
    uint32_t module_slot = 0;
    if (module_find_attr_slot(regs[in.a], name, module_slot, module_error) &&
        module_slot < module_object->slots.size()) {
      const Value& module_attr = module_object->slots[module_slot];
      if (auto* native = value_as_native_function(module_attr)) {
        if (!instr_cache.empty()) {
          auto& cache = instr_cache[ip].call;
          cache.callee_object = regs[in.a].as.obj;
          cache.kind = CallSiteKind::NativeFunction;
          cache.function = nullptr;
          cache.native = native;
          cache.class_version = module_object->version;
        }
        if (!xlang3::xlang_vm::ops::call_native_function(runtime, native, call_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        return XlangVMOpFlow::Next;
      }
    }
  }
  }

  if (const auto* builtin_spec = builtin_method_find_spec_for_call(regs[in.a], name)) {
    if (!instr_cache.empty() && regs[in.a].tag == ValueTag::Object && regs[in.a].as.obj != nullptr) {
      auto& cache = instr_cache[ip].call;
      cache.callee_object = nullptr;
      cache.kind = CallSiteKind::BuiltinMethodSpec;
      cache.function = nullptr;
      cache.native = nullptr;
      cache.builtin_method = builtin_spec;
      cache.fast_callback = builtin_spec->fast_callback;
      cache.native_user_data = nullptr;
      cache.fast_releases_vm_lock = builtin_spec->fast_releases_vm_lock;
      cache.class_version = static_cast<uint64_t>(regs[in.a].as.obj->kind);
    }
    if (!call_builtin_method_spec(
            runtime,
            *builtin_spec,
            regs[in.a],
            call_args,
            native_call_args,
            execution_lock,
            regs[in.dst],
            raise_runtime_error,
            raise_exception_value)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    return XlangVMOpFlow::Next;
  }

  Value method;
  std::string attr_error;
  if (!attribute_get(regs[in.a], name, method, attr_error)) {
    if (raise_runtime_error(attr_error)) return XlangVMOpFlow::ContinueLoop;
    return XlangVMOpFlow::ReturnResult;
  }

  if (auto* bound = value_as_bound_method(method)) {
    CallArgsView bound_args = call_args;
    bound_args.leading = &bound->self;
    bound_args.leading_count = 1;
    if (auto* native = value_as_native_function(bound->function)) {
      if (!receiver_is_super && !instr_cache.empty() && regs[in.a].tag == ValueTag::Object && regs[in.a].as.obj != nullptr) {
        auto& cache = instr_cache[ip].call;
        cache.callee_object = nullptr;
        cache.kind = CallSiteKind::BoundNativeFunction;
        cache.class_version = static_cast<uint64_t>(regs[in.a].as.obj->kind);
        cache.cached_values.clear();
        cache.cached_values.push_back(bound->function);
        cache.native = value_as_native_function(cache.cached_values[0]);
        cache.fast_callback = native->fast_callback;
        cache.native_user_data = native->user_data;
        cache.fast_releases_vm_lock = native->fast_releases_vm_lock;
        cache.function = nullptr;
      }
      if (!xlang3::xlang_vm::ops::call_native_function(runtime, native, bound_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
    } else if (auto* fn_obj = value_as_function(bound->function)) {
      if (!xlang3::xlang_vm::ops::call_user_function(fn_obj, bound_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
    } else {
      if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return XlangVMOpFlow::ContinueLoop;
      return XlangVMOpFlow::ReturnResult;
    }
  } else if (auto* native = value_as_native_function(method)) {
    if (!xlang3::xlang_vm::ops::call_native_function(runtime, native, call_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
  } else if (auto* fn_obj = value_as_function(method)) {
    if (!xlang3::xlang_vm::ops::call_user_function(fn_obj, call_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else if (value_as_instance(method) != nullptr) {
    Value call_attr;
    std::string call_error;
    if (!attribute_get(method, "__call__", call_attr, call_error)) {
      if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return XlangVMOpFlow::ContinueLoop;
      return XlangVMOpFlow::ReturnResult;
    }
    if (!xlang3::xlang_vm::ops::call_callable_value(
            runtime,
            call_attr,
            call_args,
            module,
            module_owner,
            in.dst,
            ip,
            native_call_args,
            execution_lock,
            regs[in.dst],
            pushed_frame,
            make_generator_if_needed,
            push_frame,
            raise_runtime_error,
            raise_exception_value)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else if (auto* klass = value_as_class(method)) {
    if (call_args.size() == 1 && !call_args.has_keywords() && !call_args.has_expansion()) {
      Value enum_member;
      if (class_try_enum_value_lookup(method, call_args.get(0), enum_member)) {
        value_assign_fast(regs[in.dst], enum_member);
        return XlangVMOpFlow::Next;
      }
    }
    Value new_callable;
    if (xlang_vm_resolve_class_new_callable(method, klass, new_callable)) {
      if (!call_args.has_keywords() && call_args.kw_star_arg == UINT32_MAX) {
        if (!xlang_vm_call_class_new_then_init_sync(
                runtime,
                method,
                klass,
                new_callable,
                call_args,
                regs[in.dst],
                raise_runtime_error,
                raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        return XlangVMOpFlow::Next;
      }
      CallArgsView new_args = call_args;
      new_args.leading = &method;
      new_args.leading_count = 1;
      if (!xlang3::xlang_vm::ops::call_callable_value(
              runtime,
              new_callable,
              new_args,
              module,
              module_owner,
              in.dst,
              ip,
              native_call_args,
              execution_lock,
              regs[in.dst],
              pushed_frame,
              make_generator_if_needed,
              push_frame,
              raise_runtime_error,
              raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
      return XlangVMOpFlow::Next;
    }
    std::string constructor_error;
    if (call_builtin_type_constructor_fn(runtime, *klass, call_args, execution_lock, regs[in.dst], constructor_error)) {
      if (value_as_class(regs[in.dst]) == nullptr) {
        return XlangVMOpFlow::Next;
      }
      return call_metaclass_init_after_type_new(
          method,
          regs[in.dst],
          call_args,
          module,
          module_owner,
          runtime,
          native_call_args,
          ip,
          in.dst,
          result,
          execution_lock,
          make_generator_if_needed,
          push_frame,
          raise_runtime_error,
          raise_exception_value);
    }
    if (!constructor_error.empty()) {
      if (raise_runtime_error(constructor_error)) return XlangVMOpFlow::ContinueLoop;
      return XlangVMOpFlow::ReturnResult;
    }
    Value instance = Value::instance(method);
    CallArgsView init_args = call_args;
    init_args.leading = &instance;
    init_args.leading_count = 1;
    Value init_value;
    std::string init_error;
    if (xlang_vm_get_init_attr(method, init_value, init_error)) {
      if (auto* native = value_as_native_function(init_value)) {
        Value ignored;
        if (!xlang3::xlang_vm::ops::call_native_function(runtime, native, init_args, native_call_args, execution_lock, ignored, raise_runtime_error, raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
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
        if (!push_frame(*call_module, init_fn->function_id, init_args, init_fn->closure, init_fn->defaults, init_fn->globals_module,
                        std::move(call_module_owner), in.dst,
                        FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
          return XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::SwitchFrame;
      } else {
        if (raise_runtime_error("__init__ is not callable")) return XlangVMOpFlow::ContinueLoop;
        return XlangVMOpFlow::ReturnResult;
      }
    } else {
      if (call_args.size() != 0) {
        if (raise_runtime_error("class construction expected no arguments")) return XlangVMOpFlow::ContinueLoop;
        return XlangVMOpFlow::ReturnResult;
      }
      value_assign_fast(regs[in.dst], instance);
    }
  } else {
    if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return XlangVMOpFlow::ContinueLoop;
    return XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool try_call_metaclass_new(
    const Value& metaclass_value,
    CallArgsView original_args,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    std::vector<Value>& native_call_args,
    size_t& ip,
    uint32_t return_dst,
    Value& out,
    bool& pushed_frame,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  auto* metaclass = value_as_class(metaclass_value);
  if (metaclass == nullptr || metaclass->name == XlangVMNames::builtin_type ||
      !class_has_builtin_base_name(metaclass, XlangVMNames::builtin_type) ||
      original_args.size() != 3 || original_args.has_expansion()) {
    return false;
  }

  Value new_value;
  std::string new_error;
  if (!object_get_attr(metaclass_value, "__new__", new_value, new_error)) {
    return false;
  }
  if (auto* native = value_as_native_function(new_value)) {
    if (native->name == "type.__new__" || native->name == "object.__new__") {
      return false;
    }
    Value leading[1];
    value_assign_fast(leading[0], metaclass_value);
    CallArgsView new_args = original_args;
    new_args.leading = leading;
    new_args.leading_count = 1;
    return call_native_function(runtime, native, new_args, native_call_args, execution_lock, out,
                                raise_runtime_error, raise_exception_value);
  }
  if (auto* fn_obj = value_as_function(new_value)) {
    Value leading[1];
    value_assign_fast(leading[0], metaclass_value);
    CallArgsView new_args = original_args;
    new_args.leading = leading;
    new_args.leading_count = 1;
    return call_user_function(fn_obj, new_args, module, module_owner, return_dst, ip, out, pushed_frame,
                              make_generator_if_needed, push_frame);
  }
  return false;
}

template <
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_metaclass_init_after_type_new(
    const Value& metaclass_value,
    const Value& constructed_class,
    CallArgsView original_args,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    std::vector<Value>& native_call_args,
    size_t& ip,
    uint32_t return_dst,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  auto* metaclass = value_as_class(metaclass_value);
  if (metaclass == nullptr || metaclass->name == XlangVMNames::builtin_type ||
      !class_has_builtin_base_name(metaclass, XlangVMNames::builtin_type) ||
      original_args.size() != 3 || original_args.has_expansion()) {
    return XlangVMOpFlow::Next;
  }

  Value init_value;
  std::string init_error;
  if (!xlang_vm_get_init_attr(metaclass_value, init_value, init_error)) {
    return XlangVMOpFlow::Next;
  }
  if (auto* native = value_as_native_function(init_value)) {
    if (native->name == "object.__init__") {
      return XlangVMOpFlow::Next;
    }
    Value leading[1];
    value_assign_fast(leading[0], constructed_class);
    CallArgsView init_args = original_args;
    init_args.leading = leading;
    init_args.leading_count = 1;
    Value ignored;
    if (!xlang3::xlang_vm::ops::call_native_function(
            runtime, native, init_args, native_call_args, execution_lock, ignored,
            raise_runtime_error, raise_exception_value)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    return XlangVMOpFlow::Next;
  }
  if (auto* fn_obj = value_as_function(init_value)) {
    Value leading[1];
    value_assign_fast(leading[0], constructed_class);
    CallArgsView init_args = original_args;
    init_args.leading = leading;
    init_args.leading_count = 1;
    const ir::Module* call_module = &module;
    auto call_module_owner = module_owner;
    if (fn_obj->module != nullptr) {
      call_module = fn_obj->module.get();
      call_module_owner = fn_obj->module;
    }
    Value continuation;
    value_assign_fast(continuation, constructed_class);
    ++ip;
    if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->defaults,
                    fn_obj->globals_module, std::move(call_module_owner), return_dst,
                    FrameReturnMode::StoreConstructedInstance, std::move(continuation))) {
      return XlangVMOpFlow::ReturnResult;
    }
    return XlangVMOpFlow::SwitchFrame;
  }
  return raise_runtime_error("__init__ is not callable") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

template <
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename CallBuiltinTypeConstructor,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_ex(
    const ir::Instr& in,
    const ir::Function& fn,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<VMFrame>& frames,
    size_t& frame_count,
    std::vector<XlangVMInstrCache>& instr_cache,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor_fn,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::Call);
  if (in.a >= regs.size() || in.b >= fn.call_specs.size()) {
    result.errors.push_back("invalid extended call");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& spec = fn.call_specs[in.b];
  CallArgsView call_args;
  call_args.registers = regs.value_data();
  call_args.register_args = &spec.positional;
  call_args.keyword_args = &spec.keywords;
  call_args.star_arg = spec.star_arg;
  call_args.kw_star_arg = spec.kw_star_arg;
  call_args.star_args = &spec.star_args;
  call_args.kw_star_args = &spec.kw_star_args;
  const auto& callee = regs[in.a];
  bool pushed_frame = false;
  Value monitoring_code = monitoring_code_object_for_function(module, module_owner, fn);

  std::vector<NativeKeywordArg> native_keyword_args;

  if (auto* bound = value_as_bound_method(callee)) {
    CallArgsView bound_args = call_args;
    bound_args.leading = &bound->self;
    bound_args.leading_count = 1;
    if (!xlang3::xlang_vm::ops::call_callable_value_ex(runtime, bound->function, bound_args, module, module_owner, in.dst, ip, native_call_args, native_keyword_args, execution_lock, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame, raise_runtime_error, raise_exception_value, &monitoring_code, static_cast<int64_t>(ip))) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else if (auto* fn_obj = value_as_function(callee)) {
    if (!xlang3::xlang_vm::ops::call_user_function(fn_obj, call_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else if (auto* native = value_as_native_function(callee)) {
    if (!xlang3::xlang_vm::ops::call_native_function_ex(runtime, native, call_args, native_call_args, native_keyword_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value, &monitoring_code, static_cast<int64_t>(ip))) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
  } else if (auto* klass = value_as_class(callee)) {
    if (call_args.size() == 1 && !call_args.has_keywords() && !call_args.has_expansion()) {
      Value enum_member;
      if (class_try_enum_value_lookup(callee, call_args.get(0), enum_member)) {
        value_assign_fast(regs[in.dst], enum_member);
        return XlangVMOpFlow::Next;
      }
    }
    Value early_new_callable;
    if (xlang_vm_resolve_class_new_callable(callee, klass, early_new_callable)) {
      Value new_callable;
      value_assign_fast(new_callable, early_new_callable);
      if (!call_args.has_keywords() && call_args.kw_star_arg == UINT32_MAX) {
        if (!xlang_vm_call_class_new_then_init_sync(
                runtime,
                callee,
                klass,
                new_callable,
                call_args,
                regs[in.dst],
                raise_runtime_error,
                raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        return XlangVMOpFlow::Next;
      }
      CallArgsView new_args = call_args;
      new_args.leading = &callee;
      new_args.leading_count = 1;
      if (!xlang3::xlang_vm::ops::call_callable_value_ex(
              runtime,
              new_callable,
              new_args,
              module,
              module_owner,
              in.dst,
              ip,
              native_call_args,
              native_keyword_args,
              execution_lock,
              regs[in.dst],
              pushed_frame,
              make_generator_if_needed,
              push_frame,
              raise_runtime_error,
              raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
      return XlangVMOpFlow::Next;
    }
    if (auto* metaclass = value_as_class(klass->metaclass)) {
      Value meta_call;
      std::string meta_call_error;
      (void)metaclass;
      if (object_get_attr(klass->metaclass, "__call__", meta_call, meta_call_error)) {
        CallArgsView meta_call_args = call_args;
        meta_call_args.leading = &callee;
        meta_call_args.leading_count = 1;
        if (!xlang3::xlang_vm::ops::call_callable_value_ex(
                runtime,
                meta_call,
                meta_call_args,
                module,
                module_owner,
                in.dst,
                ip,
                native_call_args,
                native_keyword_args,
                execution_lock,
                regs[in.dst],
                pushed_frame,
                make_generator_if_needed,
                push_frame,
                raise_runtime_error,
                raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
        return XlangVMOpFlow::Next;
      }
    }
    if (klass->name == XlangVMNames::builtin_type && call_args.size() == 3 && !call_args.has_expansion()) {
      if (auto* bases = value_as_tuple(call_args.get(1)); bases != nullptr && !bases->items.empty()) {
        if (auto* base_class = value_as_class(bases->items[0])) {
          if (value_as_class(base_class->metaclass) != nullptr) {
            if (try_call_metaclass_new(
                    base_class->metaclass,
                    call_args,
                    module,
                    module_owner,
                    runtime,
                    native_call_args,
                    ip,
                    in.dst,
                    regs[in.dst],
                    pushed_frame,
                    execution_lock,
                    make_generator_if_needed,
                    push_frame,
                    raise_runtime_error,
                    raise_exception_value)) {
              if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
              return XlangVMOpFlow::Next;
            }
          }
        }
      }
    }
    std::string constructor_error;
    if (try_call_metaclass_new(
            callee,
            call_args,
            module,
            module_owner,
            runtime,
            native_call_args,
            ip,
            in.dst,
            regs[in.dst],
            pushed_frame,
            execution_lock,
            make_generator_if_needed,
            push_frame,
            raise_runtime_error,
            raise_exception_value)) {
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
      if (value_as_class(regs[in.dst]) == nullptr) return XlangVMOpFlow::Next;
      return call_metaclass_init_after_type_new(
          callee,
          regs[in.dst],
          call_args,
          module,
          module_owner,
          runtime,
          native_call_args,
          ip,
          in.dst,
          result,
          execution_lock,
          make_generator_if_needed,
          push_frame,
          raise_runtime_error,
          raise_exception_value);
    }
    Value resolved_new_callable;
    if (xlang_vm_resolve_class_new_callable(callee, klass, resolved_new_callable)) {
      Value new_callable;
      value_assign_fast(new_callable, resolved_new_callable);
      CallArgsView new_args = call_args;
      new_args.leading = &callee;
      new_args.leading_count = 1;
      if (!xlang3::xlang_vm::ops::call_callable_value(
              runtime,
              new_callable,
              new_args,
              module,
              module_owner,
              in.dst,
              ip,
              native_call_args,
              execution_lock,
              regs[in.dst],
              pushed_frame,
              make_generator_if_needed,
              push_frame,
              raise_runtime_error,
              raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
      return XlangVMOpFlow::Next;
    }
    if (call_builtin_type_constructor_fn(runtime, *klass, call_args, execution_lock, regs[in.dst], constructor_error)) {
      return call_metaclass_init_after_type_new(
          callee,
          regs[in.dst],
          call_args,
          module,
          module_owner,
          runtime,
          native_call_args,
          ip,
          in.dst,
          result,
          execution_lock,
          make_generator_if_needed,
          push_frame,
          raise_runtime_error,
          raise_exception_value);
    }
    if (!constructor_error.empty()) {
      if (raise_runtime_error(constructor_error)) return XlangVMOpFlow::ContinueLoop;
      return XlangVMOpFlow::ReturnResult;
    }
    bool abstract_rejected = false;
    if (!xlang_vm_reject_abstract_class_instantiation(runtime, callee, *klass, abstract_rejected, raise_exception_value)) {
      return XlangVMOpFlow::ReturnResult;
    }
    if (abstract_rejected) {
      return XlangVMOpFlow::ContinueLoop;
    }
    Value instance = Value::instance(callee);
    CallArgsView init_args = call_args;
    init_args.leading = &instance;
    init_args.leading_count = 1;
    Value init_value;
    std::string init_error;
    if (xlang_vm_get_init_attr(callee, init_value, init_error)) {
      if (!xlang3::xlang_vm::ops::call_callable_value_ex(runtime, init_value, init_args, module, module_owner, in.dst, ip, native_call_args, native_keyword_args, execution_lock, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame, raise_runtime_error, raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      if (pushed_frame) {
        frames[frame_count - 1].return_mode = FrameReturnMode::StoreConstructedInstance;
        frames[frame_count - 1].continuation_value = instance;
        return XlangVMOpFlow::SwitchFrame;
      }
      value_assign_fast(regs[in.dst], instance);
    } else {
      value_assign_fast(regs[in.dst], instance);
    }
  } else if (value_as_instance(callee) != nullptr) {
    Value call_attr;
    std::string attr_error;
    if (!attribute_get(callee, "__call__", call_attr, attr_error)) {
      if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return XlangVMOpFlow::ContinueLoop;
      return XlangVMOpFlow::ReturnResult;
    }
    if (!xlang3::xlang_vm::ops::call_callable_value_ex(
            runtime,
            call_attr,
            call_args,
            module,
            module_owner,
            in.dst,
            ip,
            native_call_args,
            native_keyword_args,
            execution_lock,
            regs[in.dst],
            pushed_frame,
            make_generator_if_needed,
            push_frame,
            raise_runtime_error,
            raise_exception_value)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else {
    if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return XlangVMOpFlow::ContinueLoop;
    return XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <
    typename MakeGeneratorIfNeeded,
    typename PushFrame,
    typename CallBuiltinTypeConstructor,
    typename AnalyzeArgBinaryFunction,
    typename ExecuteArgBinaryFunction,
    typename AnalyzeSlotConstructor,
    typename ExecuteSlotConstructor,
    typename RaiseRuntimeError,
    typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call(
    const ir::Instr& in,
    const ir::Function& fn,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<XlangVMInstrCache>& instr_cache,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor_fn,
    AnalyzeArgBinaryFunction&& analyze_arg_binary_function_fn,
    ExecuteArgBinaryFunction&& execute_arg_binary_function_fn,
    AnalyzeSlotConstructor&& analyze_slot_constructor_fn,
    ExecuteSlotConstructor&& execute_slot_constructor_fn,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::Call);
  if (in.b >= fn.call_args.size()) {
    result.errors.push_back("invalid call arg list");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& call_arg_regs = fn.call_args[in.b];
  CallArgsView call_args;
  call_args.registers = regs.value_data();
  call_args.register_args = &call_arg_regs;
  if (in.a < regs.size() && regs[in.a].tag == ValueTag::Invalid) {
    result.errors.push_back("invalid callee");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& callee = regs[in.a];
  bool pushed_frame = false;
  const bool allow_inline_calls = !runtime.debug_step_active();
  if (!instr_cache.empty() && callee.tag == ValueTag::Object && callee.as.obj != nullptr) {
    auto& cache = instr_cache[ip].call;
      if (cache.callee_object == callee.as.obj) {
      if (cache.kind == CallSiteKind::UserFunction) {
        if (!xlang3::xlang_vm::ops::call_user_function(cache.function, call_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
        return XlangVMOpFlow::Next;
      }
      if (cache.kind == CallSiteKind::NativeFunction) {
        if (!xlang3::xlang_vm::ops::call_native_function(runtime, cache.native, call_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        return XlangVMOpFlow::Next;
      }
      if (allow_inline_calls && cache.kind == CallSiteKind::InlineArgBinaryFunction) {
        ArgBinaryFunctionSpec spec;
        spec.lhs_arg = cache.lhs_slot;
        spec.rhs_arg = cache.rhs_slot;
        spec.op = cache.inline_op;
        spec.next_arg = cache.next_arg;
        spec.next_op = cache.next_op;
        spec.has_next = cache.has_next;
        std::string error;
        if (!execute_arg_binary_function_fn(call_args, spec, regs[in.dst], error)) {
          if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
          return XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
      if (cache.kind == CallSiteKind::UserConstructor || cache.kind == CallSiteKind::NativeConstructor ||
          (allow_inline_calls && cache.kind == CallSiteKind::InlineSlotConstructor)) {
        auto* cached_class = value_as_class(callee);
        if (cached_class == nullptr || cache.class_version != cached_class->version) {
          cache.kind = CallSiteKind::Empty;
        } else {
        if (allow_inline_calls && cache.kind == CallSiteKind::InlineSlotConstructor) {
          std::string error;
          if (!execute_slot_constructor_fn(callee, call_args, cache.slot_constructor_args, regs[in.dst], error)) {
            if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
            return XlangVMOpFlow::ReturnResult;
          }
          return XlangVMOpFlow::Next;
        }
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
          if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                          std::move(call_module_owner), in.dst,
                          FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
            return XlangVMOpFlow::ReturnResult;
          }
          return XlangVMOpFlow::SwitchFrame;
        }
        Value ignored;
        if (!xlang3::xlang_vm::ops::call_native_function(runtime, cache.native, init_args, native_call_args, execution_lock, ignored, raise_runtime_error, raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        value_assign_fast(regs[in.dst], instance);
        return XlangVMOpFlow::Next;
        }
      }
    }
  }
  if (auto* fn_obj = value_as_function(callee)) {
    ArgBinaryFunctionSpec inline_spec;
    if (allow_inline_calls &&
        analyze_arg_binary_function_fn(module, *fn_obj, static_cast<uint32_t>(call_args.size()), inline_spec)) {
      if (!instr_cache.empty() && callee.tag == ValueTag::Object) {
        auto& cache = instr_cache[ip].call;
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
      if (!execute_arg_binary_function_fn(call_args, inline_spec, regs[in.dst], error)) {
        if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
        return XlangVMOpFlow::ReturnResult;
      }
      return XlangVMOpFlow::Next;
    }
    if (!instr_cache.empty() && callee.tag == ValueTag::Object) {
      auto& cache = instr_cache[ip].call;
      cache.callee_object = callee.as.obj;
      cache.kind = CallSiteKind::UserFunction;
      cache.function = fn_obj;
      cache.native = nullptr;
      cache.class_version = 0;
    }
    if (!xlang3::xlang_vm::ops::call_user_function(fn_obj, call_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else if (auto* bound = value_as_bound_method(callee)) {
    CallArgsView bound_args = call_args;
    bound_args.leading = &bound->self;
    bound_args.leading_count = 1;
    if (!xlang3::xlang_vm::ops::call_callable_value(runtime, bound->function, bound_args, module, module_owner, in.dst, ip, native_call_args, execution_lock, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame, raise_runtime_error, raise_exception_value)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else if (auto* klass = value_as_class(callee)) {
    if (call_args.size() == 1 && !call_args.has_keywords() && !call_args.has_expansion()) {
      Value enum_member;
      if (class_try_enum_value_lookup(callee, call_args.get(0), enum_member)) {
        value_assign_fast(regs[in.dst], enum_member);
        return XlangVMOpFlow::Next;
      }
    }
    Value early_new_callable;
    if (xlang_vm_resolve_class_new_callable(callee, klass, early_new_callable)) {
      Value new_callable;
      value_assign_fast(new_callable, early_new_callable);
      if (!call_args.has_keywords() && call_args.kw_star_arg == UINT32_MAX) {
        if (!xlang_vm_call_class_new_then_init_sync(
                runtime,
                callee,
                klass,
                new_callable,
                call_args,
                regs[in.dst],
                raise_runtime_error,
                raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        return XlangVMOpFlow::Next;
      }
      CallArgsView new_args = call_args;
      new_args.leading = &callee;
      new_args.leading_count = 1;
      if (!xlang3::xlang_vm::ops::call_callable_value(
              runtime,
              new_callable,
              new_args,
              module,
              module_owner,
              in.dst,
              ip,
              native_call_args,
              execution_lock,
              regs[in.dst],
              pushed_frame,
              make_generator_if_needed,
              push_frame,
              raise_runtime_error,
              raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
      return XlangVMOpFlow::Next;
    }
    if (auto* metaclass = value_as_class(klass->metaclass)) {
      Value meta_call;
      std::string meta_call_error;
      (void)metaclass;
      if (object_get_attr(klass->metaclass, "__call__", meta_call, meta_call_error)) {
        CallArgsView meta_call_args = call_args;
        meta_call_args.leading = &callee;
        meta_call_args.leading_count = 1;
        if (!xlang3::xlang_vm::ops::call_callable_value(
                runtime,
                meta_call,
                meta_call_args,
                module,
                module_owner,
                in.dst,
                ip,
                native_call_args,
                execution_lock,
                regs[in.dst],
                pushed_frame,
                make_generator_if_needed,
                push_frame,
                raise_runtime_error,
                raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
        return XlangVMOpFlow::Next;
      }
    }
    std::string constructor_error;
    if (try_call_metaclass_new(
            callee,
            call_args,
            module,
            module_owner,
            runtime,
            native_call_args,
            ip,
            in.dst,
            regs[in.dst],
            pushed_frame,
            execution_lock,
            make_generator_if_needed,
            push_frame,
            raise_runtime_error,
            raise_exception_value)) {
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
      if (value_as_class(regs[in.dst]) == nullptr) return XlangVMOpFlow::Next;
      return call_metaclass_init_after_type_new(
          callee,
          regs[in.dst],
          call_args,
          module,
          module_owner,
          runtime,
          native_call_args,
          ip,
          in.dst,
          result,
          execution_lock,
          make_generator_if_needed,
          push_frame,
          raise_runtime_error,
          raise_exception_value);
    }
    if (call_builtin_type_constructor_fn(runtime, *klass, call_args, execution_lock, regs[in.dst], constructor_error)) {
      return call_metaclass_init_after_type_new(
          callee,
          regs[in.dst],
          call_args,
          module,
          module_owner,
          runtime,
          native_call_args,
          ip,
          in.dst,
          result,
          execution_lock,
          make_generator_if_needed,
          push_frame,
          raise_runtime_error,
          raise_exception_value);
    }
    if (!constructor_error.empty()) {
      if (raise_runtime_error(constructor_error)) return XlangVMOpFlow::ContinueLoop;
      return XlangVMOpFlow::ReturnResult;
    }
    bool abstract_rejected = false;
    if (!xlang_vm_reject_abstract_class_instantiation(runtime, callee, *klass, abstract_rejected, raise_exception_value)) {
      return XlangVMOpFlow::ReturnResult;
    }
    if (abstract_rejected) {
      return XlangVMOpFlow::ContinueLoop;
    }
    Value instance = Value::instance(callee);
    CallArgsView init_args = call_args;
    init_args.leading = &instance;
    init_args.leading_count = 1;
    if (!instr_cache.empty()) {
      auto& cache = instr_cache[ip].call;
      if (cache.callee_object == callee.as.obj && cache.class_version == klass->version) {
        if (allow_inline_calls && cache.kind == CallSiteKind::InlineSlotConstructor) {
          std::string error;
          if (!execute_slot_constructor_fn(callee, call_args, cache.slot_constructor_args, regs[in.dst], error)) {
            if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
            return XlangVMOpFlow::ReturnResult;
          }
          return XlangVMOpFlow::Next;
        }
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
          if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                          std::move(call_module_owner), in.dst,
                          FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
            return XlangVMOpFlow::ReturnResult;
          }
          return XlangVMOpFlow::SwitchFrame;
        }
        if (cache.kind == CallSiteKind::NativeConstructor) {
          Value ignored;
          if (!xlang3::xlang_vm::ops::call_native_function(runtime, cache.native, init_args, native_call_args, execution_lock, ignored, raise_runtime_error, raise_exception_value)) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          value_assign_fast(regs[in.dst], instance);
          return XlangVMOpFlow::Next;
        }
      }
    }
    Value init_value;
    std::string init_error;
    if (xlang_vm_get_init_attr(callee, init_value, init_error)) {
      if (auto* native = value_as_native_function(init_value)) {
        if (!instr_cache.empty()) {
          auto& cache = instr_cache[ip].call;
          cache.callee_object = callee.as.obj;
          cache.kind = CallSiteKind::NativeConstructor;
          cache.function = nullptr;
          cache.native = native;
          cache.class_version = klass->version;
        }
        Value ignored;
        if (!xlang3::xlang_vm::ops::call_native_function(runtime, native, init_args, native_call_args, execution_lock, ignored, raise_runtime_error, raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        value_assign_fast(regs[in.dst], instance);
      } else if (auto* fn_obj = value_as_function(init_value)) {
        SlotConstructorSpec slot_constructor_spec;
        if (allow_inline_calls && !call_args.has_keywords() && !call_args.has_expansion() &&
            analyze_slot_constructor_fn(module, *fn_obj, slot_constructor_spec)) {
          if (!instr_cache.empty()) {
            auto& cache = instr_cache[ip].call;
            cache.callee_object = callee.as.obj;
            cache.kind = CallSiteKind::InlineSlotConstructor;
            cache.function = fn_obj;
            cache.native = nullptr;
            cache.class_version = klass->version;
            cache.slot_constructor_args = slot_constructor_spec;
          }
          std::string error;
          if (!execute_slot_constructor_fn(callee, call_args, slot_constructor_spec, regs[in.dst], error)) {
            if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
            return XlangVMOpFlow::ReturnResult;
          }
          return XlangVMOpFlow::Next;
        }
        if (!instr_cache.empty()) {
          auto& cache = instr_cache[ip].call;
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
        if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                        std::move(call_module_owner), in.dst,
                        FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
          return XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::SwitchFrame;
      } else {
        if (raise_runtime_error("__init__ is not callable")) return XlangVMOpFlow::ContinueLoop;
        return XlangVMOpFlow::ReturnResult;
      }
    } else {
      if (call_args.size() != 0) {
        if (raise_runtime_error("class construction expected no arguments")) return XlangVMOpFlow::ContinueLoop;
        return XlangVMOpFlow::ReturnResult;
      }
      value_assign_fast(regs[in.dst], instance);
    }
  } else if (auto* native = value_as_native_function(callee)) {
    if (!instr_cache.empty() && callee.tag == ValueTag::Object) {
      auto& cache = instr_cache[ip].call;
      cache.callee_object = callee.as.obj;
      cache.kind = CallSiteKind::NativeFunction;
      cache.function = nullptr;
      cache.native = native;
      cache.class_version = 0;
    }
    if (!xlang3::xlang_vm::ops::call_native_function(runtime, native, call_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
  } else if (callee.tag == ValueTag::Object) {
    if (value_as_instance(callee) != nullptr) {
      Value call_attr;
      std::string attr_error;
      if (attribute_get(callee, "__call__", call_attr, attr_error)) {
        if (!xlang3::xlang_vm::ops::call_callable_value(
                runtime,
                call_attr,
                call_args,
                module,
                module_owner,
                in.dst,
                ip,
                native_call_args,
                execution_lock,
                regs[in.dst],
                pushed_frame,
                make_generator_if_needed,
                push_frame,
                raise_runtime_error,
                raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
        return XlangVMOpFlow::Next;
      }
    }
    if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return XlangVMOpFlow::ContinueLoop;
    return XlangVMOpFlow::ReturnResult;
  } else if (callee.tag == ValueTag::Invalid) {
    if (raise_runtime_error("invalid callee")) return XlangVMOpFlow::ContinueLoop;
    return XlangVMOpFlow::ReturnResult;
  } else {
    if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return XlangVMOpFlow::ContinueLoop;
    return XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_cached_native_fast(
    Runtime& runtime,
    NativeFastCallCallback fast_callback,
    void* native_user_data,
    bool fast_releases_vm_lock,
    CallArgsView values,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  std::string& error = xlang_vm_native_error_scratch();
  bool ok = false;
  xlang_perf_count_cached_native_fast_call();
  if (!fast_releases_vm_lock) {
    ok = fast_callback(
        runtime,
        values.leading,
        values.leading_count,
        values.registers,
        values.register_args == nullptr ? nullptr : values.register_args->data(),
        values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
        out,
        error,
        native_user_data);
  } else {
    execution_lock.unlock();
    ok = fast_callback(
        runtime,
        values.leading,
        values.leading_count,
        values.registers,
        values.register_args == nullptr ? nullptr : values.register_args->data(),
        values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
        out,
        error,
        native_user_data);
    execution_lock.lock();
  }
  if (!ok) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      if (raise_exception_value(std::move(pending))) return false;
      return false;
    }
    if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
    return false;
  }
  return true;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_native_function(
    Runtime& runtime,
    NativeFunctionObject* native,
    CallArgsView values,
    std::vector<Value>& native_call_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  std::string& error = xlang_vm_native_error_scratch();
  const bool needs_materialized_expansion = values.has_expansion();
  const bool use_fast = native->fast_callback != nullptr && !needs_materialized_expansion;
  xlang_perf_count_native_call(use_fast);
  const Value* native_args = nullptr;
  uint32_t native_argc = static_cast<uint32_t>(values.size());
  if (needs_materialized_expansion) {
    std::vector<NativeKeywordArg> native_keyword_args;
    bool has_keywords = false;
    native_args = materialize_native_call_ex(runtime, values, native_call_args, native_keyword_args, has_keywords, error);
    if (native_args == nullptr && !error.empty()) {
      if (raise_runtime_error(error)) return false;
      return false;
    }
    if (has_keywords) {
      if (raise_runtime_error("native function '" + native->name + "' does not accept keyword arguments")) return false;
      return false;
    }
    native_argc = static_cast<uint32_t>(native_call_args.size());
  } else if (!use_fast) {
    native_args = materialize_native_args(values, native_call_args);
  }
  Value callable = Value::string(native->name);
  Value code = Value::none();
  Value profile_frame = runtime.current_frame_snapshot();
  if (!sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCall, code, -1, &callable, error)) {
    if (raise_runtime_error(error.empty() ? "monitoring callback failed" : error)) return false;
    return false;
  }
  if (!runtime.emit_profile_event_for_frame(profile_frame, "c_call", callable, error)) {
    if (raise_runtime_error(error.empty() ? "profile callback failed" : error)) return false;
    return false;
  }
  bool ok = false;
  if (use_fast && !native->fast_releases_vm_lock) {
    ok = native->fast_callback(
        runtime,
        values.leading,
        values.leading_count,
        values.registers,
        values.register_args == nullptr ? nullptr : values.register_args->data(),
        values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
        out,
        error,
        native->user_data);
  } else {
    Value native_result;
    execution_lock.unlock();
    ok = use_fast
        ? native->fast_callback(
              runtime,
              values.leading,
              values.leading_count,
              values.registers,
              values.register_args == nullptr ? nullptr : values.register_args->data(),
              values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
              out,
              error,
              native->user_data)
        : native->callback != nullptr &&
              native->callback(
                  runtime,
                  native_args,
                  native_argc,
                  native_result,
                  error,
                  native->user_data);
    execution_lock.lock();
    if (ok && !use_fast) {
      out = std::move(native_result);
    }
  }
  if (!ok) {
    std::string monitoring_error;
    (void)sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCRaise, code, -1, &callable, monitoring_error);
    std::string profile_error;
    (void)runtime.emit_profile_event_for_frame(profile_frame, "c_exception", callable, profile_error);
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      if (raise_exception_value(std::move(pending))) return false;
      return false;
    }
    if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
    return false;
  }
  if (!sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCReturn, code, -1, &callable, error)) {
    if (raise_runtime_error(error.empty() ? "monitoring callback failed" : error)) return false;
    return false;
  }
  if (!runtime.emit_profile_event_for_frame(profile_frame, "c_return", callable, error)) {
    if (raise_runtime_error(error.empty() ? "profile callback failed" : error)) return false;
    return false;
  }
  return true;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_native_function_ex(
    Runtime& runtime,
    NativeFunctionObject* native,
    CallArgsView values,
    std::vector<Value>& native_call_args,
    std::vector<NativeKeywordArg>& native_keyword_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    const Value* monitoring_code = nullptr,
    int64_t monitoring_instruction_offset = -1) {
  std::string error;
  bool has_materialized_keywords = false;
  const bool needs_materialized_ex = values.has_keywords() || values.has_expansion();
  const bool use_fast = native->fast_callback != nullptr && !needs_materialized_ex;
  xlang_perf_count_native_call(use_fast);
  const Value* native_args = nullptr;
  if (needs_materialized_ex) {
    native_args = materialize_native_call_ex(runtime, values, native_call_args, native_keyword_args, has_materialized_keywords, error);
    if (native_args == nullptr && !error.empty()) {
      if (raise_runtime_error(error)) return false;
      return false;
    }
    if (has_materialized_keywords && native->keyword_callback == nullptr) {
      if (raise_runtime_error("native function '" + native->name + "' does not accept keyword arguments")) return false;
      return false;
    }
  } else if (!use_fast) {
    native_args = materialize_native_args(values, native_call_args);
  }
  Value callable = Value::string(native->name);
  Value profile_frame = runtime.current_frame_snapshot();
  if (monitoring_code != nullptr) {
    std::string monitoring_error;
    if (!sys_monitoring_dispatch_event(
            runtime,
            kSysMonitoringEventCall,
            *monitoring_code,
            monitoring_instruction_offset,
            &callable,
            monitoring_error)) {
      if (raise_runtime_error(monitoring_error.empty() ? "monitoring callback failed" : monitoring_error)) return false;
      return false;
    }
  }
  if (!runtime.emit_profile_event_for_frame(profile_frame, "c_call", callable, error)) {
    if (raise_runtime_error(error.empty() ? "profile callback failed" : error)) return false;
    return false;
  }
  bool ok = false;
  if (use_fast && !native->fast_releases_vm_lock) {
    ok = native->fast_callback(
        runtime,
        values.leading,
        values.leading_count,
        values.registers,
        values.register_args == nullptr ? nullptr : values.register_args->data(),
        values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
        out,
        error,
        native->user_data);
  } else {
    Value native_result;
    execution_lock.unlock();
    if (use_fast) {
      ok = native->fast_callback(
          runtime,
          values.leading,
          values.leading_count,
          values.registers,
          values.register_args == nullptr ? nullptr : values.register_args->data(),
          values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
          out,
          error,
          native->user_data);
    } else if (has_materialized_keywords) {
      ok = native->keyword_callback != nullptr &&
           native->keyword_callback(
               runtime,
               native_args,
               static_cast<uint32_t>(native_call_args.size()),
               native_keyword_args.data(),
               static_cast<uint32_t>(native_keyword_args.size()),
               native_result,
               error,
               native->user_data);
    } else {
      ok = native->callback != nullptr &&
           native->callback(
               runtime,
               native_args,
               static_cast<uint32_t>(needs_materialized_ex ? native_call_args.size() : values.size()),
               native_result,
               error,
               native->user_data);
    }
    execution_lock.lock();
    if (ok && !use_fast) {
      out = std::move(native_result);
    }
  }
  if (!ok) {
    if (monitoring_code != nullptr) {
      std::string monitoring_error;
      if (!sys_monitoring_dispatch_event(
              runtime,
              kSysMonitoringEventCRaise,
              *monitoring_code,
              monitoring_instruction_offset,
              &callable,
              monitoring_error)) {
        if (raise_runtime_error(monitoring_error.empty() ? "monitoring callback failed" : monitoring_error)) return false;
        return false;
      }
    }
    std::string profile_error;
    (void)runtime.emit_profile_event_for_frame(profile_frame, "c_exception", callable, profile_error);
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      if (raise_exception_value(std::move(pending))) return false;
      return false;
    }
    if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
    return false;
  }
  if (monitoring_code != nullptr) {
    std::string monitoring_error;
    if (!sys_monitoring_dispatch_event(
            runtime,
            kSysMonitoringEventCReturn,
            *monitoring_code,
            monitoring_instruction_offset,
            &callable,
            monitoring_error)) {
      if (raise_runtime_error(monitoring_error.empty() ? "monitoring callback failed" : monitoring_error)) return false;
      return false;
    }
  }
  if (!runtime.emit_profile_event_for_frame(profile_frame, "c_return", callable, error)) {
    if (raise_runtime_error(error.empty() ? "profile callback failed" : error)) return false;
    return false;
  }
  return true;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame>
XLANG3_HOT_INLINE bool call_user_function(
    FunctionObject* fn_obj,
    CallArgsView values,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    uint32_t return_dst,
    size_t& ip,
    Value& out,
    bool& pushed_frame,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue,
    Value continuation_value = Value::invalid()) {
  bool made_generator = false;
  if (!make_generator_if_needed(fn_obj, values, out, made_generator)) {
    return false;
  }
  if (made_generator) {
    return true;
  }
  const ir::Module* call_module = &module;
  auto call_module_owner = module_owner;
  if (fn_obj->module != nullptr) {
    call_module = fn_obj->module.get();
    call_module_owner = fn_obj->module;
  }
  ++ip;
  if (!push_frame(*call_module, fn_obj->function_id, values, fn_obj->closure, fn_obj->defaults,
                  fn_obj->globals_module, std::move(call_module_owner), return_dst, return_mode,
                  std::move(continuation_value))) {
    return false;
  }
  pushed_frame = true;
  return true;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_callable_value(
    Runtime& runtime,
    const Value& function_value,
    CallArgsView values,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    uint32_t return_dst,
    size_t& ip,
    std::vector<Value>& native_call_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    bool& pushed_frame,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (auto* bound = value_as_bound_method(function_value)) {
    CallArgsView bound_args = values;
    bound_args.leading = &bound->self;
    bound_args.leading_count = 1;
    return call_callable_value(
        runtime,
        bound->function,
        bound_args,
        module,
        module_owner,
        return_dst,
        ip,
        native_call_args,
        execution_lock,
        out,
        pushed_frame,
        make_generator_if_needed,
        push_frame,
        raise_runtime_error,
        raise_exception_value);
  }
  if (auto* native = value_as_native_function(function_value)) {
    return call_native_function(runtime, native, values, native_call_args, execution_lock, out,
                                raise_runtime_error, raise_exception_value);
  }

  auto* fn_obj = value_as_function(function_value);
  if (fn_obj == nullptr) {
    if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return false;
    return false;
  }
  return call_user_function(fn_obj, values, module, module_owner, return_dst, ip, out, pushed_frame,
                            make_generator_if_needed, push_frame);
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow contains_dynamic(
    const ir::Instr& in,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  bool contains_value = false;
  std::string error;
  if (value_contains(regs[in.b], regs[in.a], contains_value, error)) {
    value_set_bool(regs[in.dst], contains_value != (in.c != 0));
    return XlangVMOpFlow::Next;
  }

  Value contains_method;
  std::string attr_error;
  if (!object_get_attr(regs[in.b], "__contains__", contains_method, attr_error)) {
    return raise_runtime_error(error.empty() ? attr_error : error) ? XlangVMOpFlow::ContinueLoop
                                                                   : XlangVMOpFlow::ReturnResult;
  }

  uint32_t arg_reg = in.a;
  CallArgsView call_args;
  call_args.registers = regs.value_data();
  std::vector<uint32_t> register_args = {arg_reg};
  call_args.register_args = &register_args;

  bool pushed_frame = false;
  if (!call_callable_value(
          runtime,
          contains_method,
          call_args,
          module,
          module_owner,
          in.dst,
          ip,
          native_call_args,
          execution_lock,
          regs[in.dst],
          pushed_frame,
          make_generator_if_needed,
          push_frame,
          raise_runtime_error,
          raise_exception_value)) {
    return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  if (pushed_frame) {
    return XlangVMOpFlow::SwitchFrame;
  }
  value_set_bool(regs[in.dst], value_truthy(regs[in.dst]) != (in.c != 0));
  return XlangVMOpFlow::Next;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_callable_value_ex(
    Runtime& runtime,
    const Value& function_value,
    CallArgsView values,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    uint32_t return_dst,
    size_t& ip,
    std::vector<Value>& native_call_args,
    std::vector<NativeKeywordArg>& native_keyword_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    bool& pushed_frame,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    const Value* monitoring_code = nullptr,
    int64_t monitoring_instruction_offset = -1) {
  if (auto* bound = value_as_bound_method(function_value)) {
    CallArgsView bound_args = values;
    bound_args.leading = &bound->self;
    bound_args.leading_count = 1;
    return call_callable_value_ex(
        runtime,
        bound->function,
        bound_args,
        module,
        module_owner,
        return_dst,
        ip,
        native_call_args,
        native_keyword_args,
        execution_lock,
        out,
        pushed_frame,
        make_generator_if_needed,
        push_frame,
        raise_runtime_error,
        raise_exception_value,
        monitoring_code,
        monitoring_instruction_offset);
  }
  if (auto* native = value_as_native_function(function_value)) {
    return call_native_function_ex(runtime, native, values, native_call_args, native_keyword_args, execution_lock, out,
                                   raise_runtime_error, raise_exception_value, monitoring_code, monitoring_instruction_offset);
  }
  if (auto* fn_obj = value_as_function(function_value)) {
    return call_user_function(fn_obj, values, module, module_owner, return_dst, ip, out, pushed_frame,
                              make_generator_if_needed, push_frame);
  }
  if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return false;
  return false;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_module_method(
    const ir::Instr& in,
    const ir::Function& fn,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    const Value& globals_module,
    std::vector<XlangVMInstrCache>& instr_cache,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::CallMethod);
  if (in.a >= module.global_slots.size() || in.b >= fn.names.size() || in.c >= fn.call_args.size()) {
    result.errors.push_back("invalid module method call");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* globals_module_obj = value_as_module(globals_module);
  if (globals_module_obj == nullptr || in.a >= globals_module_obj->slots.size()) {
    return raise_runtime_error("module slot is not bound") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const auto& module_value = globals_module_obj->slots[in.a];
  auto* module_object = value_as_module(module_value);
  if (module_object == nullptr) {
    return raise_runtime_error("imported module binding is not a module") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }

  const auto& call_arg_regs = fn.call_args[in.c];
  CallArgsView call_args;
  call_args.registers = regs.value_data();
  call_args.register_args = &call_arg_regs;

  auto materialize_native_args = [&](CallArgsView values) -> const Value* {
    native_call_args.clear();
    native_call_args.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      native_call_args.push_back(values.get(i));
    }
    return native_call_args.data();
  };

  auto dispatch_native_monitoring_event = [&](int64_t event, const Value& callable) -> bool {
    std::string monitoring_error;
    Value code = monitoring_code_object_for_function(module, module_owner, fn);
    if (!sys_monitoring_dispatch_event(runtime, event, code, static_cast<int64_t>(ip), &callable, monitoring_error)) {
      return raise_runtime_error(monitoring_error) ? false : false;
    }
    return true;
  };

  auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
    std::string error;
    Value native_result;
    Value callable = Value::string(native->name);
    if (!dispatch_native_monitoring_event(kSysMonitoringEventCall, callable)) {
      return false;
    }
    const bool use_fast = native->fast_callback != nullptr;
    xlang_perf_count_native_call(use_fast);
    const Value* native_args = nullptr;
    if (!use_fast) {
      native_args = materialize_native_args(values);
    }
    bool ok = false;
    if (use_fast && !native->fast_releases_vm_lock) {
      ok = native->fast_callback(
          runtime,
          values.leading,
          values.leading_count,
          values.registers,
          values.register_args == nullptr ? nullptr : values.register_args->data(),
          values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
          native_result,
          error,
          native->user_data);
    } else {
      execution_lock.unlock();
      ok = use_fast
          ? native->fast_callback(
                runtime,
                values.leading,
                values.leading_count,
                values.registers,
                values.register_args == nullptr ? nullptr : values.register_args->data(),
                values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                native_result,
                error,
                native->user_data)
          : native->callback != nullptr &&
                native->callback(
                    runtime,
                    native_args,
                    static_cast<uint32_t>(values.size()),
                    native_result,
                    error,
                    native->user_data);
      execution_lock.lock();
    }
    if (!ok) {
      if (!dispatch_native_monitoring_event(kSysMonitoringEventCRaise, callable)) {
        return false;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (raise_exception_value(std::move(pending))) return false;
        return false;
      }
      if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
      return false;
    }
    out = std::move(native_result);
    if (!dispatch_native_monitoring_event(kSysMonitoringEventCReturn, callable)) {
      return false;
    }
    return true;
  };

  auto call_cached_fast_function = [&](const CallSiteCache& cache, CallArgsView values, Value& out) -> bool {
    std::string error;
    Value native_result;
    Value callable = cache.native != nullptr ? Value::string(cache.native->name) : Value::none();
    if (!dispatch_native_monitoring_event(kSysMonitoringEventCall, callable)) {
      return false;
    }
    bool ok = false;
    xlang_perf_count_cached_native_fast_call();
    if (!cache.fast_releases_vm_lock) {
      ok = cache.fast_callback(
          runtime,
          values.leading,
          values.leading_count,
          values.registers,
          values.register_args == nullptr ? nullptr : values.register_args->data(),
          values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
          native_result,
          error,
          cache.native_user_data);
    } else {
      execution_lock.unlock();
      ok = cache.fast_callback(
          runtime,
          values.leading,
          values.leading_count,
          values.registers,
          values.register_args == nullptr ? nullptr : values.register_args->data(),
          values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
          native_result,
          error,
          cache.native_user_data);
      execution_lock.lock();
    }
    if (!ok) {
      if (!dispatch_native_monitoring_event(kSysMonitoringEventCRaise, callable)) {
        return false;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (raise_exception_value(std::move(pending))) return false;
        return false;
      }
      if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
      return false;
    }
    out = std::move(native_result);
    if (!dispatch_native_monitoring_event(kSysMonitoringEventCReturn, callable)) {
      return false;
    }
    return true;
  };

  if (!instr_cache.empty()) {
    auto& cache = instr_cache[ip].call;
    if (cache.callee_object == module_value.as.obj &&
        cache.class_version == module_object->version &&
        cache.kind == CallSiteKind::NativeFunction) {
      if (cache.fast_callback != nullptr) {
        if (!call_cached_fast_function(cache, call_args, regs[in.dst])) {
          return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
      } else if (!call_native_function(cache.native, call_args, regs[in.dst])) {
        return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return XlangVMOpFlow::Next;
    }
  }

  std::string module_error;
  uint32_t module_slot = 0;
  const auto& name = fn.names[in.b];
  if (!module_find_attr_slot(module_value, name, module_slot, module_error) ||
      module_slot >= module_object->slots.size()) {
    return raise_runtime_error(module_error.empty() ? "module method not found" : module_error)
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  const Value& callee = module_object->slots[module_slot];
  auto* native = value_as_native_function(callee);
  if (native == nullptr) {
    if (auto* klass = value_as_class(callee)) {
      if (call_args.size() == 1 && !call_args.has_keywords() && !call_args.has_expansion()) {
        Value enum_member;
        if (class_try_enum_value_lookup(callee, call_args.get(0), enum_member)) {
          value_assign_fast(regs[in.dst], enum_member);
          return XlangVMOpFlow::Next;
        }
      }
      Value new_callable;
      if (xlang_vm_resolve_class_new_callable(callee, klass, new_callable)) {
        if (!xlang_vm_call_class_new_then_init_sync(
                runtime,
                callee,
                klass,
                new_callable,
                call_args,
                regs[in.dst],
                raise_runtime_error,
                raise_exception_value)) {
          return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
      Value instance = Value::instance(callee);
      CallArgsView init_args = call_args;
      init_args.leading = &instance;
      init_args.leading_count = 1;
      Value init_value;
      std::string init_error;
      if (xlang_vm_get_init_attr(callee, init_value, init_error)) {
        if (auto* init_native = value_as_native_function(init_value)) {
          Value ignored;
          if (!call_native_function(init_native, init_args, ignored)) {
            return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
          }
          value_assign_fast(regs[in.dst], instance);
          return XlangVMOpFlow::Next;
        }
        if (auto* init_fn = value_as_function(init_value)) {
          bool pushed_frame = false;
          Value constructed_instance;
          value_assign_fast(constructed_instance, instance);
          if (!call_user_function(
                  init_fn,
                  init_args,
                  module,
                  module_owner,
                  in.dst,
                  ip,
                  regs[in.dst],
                  pushed_frame,
                  make_generator_if_needed,
                  push_frame,
                  FrameReturnMode::StoreConstructedInstance,
                  constructed_instance)) {
            return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
          }
          if (pushed_frame) {
            value_assign_fast(regs[in.dst], constructed_instance);
            return XlangVMOpFlow::SwitchFrame;
          }
        } else {
          if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return XlangVMOpFlow::ContinueLoop;
          return XlangVMOpFlow::ReturnResult;
        }
      } else if (!init_error.empty()) {
        return raise_runtime_error(init_error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      value_assign_fast(regs[in.dst], instance);
      return XlangVMOpFlow::Next;
    }
    std::vector<NativeKeywordArg> native_keyword_args;
    bool pushed_frame = false;
    if (!call_callable_value_ex(
            runtime,
            callee,
            call_args,
            module,
            module_owner,
            in.dst,
            ip,
            native_call_args,
            native_keyword_args,
            execution_lock,
            regs[in.dst],
            pushed_frame,
            make_generator_if_needed,
            push_frame,
            raise_runtime_error,
            raise_exception_value)) {
      return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
  }
  if (!instr_cache.empty()) {
    auto& cache = instr_cache[ip].call;
    cache.callee_object = module_value.as.obj;
    cache.kind = CallSiteKind::NativeFunction;
    cache.function = nullptr;
    cache.native = native;
    cache.fast_callback = native->fast_callback;
    cache.native_user_data = native->user_data;
    cache.fast_releases_vm_lock = native->fast_releases_vm_lock;
    cache.class_version = module_object->version;
  }
  if (!call_native_function(native, call_args, regs[in.dst])) {
    return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
