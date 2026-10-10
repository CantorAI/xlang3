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
#include "../xlang_vm_attr.h"
#include "../xlang_vm_inline_call.h"
#include "../xlang_vm_inline_support.h"
#include "../xlang_vm_names.h"
#include "../xlang_vm_op_switch.h"

#include "xlang_vm_ops_variables.h"

#include "runtime/modules/thread/runtime_lock.h"

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

template <typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow finish_fully_handled_native_constructor(
    Runtime& runtime,
    const XlangVMBuiltinConstructorError& constructor_error,
    RaiseExceptionValue&& raise_exception_value) {
  Value pending;
  if (runtime.take_pending_exception(pending)) {
    return raise_exception_value(std::move(pending))
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  if (!constructor_error.message.empty()) {
    return raise_exception_value(runtime.make_exception(
        constructor_error.type, constructor_error.message))
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE void initialize_exception_call_args(
    Runtime& runtime, Value& instance, const CallArgsView& call_args) {
  auto* object = value_as_instance(instance);
  auto* klass = object == nullptr ? nullptr : value_as_class(object->klass);
  const Value* base_value = runtime.find_builtin("BaseException");
  auto* base = base_value == nullptr ? nullptr : value_as_class(*base_value);
  if (klass == nullptr || base == nullptr || !class_is_subclass(klass, base)) return;
  std::vector<Value> values;
  values.reserve(call_args.size());
  for (uint32_t index = 0; index < call_args.size(); ++index) {
    values.push_back(call_args.get(index));
  }
  runtime_initialize_exception_constructor_args(
      runtime, instance, values.data(), static_cast<uint32_t>(values.size()));
}

XLANG3_HOT_INLINE bool call_ex_constructor_args_callback_free(
    const CallArgsView& args, size_t register_count) {
  const auto check_star = [&](uint32_t reg) {
    return args.registers != nullptr && reg < register_count &&
        (value_as_tuple(args.registers[reg]) != nullptr ||
         value_as_list(args.registers[reg]) != nullptr);
  };
  if (args.star_args != nullptr && !args.star_args->empty()) {
    for (uint32_t reg : *args.star_args) if (!check_star(reg)) return false;
  } else if (args.star_arg != UINT32_MAX && !check_star(args.star_arg)) {
    return false;
  }
  // Restrict persistence to a single completed exact mapping. The generic
  // binder currently performs custom protocols after selecting __init__;
  // declining this proof leaves that separate ordering work visible.
  uint32_t mapping_reg = args.kw_star_arg;
  if (args.kw_star_args != nullptr && !args.kw_star_args->empty()) {
    if (args.kw_star_args->size() != 1) return false;
    mapping_reg = args.kw_star_args->front();
  }
  const DictObject* mapping = nullptr;
  if (mapping_reg != UINT32_MAX) {
    if (args.registers == nullptr || mapping_reg >= register_count) return false;
    mapping = value_as_dict(args.registers[mapping_reg]);
    if (mapping == nullptr) return false;
    for (const auto& entry : mapping->entries)
      if (value_as_string(entry.first) == nullptr) return false;
  }
  if (args.keyword_args != nullptr) {
    for (size_t index = 0; index < args.keyword_args->size(); ++index) {
      const auto& name = (*args.keyword_args)[index].name;
      for (size_t earlier = 0; earlier < index; ++earlier)
        if ((*args.keyword_args)[earlier].name == name) return false;
      if (mapping != nullptr)
        for (const auto& entry : mapping->entries)
          if (string_object_view(*value_as_string(entry.first)) == name) return false;
    }
  }
  return true;
}

XLANG3_HOT_INLINE bool inline_calls_allowed(Runtime& runtime) {
  const auto active_hook = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };
  return !runtime.debug_step_active() &&
      !sys_monitoring_event_may_dispatch(kSysMonitoringEventAll) &&
      !active_hook(runtime.trace_function()) &&
      !active_hook(runtime.profile_function());
}

XLANG3_HOT_INLINE bool inline_python_function_allowed(
    Runtime& runtime,
    const ir::Module& current_module,
    const FunctionObject& function) {
  return xlang_vm_inline_python_function_allowed(runtime, current_module, function);
}

XLANG3_HOT_INLINE bool inline_cached_arg_function_allowed(
    Runtime& runtime,
    const ir::Module& current_module,
    const FunctionObject& function,
    CallSiteCache& cache) {
  const auto active_hook = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };
  if (runtime.debug_step_active() || active_hook(runtime.trace_function()) ||
      active_hook(runtime.profile_function())) return false;
  const uint64_t generation = sys_monitoring_configuration_generation();
  if (cache.inline_function_id == function.function_id &&
      cache.class_version == generation) {
    return cache.fast_method_id == 0;
  }
  const ir::Module* target_module = function.module != nullptr
      ? function.module.get() : &current_module;
  const bool may_dispatch = sys_monitoring_function_may_dispatch(
      target_module, function.function_id);
  const uint64_t after_check_generation = sys_monitoring_configuration_generation();
  if (generation != after_check_generation) {
    cache.inline_function_id = UINT32_MAX;
    return false;
  }
  // Cache the expensive code/event eligibility test until monitoring changes;
  // trace/profile/debug switches remain live and are still checked per call.
  cache.inline_function_id = function.function_id;
  cache.class_version = generation;
  cache.fast_method_id = may_dispatch ? 1u : 0u;
  return !may_dispatch;
}

// Call dispatch templates below share these helpers in both directions.
template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_cached_native_fast(
    Runtime&, NativeFastCallCallback, void*, bool, CallArgsView,
    XlangRuntimeExecutionGuard&, Value&, RaiseRuntimeError&&, RaiseExceptionValue&&);

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_native_function(
    Runtime&, NativeFunctionObject*, CallArgsView, std::vector<Value>&,
    XlangRuntimeExecutionGuard&, Value&, RaiseRuntimeError&&, RaiseExceptionValue&&);

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool call_native_function_ex(
    Runtime&, NativeFunctionObject*, CallArgsView, std::vector<Value>&,
    std::vector<NativeKeywordArg>&, XlangRuntimeExecutionGuard&, Value&,
    RaiseRuntimeError&&, RaiseExceptionValue&&,
    const Value* monitoring_code = nullptr, int64_t monitoring_instruction_offset = -1,
    const Value* observed_callable = nullptr);

template <typename MakeGeneratorIfNeeded, typename PushFrame>
XLANG3_HOT_INLINE bool call_user_function(
    FunctionObject*, CallArgsView, const ir::Module&, const std::shared_ptr<const ir::Module>&,
    uint32_t, size_t&, Value&, bool&, MakeGeneratorIfNeeded&&, PushFrame&&,
    FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue,
    Value continuation_value = Value::invalid(),
    Runtime* ordinary_call_runtime = nullptr);

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
inline bool call_callable_value(
    Runtime&, const Value&, CallArgsView, const ir::Module&, const std::shared_ptr<const ir::Module>&,
    uint32_t, size_t&, std::vector<Value>&, XlangRuntimeExecutionGuard&, Value&, bool&,
    MakeGeneratorIfNeeded&&, PushFrame&&, RaiseRuntimeError&&, RaiseExceptionValue&&);

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
inline bool call_callable_value_ex(
    Runtime&, const Value&, CallArgsView, const ir::Module&, const std::shared_ptr<const ir::Module>&,
    uint32_t, size_t&, std::vector<Value>&, std::vector<NativeKeywordArg>&,
    XlangRuntimeExecutionGuard&, Value&, bool&, MakeGeneratorIfNeeded&&, PushFrame&&,
    RaiseRuntimeError&&, RaiseExceptionValue&&,
    const Value* monitoring_code = nullptr, int64_t monitoring_instruction_offset = -1);

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_metaclass_init_after_type_new(
    const Value&, const Value&, CallArgsView, const ir::Module&,
    const std::shared_ptr<const ir::Module>&, Runtime&, std::vector<Value>&, size_t&, uint32_t,
    RuntimeResult&, XlangRuntimeExecutionGuard&, MakeGeneratorIfNeeded&&, PushFrame&&,
    RaiseRuntimeError&&, RaiseExceptionValue&&);

XLANG3_HOT_INLINE bool xlang_vm_is_default_object_hook(const Value& hook, const char* name) {
  auto* native = value_as_native_function(hook);
  if (native != nullptr) {
    return native->name == name;
  }
  auto* bound = value_as_bound_method(hook);
  if (bound == nullptr) {
    return false;
  }
  native = value_as_native_function(bound->function);
  return native != nullptr && native->name == name;
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

// Reuse native-call argument capacity without turning copied arguments into
// lifetime roots after the synchronous callback returns. This matters for
// buffer-protocol arguments: retaining a temporary memoryview pins its mutable
// bytearray and makes a later resize fail even after the VM's last-use cleanup.
struct NativeCallArgsScope {
  std::vector<Value>& values;
  ~NativeCallArgsScope() { values.clear(); }
};

XLANG3_HOT_INLINE std::string& xlang_vm_native_error_scratch() {
  thread_local std::string error;
  error.clear();
  return error;
}

template <typename RaiseExceptionValue>
XLANG3_HOT_INLINE bool xlang_vm_raise_not_callable(Runtime& runtime, RaiseExceptionValue&& raise_exception_value) {
  return raise_exception_value(runtime.make_exception("TypeError", "object is not callable"));
}

XLANG3_HOT_INLINE bool xlang_vm_abstract_methods_empty(
    const Value& methods,
    std::vector<std::string>& names) {
  auto visit = [&names](const Value& item) {
    if (auto* string = value_as_string(item)) {
      names.push_back(string_object_to_string(*string));
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

XLANG3_HOT_INLINE bool xlang_vm_resolve_class_new_callable(
    Runtime& runtime,
    const Value& class_value,
    ClassObject* klass,
    Value& out,
    bool* plain_python_new = nullptr) {
  if (plain_python_new != nullptr) *plain_python_new = false;
  if (klass == nullptr ||
      (xlang_vm_call_class_is_builtin_module_class(*klass) &&
       klass->name != "complex" && klass->name != "iterator") ||
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
  } else if (auto* class_method = value_as_class_method(new_attr)) {
    out = Value::bound_method(class_value, class_method->function);
  } else {
    value_assign_fast(out, new_attr);
  }

  if (auto* native = value_as_native_function(out)) {
    if (native->name == "object.__new__" || native->name == "type.__new__") {
      return false;
    }
    return true;
  }
  if (value_as_function(out) != nullptr) {
    // Only the raw Function/exact StaticMethod selection is eligible. A
    // descriptor which returns a Function still uses the synchronous path.
    if (plain_python_new != nullptr) *plain_python_new = true;
    return true;
  }
  return class_get_bound_attr(runtime, class_value, Value::none(), "__new__", out, ignored);
}

XLANG3_HOT_INLINE bool xlang_vm_plain_sync_function(const FunctionObject* function) {
  if (function == nullptr || function->module == nullptr ||
      function->function_id >= function->module->functions.size()) return false;
  const auto& body = function->module->functions[function->function_id];
  return !body.is_generator && !body.is_async && !body.is_coroutine;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame>
XLANG3_HOT_INLINE bool xlang_vm_try_push_python_class_new(
    Runtime& runtime, const Value& class_value, ClassObject* klass,
    const Value& new_callable, CallArgsView args, const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner, size_t register_count, uint32_t return_dst,
    size_t& ip, Value& out, bool& pushed_frame, bool& attempted,
    MakeGeneratorIfNeeded&& make_generator_if_needed, PushFrame&& push_frame) {
  attempted = false;
  auto* function = value_as_function(new_callable);
  const Value* builtin_type = runtime.find_builtin("type");
  if (klass == nullptr || klass->native_type_constructor != nullptr ||
      builtin_type == nullptr || !value_is(klass->metaclass, *builtin_type) ||
      !xlang_vm_plain_sync_function(function) || args.has_keywords() ||
      args.kw_star_arg != UINT32_MAX ||
      (args.kw_star_args != nullptr && !args.kw_star_args->empty()) ||
      args.size() > UINT32_MAX - 2 ||
      !inline_calls_allowed(runtime) || runtime.debug_poll_needed() ||
      interpreter_pending_events() != 0) return false;
  size_t expanded_size = args.size();
  auto count_tuple = [&](uint32_t reg) -> bool {
    if (args.registers == nullptr || reg >= register_count) return false;
    const auto& value = args.registers[reg];
    if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
        value.as.obj->kind != ObjectKind::Tuple) return false;
    const auto* tuple = value_as_tuple(value);
    if (tuple->items.size() > UINT32_MAX - 2 - expanded_size) return false;
    expanded_size += tuple->items.size();
    return true;
  };
  if (args.star_args != nullptr && !args.star_args->empty()) {
    for (uint32_t reg : *args.star_args) if (!count_tuple(reg)) return false;
  } else if (args.star_arg != UINT32_MAX && !count_tuple(args.star_arg)) {
    return false;
  }
  attempted = true;

  // This changes execution topology, not the Python body or its result. A
  // normal frame push avoids recursively constructing an Interpreter for
  // plain __new__. Keep selection and all original inputs in an owning tuple
  // until fresh __init__ and old-output cleanup finish; nothing is cached
  // across calls. Copy construction upgrades borrowed register arguments.
  std::vector<Value> context;
  context.reserve(expanded_size + 2);
  context.push_back(new_callable);
  context.push_back(class_value);
  for (size_t i = 0; i < args.size(); ++i) context.push_back(args.get(i));
  // CallEx admits only exact Tuple expansion and no keywords. Match the old
  // helper's explicit-then-star materialization order without invoking Python
  // iteration. List/subclass/custom iterators and ** mappings decline before
  // any expansion or __new__ execution; the original helper runs exactly once.
  auto append_tuple = [&](uint32_t reg) {
    for (const auto& item : value_as_tuple(args.registers[reg])->items)
      context.push_back(item);
  };
  if (args.star_args != nullptr && !args.star_args->empty()) {
    for (uint32_t reg : *args.star_args) append_tuple(reg);
  } else if (args.star_arg != UINT32_MAX) {
    append_tuple(args.star_arg);
  }
  Value continuation = Value::tuple(std::move(context));
  const auto* tuple = value_as_tuple(continuation);
  CallArgsView new_args;
  new_args.leading = tuple->items.begin() + 1;
  new_args.leading_count = static_cast<uint32_t>(tuple->items.size() - 1);
  return call_user_function(
      function, new_args, module, module_owner, return_dst, ip, out,
      pushed_frame, make_generator_if_needed, push_frame,
      FrameReturnMode::ContinuePythonClassNew, std::move(continuation));
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
  std::vector<Value> positional_args;
  positional_args.reserve(call_args.size());
  for (size_t i = 0; i < call_args.size(); ++i) {
    positional_args.push_back(call_args.get(i));
  }
  auto expand_star_arg = [&](uint32_t star_reg) -> bool {
    if (call_args.registers == nullptr || star_reg >= UINT32_MAX) {
      if (raise_runtime_error("* argument is invalid")) return false;
      return false;
    }
    std::string collect_error;
    if (!runtime_collect_iterable(runtime, call_args.registers[star_reg], positional_args, collect_error)) {
      Value pending;
      runtime.take_pending_exception(pending);
      if (collect_error == "object is not iterable" || collect_error.empty()) {
        collect_error = "Value after * must be an iterable, not " +
            std::string(value_binary_type_name(call_args.registers[star_reg]));
      }
      if (raise_exception_value(runtime.make_exception("TypeError", collect_error))) return false;
      return false;
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

  std::vector<std::pair<std::string, Value>> keyword_args;
  auto add_keyword = [&](std::string name, const Value& value) -> bool {
    for (const auto& existing : keyword_args) {
      if (existing.first == name) {
        if (raise_runtime_error("got multiple values for keyword argument '" + name + "'")) return false;
        return false;
      }
    }
    keyword_args.emplace_back(std::move(name), value);
    return true;
  };
  if (call_args.keyword_args != nullptr) {
    for (const auto& keyword : *call_args.keyword_args) {
      if (!add_keyword(keyword.name, call_args.registers[keyword.value_reg])) {
        return false;
      }
    }
  }
  auto expand_kw_star_arg = [&](uint32_t kw_star_reg) -> bool {
    if (call_args.registers == nullptr) {
      if (raise_runtime_error("** argument is invalid")) return false;
      return false;
    }
    auto* dict = value_as_dict(call_args.registers[kw_star_reg]);
    if (dict == nullptr) {
      std::string callable_name = klass == nullptr ? "function" : klass->name;
      if (klass != nullptr) {
        const auto module_it = klass->attrs.find("__module__");
        if (module_it != klass->attrs.end()) {
          if (auto* module_name = value_as_string(module_it->second)) {
            callable_name = string_object_to_string(*module_name) + "." + callable_name;
          }
        }
      }
      const std::string message = callable_name +
          "() argument after ** must be a mapping, not " +
          value_binary_type_name(call_args.registers[kw_star_reg]);
      if (raise_exception_value(runtime.make_exception("TypeError", message))) return false;
      return false;
    }
    for (const auto& entry : dict->entries) {
      auto* key = value_as_string(entry.first);
      if (key == nullptr) {
        if (raise_runtime_error("** argument keys must be strings")) return false;
        return false;
      }
      if (!add_keyword(string_object_to_string(*key), entry.second)) {
        return false;
      }
    }
    return true;
  };
  if (call_args.kw_star_args != nullptr && !call_args.kw_star_args->empty()) {
    for (uint32_t kw_star_reg : *call_args.kw_star_args) {
      if (!expand_kw_star_arg(kw_star_reg)) {
        return false;
      }
    }
  } else if (call_args.kw_star_arg != UINT32_MAX) {
    if (!expand_kw_star_arg(call_args.kw_star_arg)) {
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
  if (!runtime_call_callable_kw(
          runtime,
          new_callable,
          new_args.data(),
          static_cast<uint32_t>(new_args.size()),
          keyword_args,
          new_result,
          error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      if (raise_exception_value(std::move(pending))) return false;
      return false;
    }
    if (raise_runtime_error(error.empty() ? "__new__ failed" : error)) return false;
    return false;
  }

  auto* instance = value_as_instance(new_result);
  auto* module_instance = value_as_module(new_result);
  auto* instance_class = instance != nullptr ? value_as_class(instance->klass)
      : module_instance != nullptr ? value_as_class(module_instance->klass) : nullptr;
  if (instance_class != nullptr && klass != nullptr && class_is_subclass(instance_class, klass)) {
    Value init;
    std::string init_error;
    const bool found_init = module_instance != nullptr
        ? module_get_attr(new_result, "__init__", init, init_error)
        : object_get_attr(new_result, "__init__", init, init_error);
    if (found_init && init.tag != ValueTag::Invalid) {
      const NativeFunctionObject* init_native = value_as_native_function(init);
      if (auto* bound_init = value_as_bound_method(init)) {
        init_native = value_as_native_function(bound_init->function);
      }
      if (init_native != nullptr && init_native->name == "object.__init__") {
        value_assign_fast(out, new_result);
        return true;
      }
      Value ignored;
      if (!runtime_call_callable_kw(
              runtime,
              init,
              positional_args.data(),
              static_cast<uint32_t>(positional_args.size()),
              keyword_args,
              ignored,
              error)) {
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          if (raise_exception_value(std::move(pending))) return false;
          return false;
        }
        if (raise_runtime_error(error.empty() ? "__init__ failed" : error)) return false;
        return false;
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
  std::vector<std::string> abstract_names;
  if (xlang_vm_abstract_methods_empty(abstract_methods, abstract_names)) {
    return true;
  }
  std::sort(abstract_names.begin(), abstract_names.end());
  rejected = true;
  std::string message = "Can't instantiate abstract class " + klass.name + " without an implementation for abstract method";
  if (abstract_names.size() != 1) {
    message += "s";
  }
  if (!abstract_names.empty()) {
    message += " ";
    for (size_t i = 0; i < abstract_names.size(); ++i) {
      if (i != 0) {
        message += i + 1 == abstract_names.size() ? ", " : ", ";
      }
      message += "'" + abstract_names[i] + "'";
    }
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
    RaiseExceptionValue&& raise_exception_value,
    const Value* monitoring_code = nullptr,
    int64_t monitoring_instruction_offset = -1) {
  CallArgsView method_args = values;
  method_args.leading = &self;
  method_args.leading_count = 1;

  const bool profiling = runtime.profile_event_may_dispatch() &&
      runtime.profile_function().tag != ValueTag::Invalid &&
      runtime.profile_function().tag != ValueTag::None &&
      !runtime.profile_dispatch_active();
  constexpr int64_t native_events =
      kSysMonitoringEventCall | kSysMonitoringEventCReturn | kSysMonitoringEventCRaise;
  if (profiling || sys_monitoring_event_may_dispatch(native_events)) {
    // Direct builtin specs omit binding when observers are inactive. Active
    // observers need the actual callable and caller location, not a name string.
    // Own self as well: CallLocalMethod can replace the receiver's register.
    Value callable = Value::bound_method(self, builtin_method_function(spec));
    auto* bound = value_as_bound_method(callable);
    auto* native = value_as_native_function(bound->function);
    method_args.leading = &bound->self;
    std::vector<NativeKeywordArg> native_keyword_args;
    return call_native_function_ex(
        runtime, native, method_args, native_call_args, native_keyword_args,
        execution_lock, out, raise_runtime_error, raise_exception_value,
        monitoring_code, monitoring_instruction_offset, &callable);
  }

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
      if (error == "object is not iterable" || error.empty()) {
        error = "Value after * must be an iterable, not " +
            std::string(value_binary_type_name(values.registers[star_reg]));
      }
      Value pending;
      runtime.take_pending_exception(pending);
      runtime.raise_class_error("TypeError", error);
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
      runtime.raise_class_error("TypeError", error);
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





XLANG3_NOINLINE inline bool xlang_vm_try_cached_call_local_accumulate(
    const ir::Instr& in,
    const ir::Function& fn,
    const CallSiteCache& cache,
    CallArgsView call_args,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    size_t& ip) {
  if ((in.c & ir::kCallAccumulateLocalFlag) == 0 ||
      (in.c & ir::kCallAccumulateLocalMask) >= locals.size() ||
      regs[in.dst].tag == ValueTag::Object || ip + 2 >= fn.code.size()) {
    return false;
  }
  const uint32_t accumulator_slot = in.c & ir::kCallAccumulateLocalMask;
  const auto& sum = fn.code[ip + 1];
  const auto& store = fn.code[ip + 2];
  const uint32_t accumulator_reg = sum.a == in.dst ? sum.b : sum.a;
  if (locals[accumulator_slot].tag != ValueTag::Int64 ||
      sum.op != ir::Op::Add || sum.dst >= regs.size() ||
      (sum.a != in.dst && sum.b != in.dst) || accumulator_reg >= regs.size() ||
      regs[accumulator_reg].tag != ValueTag::Int64 ||
      regs[accumulator_reg].as.i64 != locals[accumulator_slot].as.i64 ||
      store.op != ir::Op::StoreLocal || store.dst != accumulator_slot ||
      store.a != sum.dst) {
    return false;
  }
  ArgBinaryFunctionSpec spec;
  spec.lhs_arg = cache.lhs_slot;
  spec.rhs_arg = cache.rhs_slot;
  spec.op = cache.inline_op;
  spec.next_arg = cache.next_arg;
  spec.next_op = cache.next_op;
  if (cache.next_is_constant) value_assign_fast(spec.next_constant, cache.inline_const);
  spec.has_next = cache.has_next;
  spec.next_is_constant = cache.next_is_constant;
  int64_t function_value = 0;
  int64_t accumulated_value = 0;
  if (!xlang_vm_try_arg_binary_local_accumulate_int64(
          call_args, spec, locals[accumulator_slot],
          function_value, accumulated_value)) {
    return false;
  }
  // Keep this guarded, code-shape-specific work outlined so it does not add
  // more machine code to ordinary calls and arithmetic dispatch.
  value_set_int64(regs[in.dst], function_value);
  value_set_int64(locals[accumulator_slot], accumulated_value);
  ip += 2;
  return true;
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
    XlangVMInstrCacheStorage& instr_cache,
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
    RaiseExceptionValue&& raise_exception_value,
    const Value* monitoring_code = nullptr, size_t vm_frame_count = 0) {
  if (in.b >= fn.names.size() || in.c >= fn.call_args.size()) {
    result.errors.push_back("invalid method call");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& name = fn.names[in.b];
  const auto& call_arg_regs = fn.call_args[in.c];

  CallArgsView call_args;
  call_args.registers = regs.value_data();
  call_args.register_args = &call_arg_regs;

  // dict.get() is one of the most frequent operations in source-backed
  // protocol and import code. For keys whose hash/equality cannot invoke
  // Python, perform the same native mapping lookup directly and avoid
  // constructing a bound method or adapting arguments through a callback.
  constexpr int64_t kObservableNativeCallEvents =
      kSysMonitoringEventCall | kSysMonitoringEventCReturn | kSysMonitoringEventCRaise;
  if (!runtime.profile_event_may_dispatch() &&
      !sys_monitoring_event_may_dispatch(kObservableNativeCallEvents) &&
      name == "get" && value_as_dict(regs[in.a]) != nullptr &&
      (call_arg_regs.size() == 1 || call_arg_regs.size() == 2)) {
    const Value& key = regs[call_arg_regs[0]];
    if (key.tag == ValueTag::Int64 || value_as_string(key) != nullptr) {
      const auto* dict = value_as_dict(regs[in.a]);
      // The query and every stored key must be callback-free. Numeric index
      // membership alone is insufficient: an int-like Instance can override
      // __eq__. Unknown/mixed key sets use the ordinary runtime method before
      // any raw lookup, preserving equality calls even for exact str/int keys.
      const bool exact_string_keys =
          dict->indexed_entry_count == dict->entries.size() &&
          !dict->index_has_non_string_keys;
      const bool intrinsic_keys = dict->intrinsic_hash_keys_only &&
          dict->intrinsic_hash_checked_entry_count == dict->entries.size() &&
          dict->runtime_hash_indexed_entry_count == dict->entries.size();
      if (exact_string_keys || intrinsic_keys) {
        std::string error;
        if (mapping_get_item(regs[in.a], key, regs[in.dst], error)) {
          return XlangVMOpFlow::Next;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending))
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        if (error == "key not found") {
          if (call_arg_regs.size() == 2) {
            value_assign_fast(regs[in.dst], regs[call_arg_regs[1]]);
          } else {
            value_set_none(regs[in.dst]);
          }
          return XlangVMOpFlow::Next;
        }
      }
    }
  }

  // Exact native dict.get does not read a call-site payload. Defer cache
  // indexing and core writes to the generic fallback. A previously warmed
  // mixed site keeps its CallMethod domain during dict bypasses, so frame
  // pop still releases its retained callable, constants and vector owners.
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::CallMethod);

  bool pushed_frame = false;
  // A warmed direct method site has already proved this function's positional
  // signature and exact arity. Reuse that proof to enter the ordinary Python
  // frame without rescanning parameters/defaults in push_frame; the frame is
  // still created normally, so tracing, profiling, recursion checks, and
  // traceback behavior remain on the standard path. Starred/keyword calls
  // never use this CallMethod specialization.
  auto push_exact_positional_method_frame = [&](FunctionObject* function,
                                                CallArgsView values,
                                                uint32_t expected_arg_count,
                                                bool& handled) -> XlangVMOpFlow {
    handled = false;
    if (function == nullptr || values.has_keywords() || values.has_expansion() ||
        values.size() != expected_arg_count) {
      return XlangVMOpFlow::ContinueLoop;
    }
    const ir::Module* call_module = &module;
    auto call_module_owner = module_owner;
    if (function->module != nullptr) {
      call_module = function->module.get();
      call_module_owner = function->module;
    }
    if (function->function_id >= call_module->functions.size() ||
        call_module->functions[function->function_id].is_generator) {
      return XlangVMOpFlow::ContinueLoop;
    }
    ++ip;
    handled = true;
    if (!push_frame(*call_module, function->function_id, values.with_keyword_defaults(*function), function->closure,
                    function->defaults, function->globals_module,
                    std::move(call_module_owner), in.dst,
                    FrameReturnMode::StoreReturnValue, Value::invalid(), true, in.a)) {
      return result.errors.empty() ? XlangVMOpFlow::ContinueLoop
                                   : XlangVMOpFlow::ReturnResult;
    }
    pushed_frame = true;
    return XlangVMOpFlow::SwitchFrame;
  };
  auto has_exact_positional_method_signature = [&](FunctionObject* function,
                                                    uint32_t arg_count) -> bool {
    if (function == nullptr) return false;
    const ir::Module* call_module = function->module != nullptr
        ? function->module.get() : &module;
    if (function->function_id >= call_module->functions.size()) return false;
    const auto& call_function = call_module->functions[function->function_id];
    return !call_function.is_generator &&
        xlang_vm_has_direct_positional_signature(call_function, arg_count);
  };
  const bool allow_inline_calls = inline_calls_allowed(runtime);

  if (auto* instance = value_as_instance(regs[in.a])) {
    auto* klass = value_as_class(instance->klass);
    if (klass && klass->has_getattribute_hook) {
      Value hook;
      std::string error;
      if (object_get_class_attr_for_instance(regs[in.a], "__getattribute__", hook, error) &&
          !xlang_vm_is_default_object_hook(hook, "object.__getattribute__")) {
        // Dynamic lookup can return a new callable on every access; do not cache it.
        const Value* getattr_builtin = runtime.find_builtin("getattr");
        if (getattr_builtin == nullptr) {
          return raise_runtime_error("getattr builtin is unavailable")
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        Value lookup_args[] = {regs[in.a], Value::string(name)};
        Value callable;
        std::vector<Value> arguments;
        arguments.reserve(call_arg_regs.size());
        for (auto index : call_arg_regs) arguments.push_back(regs[index]);
        if (!runtime_call_callable(runtime, *getattr_builtin, lookup_args, 2, callable, error) ||
            !runtime_call_callable(runtime, callable, arguments.data(),
                static_cast<uint32_t>(arguments.size()), regs[in.dst], error)) {
          Value pending;
          const bool handled = runtime.take_pending_exception(pending)
              ? raise_exception_value(std::move(pending)) : raise_runtime_error(error);
          return handled ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
    }
  }

  if (auto* receiver_class = value_as_class(regs[in.a])) {
    auto* metaclass = value_as_class(receiver_class->metaclass);
    if (metaclass != nullptr && metaclass->has_getattribute_hook) {
      const Value* getattr_builtin = runtime.find_builtin("getattr");
      if (getattr_builtin == nullptr) {
        return raise_runtime_error("getattr builtin is unavailable")
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      Value lookup_args[] = {regs[in.a], Value::string(name)};
      Value callable;
      std::string error;
      std::vector<Value> arguments;
      arguments.reserve(call_arg_regs.size());
      for (auto index : call_arg_regs) arguments.push_back(regs[index]);
      if (!runtime_call_callable(runtime, *getattr_builtin, lookup_args, 2, callable, error) ||
          !runtime_call_callable(runtime, callable, arguments.data(),
              static_cast<uint32_t>(arguments.size()), regs[in.dst], error)) {
        Value pending;
        const bool handled = runtime.take_pending_exception(pending)
            ? raise_exception_value(std::move(pending)) : raise_runtime_error(error);
        return handled ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return XlangVMOpFlow::Next;
    }

    // Hot classmethod comparators such as DeltaBlue's Strength.stronger and
    // Strength.weaker otherwise allocate a BoundMethod and push a Python frame
    // for two attribute loads and one integer comparison. Cache only a
    // verified classmethod on the exact builtin `type` metaclass, and keep
    // normal lookup as the fallback for every dynamic or observable case.
    if (metaclass != nullptr && call_arg_regs.size() == 2 &&
        !call_args.has_keywords() && !call_args.has_expansion() &&
        !instr_cache.empty()) {
      auto& cache = instr_cache[ip].call;
      const bool cached_classmethod_site =
          cache.kind == CallSiteKind::InlineClassMethodAttrIntCompare &&
          cache.callee_object == &receiver_class->header &&
          cache.class_version == receiver_class->version &&
          cache.arg0_object == &metaclass->header &&
          cache.secondary_class_version == metaclass->version &&
          cache.function != nullptr &&
          cache.function_code_version == cache.function->code_version;
      if (cached_classmethod_site && cache.function != nullptr &&
          inline_python_function_allowed(runtime, module, *cache.function)) {
        const bool output_overwrite_cannot_finalize =
            regs[in.dst].tag != ValueTag::Object;
        if (xlang_vm_execute_classmethod_attr_int_compare(
                *cache.function, module, regs[in.a], call_args,
                cache.lhs_slot,
                static_cast<ir::CompareOp>(cache.fast_method_id),
                cache.inline_slots[0], cache.inline_slots[1], regs[in.dst])) {
          return output_overwrite_cannot_finalize
              ? XlangVMOpFlow::NextNoMonitoringRefresh
              : XlangVMOpFlow::Next;
        }
      }

      // Install only after proving class attribute resolution and the method
      // body shape. Class and metaclass versions invalidate both lookup and
      // data-descriptor precedence; the retained class prevents pointer reuse
      // from making a stale function cache appear to belong to a new class.
      const Value* builtin_type = runtime.find_builtin("type");
      if (builtin_type != nullptr && value_as_class(*builtin_type) == metaclass &&
          !cached_classmethod_site) {
        Value meta_descriptor;
        std::string meta_error;
        const bool has_meta_descriptor = object_lookup_class_attr(
            receiver_class->metaclass, name, meta_descriptor, meta_error);
        if (meta_error.empty() &&
            (!has_meta_descriptor || !object_value_is_data_descriptor(meta_descriptor))) {
          Value raw_method;
          std::string class_lookup_error;
          if (object_lookup_class_attr(regs[in.a], name, raw_method, class_lookup_error) &&
              class_lookup_error.empty()) {
            auto* class_method = value_as_class_method(raw_method);
            auto* function = class_method != nullptr
                ? value_as_function(class_method->function) : nullptr;
            XlangVMClassMethodAttrIntCompareSpec spec;
            if (function != nullptr && inline_python_function_allowed(runtime, module, *function) &&
                xlang_vm_analyze_classmethod_attr_int_compare(module, *function, spec)) {
              uint32_t lhs_slot = 0;
              uint32_t rhs_slot = 0;
              if (xlang_vm_prepare_classmethod_attr_int_compare(
                      *function, module, spec, regs[in.a], call_args,
                      lhs_slot, rhs_slot)) {
                const bool output_overwrite_cannot_finalize =
                    regs[in.dst].tag != ValueTag::Object;
                if (xlang_vm_execute_classmethod_attr_int_compare(
                        *function, module, regs[in.a], call_args,
                        spec.attribute_name, spec.compare, lhs_slot, rhs_slot,
                        regs[in.dst])) {
                  cache = CallSiteCache{};
                  value_assign_fast(cache.retained_callee, regs[in.a]);
                  cache.callee_object = &receiver_class->header;
                  cache.arg0_object = &metaclass->header;
                  cache.kind = CallSiteKind::InlineClassMethodAttrIntCompare;
                  cache.function = function;
                  cache.function_code_version = function->code_version;
                  cache.inline_function_id = UINT32_MAX;
                  cache.class_version = receiver_class->version;
                  cache.secondary_class_version = metaclass->version;
                  cache.lhs_slot = spec.attribute_name;
                  cache.fast_method_id = static_cast<uint32_t>(spec.compare);
                  cache.inline_slots[0] = lhs_slot;
                  cache.inline_slots[1] = rhs_slot;
                  return output_overwrite_cannot_finalize
                      ? XlangVMOpFlow::NextNoMonitoringRefresh
                      : XlangVMOpFlow::Next;
                }
              }
            }
          }
        }
      }
    }
  }

  const bool receiver_is_super = value_as_super(regs[in.a]) != nullptr;
  if (receiver_is_super && call_args.leading_count == 0 &&
      !call_args.has_keywords() && !call_args.has_expansion()) {
    Value super_method;
    Value super_receiver;
    if (object_get_super_method_for_call(regs[in.a], name, super_method, super_receiver)) {
      // Immediate super.method(...) needs a callable and receiver, not a heap
      // BoundMethod. Resolve the current MRO on every call, retain both values,
      // and use normal call dispatch so Python frames, hooks and errors remain.
      // Descriptors, class/static methods and __new__ keep ordinary lookup.
      CallArgsView bound_args = call_args;
      bound_args.leading = &super_receiver;
      bound_args.leading_count = 1;
      if (!xlang3::xlang_vm::ops::call_callable_value(
              runtime, super_method, bound_args, module, module_owner, in.dst, ip,
              native_call_args, execution_lock, regs[in.dst], pushed_frame,
              make_generator_if_needed, push_frame, raise_runtime_error, raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
    }
  }
  bool receiver_has_direct_method_attr = false;
  if (auto* instance = value_as_instance(regs[in.a])) {
    auto* klass = value_as_class(instance->klass);
    if (instance->native_get_attr != nullptr) {
      receiver_has_direct_method_attr = true;
    } else {
      if (klass != nullptr) {
        auto slot_it = klass->instance_slot_indices.find(name);
        if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance) &&
            instance_slot_at(instance, slot_it->second).tag != ValueTag::Invalid) {
          receiver_has_direct_method_attr = true;
        }
      }
      for (const auto& attr : instance->attrs) {
        if (attr.first == name) {
          receiver_has_direct_method_attr = true;
          break;
        }
      }
      if (!receiver_has_direct_method_attr &&
          value_as_dict(instance_attribute_storage(*instance)) != nullptr) {
        Value dictionary_method;
        std::string ignored_error;
        receiver_has_direct_method_attr = mapping_get_item(
            instance_attribute_storage(*instance), Value::string(name),
            dictionary_method, ignored_error);
      }
    }
  }

  if (!receiver_has_direct_method_attr &&
      !receiver_is_super &&
      !instr_cache.empty() &&
      regs[in.a].tag == ValueTag::Object &&
      regs[in.a].as.obj != nullptr) {
    auto& cache = instr_cache[ip].call;
    auto* cached_receiver_class = value_as_class(regs[in.a]);
    const bool cached_bound_native_matches =
        cached_receiver_class != nullptr
            ? cache.callee_object == &cached_receiver_class->header &&
                  cache.class_version == cached_receiver_class->version
            : cache.callee_object == nullptr &&
                  cache.class_version == static_cast<uint64_t>(regs[in.a].as.obj->kind);
    if (cache.kind == CallSiteKind::BoundNativeFunction && cached_bound_native_matches) {
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
              raise_exception_value, monitoring_code, static_cast<int64_t>(ip))) {
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
        if (cache.callee_object == &klass->header && cache.class_version == klass->version &&
            (cache.function == nullptr || cache.function_code_version == cache.function->code_version)) {
          if ((cache.kind == CallSiteKind::UserFunction ||
               cache.kind == CallSiteKind::ExactPositionalFunction) &&
              cache.function != nullptr && call_arg_regs.size() == 1 &&
              !call_args.has_keywords() && !call_args.has_expansion()) {
            Value planned_result;
            if (xlang_vm_try_two_argument_double_method(runtime, module,
                    *cache.function, regs[in.a], regs[call_arg_regs[0]], planned_result, vm_frame_count)) {
              value_assign_fast(regs[in.dst], planned_result);
              return XlangVMOpFlow::Next;
            }
          }
          if (cache.kind == CallSiteKind::ExactPositionalFunction) {
            bool handled = false;
            const auto flow = push_exact_positional_method_frame(
                cache.function, method_args, cache.lhs_slot, handled);
            if (handled) return flow;
          }
          if (cache.kind == CallSiteKind::UserFunction ||
              cache.kind == CallSiteKind::ExactPositionalFunction) {
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
          const bool allow_cached_python_inline = cache.function != nullptr &&
          cache.function_code_version == cache.function->code_version &&
              inline_python_function_allowed(runtime, module, *cache.function);
          if (allow_cached_python_inline &&
              cache.kind == CallSiteKind::InlineSelfSlotNormalizeMethod &&
              call_arg_regs.empty() && !call_args.has_keywords() &&
              !call_args.has_expansion() &&
              execute_self_slot_normalize_method(
                  regs[in.a], cache.inline_slots, *cache.function,
                  cache.inline_globals_module, cache.inline_globals_version,
                  cache.inline_const, regs[in.dst])) {
            return XlangVMOpFlow::Next;
          }
          if (allow_cached_python_inline &&
              cache.kind == CallSiteKind::InlineSelfSlotMaximizeMethod &&
              call_arg_regs.size() == 1 && !call_args.has_keywords() &&
              !call_args.has_expansion() &&
              execute_self_slot_maximize_method(
                  regs[in.a], regs[call_arg_regs[0]], cache.inline_slots,
                  regs[in.dst])) {
            return XlangVMOpFlow::Next;
          }
          if (allow_cached_python_inline &&
              cache.kind == CallSiteKind::InlineSelfAttrBooleanExprMethod &&
              call_arg_regs.empty()) {
            const bool output_overwrite_cannot_finalize =
                regs[in.dst].tag != ValueTag::Object;
            if (xlang_vm_execute_self_attr_boolean_expr_method(
                    module, *cache.function, *instance, cache.inline_slots,
                    static_cast<uint8_t>(cache.fast_method_id), regs[in.dst])) {
              return output_overwrite_cannot_finalize
                  ? XlangVMOpFlow::NextNoMonitoringRefresh
                  : XlangVMOpFlow::Next;
            }
          }
          if (allow_cached_python_inline &&
              cache.kind == CallSiteKind::InlineSelfAttrBinaryMethod &&
              call_arg_regs.empty()) {
            const bool output_overwrite_cannot_finalize =
                regs[in.dst].tag != ValueTag::Object;
            std::string error;
            if (xlang_vm_execute_self_attr_binary_method(
                    module, *cache.function, *instance, cache.lhs_slot,
                    cache.rhs_slot, cache.inline_op, regs[in.dst], error)) {
              return output_overwrite_cannot_finalize
                  ? XlangVMOpFlow::NextNoMonitoringRefresh
                  : XlangVMOpFlow::Next;
            }
            if (!error.empty()) {
              return raise_runtime_error(error)
                  ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
            }
          }
          if (allow_cached_python_inline && cache.kind == CallSiteKind::InlineSelfBinaryMethod && call_arg_regs.empty()) {
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
          if (allow_cached_python_inline && cache.kind == CallSiteKind::InlineConstMethod &&
              call_arg_regs.size() == cache.lhs_slot && !call_args.has_keywords() &&
              !call_args.has_expansion()) {
            value_assign_fast(regs[in.dst], cache.inline_const);
            return XlangVMOpFlow::Next;
          }
          if (allow_cached_python_inline && cache.kind == CallSiteKind::InlineSelfSlotConstSumMethod && call_arg_regs.empty()) {
            std::string error;
            if (!execute_self_slot_const_sum_method_fn(*instance, cache.lhs_slot, cache.inline_const, regs[in.dst], error)) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
          if (allow_cached_python_inline && cache.kind == CallSiteKind::InlineSelfSlotMethod && call_arg_regs.empty()) {
            std::string error;
            if (!execute_self_slot_method_fn(*instance, cache.lhs_slot, regs[in.dst], error)) {
              if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
              return XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
          if (allow_cached_python_inline && cache.kind == CallSiteKind::InlineSmallSelfMethod && call_arg_regs.empty()) {
            bool supported = false;
            std::string error;
            if (!execute_inline_small_self_method(module, *cache.function, regs[in.a], regs[in.dst], supported, error)) {
              // A speculative inline execution can encounter an operation that
              // must raise a Python exception. Re-run the original function so
              // exception type, chaining, and traceback frames stay observable.
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
          if (inline_python_function_allowed(runtime, module, *fn_obj) &&
              !call_args.has_keywords() && !call_args.has_expansion() &&
              analyze_const_method_with_args(
                  module, *fn_obj, static_cast<uint32_t>(call_arg_regs.size()), const_value)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.callee_object = &klass->header;
              cache.kind = CallSiteKind::InlineConstMethod;
              cache.function = fn_obj;
              cache.function_code_version = fn_obj->code_version;
              cache.inline_function_id = UINT32_MAX;
              cache.native = nullptr;
              cache.class_version = klass->version;
              cache.lhs_slot = static_cast<uint32_t>(call_arg_regs.size());
              value_assign_fast(cache.inline_const, const_value);
            }
            value_assign_fast(regs[in.dst], const_value);
            return XlangVMOpFlow::Next;
          }
          XlangVMSelfAttrBinaryMethodSpec attr_inline_spec;
          XlangVMSelfAttrBooleanExprMethodSpec bool_expr_spec;
          if (inline_python_function_allowed(runtime, module, *fn_obj) &&
              call_arg_regs.empty() &&
              xlang_vm_analyze_self_attr_boolean_expr_method(
                  module, *fn_obj, bool_expr_spec)) {
            std::array<uint32_t, 3> slots{};
            const bool output_overwrite_cannot_finalize =
                regs[in.dst].tag != ValueTag::Object;
            if (xlang_vm_prepare_self_attr_boolean_expr_method(
                    module, *fn_obj, *instance, bool_expr_spec, slots) &&
                xlang_vm_execute_self_attr_boolean_expr_method(
                    module, *fn_obj, *instance, slots, bool_expr_spec.expression,
                    regs[in.dst])) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.callee_object = &klass->header;
                cache.kind = CallSiteKind::InlineSelfAttrBooleanExprMethod;
                cache.function = fn_obj;
                cache.function_code_version = fn_obj->code_version;
                cache.inline_function_id = UINT32_MAX;
                cache.native = nullptr;
                cache.class_version = klass->version;
                cache.inline_slots = slots;
                cache.fast_method_id = bool_expr_spec.expression;
              }
              return output_overwrite_cannot_finalize
                  ? XlangVMOpFlow::NextNoMonitoringRefresh
                  : XlangVMOpFlow::Next;
            }
          }
          if (inline_python_function_allowed(runtime, module, *fn_obj) && call_arg_regs.empty() &&
              xlang_vm_analyze_self_attr_binary_method(module, *fn_obj, attr_inline_spec)) {
            uint32_t lhs_attr = 0;
            uint32_t rhs_attr = 0;
            if (xlang_vm_prepare_self_attr_binary_method(
                    module, *fn_obj, *instance, attr_inline_spec, lhs_attr, rhs_attr)) {
              const bool output_overwrite_cannot_finalize =
                  regs[in.dst].tag != ValueTag::Object;
              std::string error;
              if (xlang_vm_execute_self_attr_binary_method(
                      module, *fn_obj, *instance, lhs_attr, rhs_attr,
                      attr_inline_spec.op, regs[in.dst], error)) {
                if (!instr_cache.empty()) {
                  auto& cache = instr_cache[ip].call;
                  cache.callee_object = &klass->header;
                  cache.kind = CallSiteKind::InlineSelfAttrBinaryMethod;
                  cache.function = fn_obj;
                  cache.function_code_version = fn_obj->code_version;
                  cache.inline_function_id = UINT32_MAX;
                  cache.native = nullptr;
                  cache.class_version = klass->version;
                  cache.lhs_slot = lhs_attr;
                  cache.rhs_slot = rhs_attr;
                  cache.inline_op = attr_inline_spec.op;
                }
                return output_overwrite_cannot_finalize
                    ? XlangVMOpFlow::NextNoMonitoringRefresh
                    : XlangVMOpFlow::Next;
              }
              if (!error.empty()) {
                return raise_runtime_error(error)
                    ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
              }
            }
          }
          SelfBinaryMethodSpec inline_spec;
          if (inline_python_function_allowed(runtime, module, *fn_obj) && call_arg_regs.empty() && analyze_self_binary_method_fn(module, *fn_obj, inline_spec)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.callee_object = &klass->header;
              cache.kind = CallSiteKind::InlineSelfBinaryMethod;
              cache.function = fn_obj;
              cache.function_code_version = fn_obj->code_version;
              cache.inline_function_id = UINT32_MAX;
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
          if (inline_python_function_allowed(runtime, module, *fn_obj) &&
              call_arg_regs.size() == 1 && !call_args.has_keywords() &&
              !call_args.has_expansion()) {
            SelfSlotMaximizeMethodSpec maximize_spec;
            std::array<uint32_t, 3> slots{};
            if (analyze_self_slot_maximize_method(module, *fn_obj, maximize_spec) &&
                prepare_self_slot_maximize_method(
                    module, *fn_obj, *instance, maximize_spec, slots) &&
                execute_self_slot_maximize_method(
                    regs[in.a], regs[call_arg_regs[0]], slots, regs[in.dst])) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.callee_object = &klass->header;
                cache.kind = CallSiteKind::InlineSelfSlotMaximizeMethod;
                cache.function = fn_obj;
                cache.function_code_version = fn_obj->code_version;
                cache.inline_function_id = UINT32_MAX;
                cache.native = nullptr;
                cache.class_version = klass->version;
                cache.inline_slots = slots;
              }
              return XlangVMOpFlow::Next;
            }
          }
          if (inline_python_function_allowed(runtime, module, *fn_obj) &&
              call_arg_regs.empty() && !call_args.has_keywords() &&
              !call_args.has_expansion()) {
            SelfSlotNormalizeMethodSpec normalize_spec;
            std::array<uint32_t, 3> slots{};
            ModuleObject* globals_module = nullptr;
            Value expected_sqrt = Value::invalid();
            if (analyze_self_slot_normalize_method(
                    module, *fn_obj, normalize_spec) &&
                prepare_self_slot_normalize_method(
                    runtime, module, *fn_obj, *instance, normalize_spec,
                    slots, globals_module, expected_sqrt) &&
                execute_self_slot_normalize_method(
                    regs[in.a], slots, *fn_obj, globals_module,
                    globals_module->version, expected_sqrt, regs[in.dst])) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.callee_object = &klass->header;
                cache.kind = CallSiteKind::InlineSelfSlotNormalizeMethod;
                cache.function = fn_obj;
                cache.function_code_version = fn_obj->code_version;
                cache.inline_function_id = UINT32_MAX;
                cache.native = nullptr;
                cache.class_version = klass->version;
                cache.inline_slots = slots;
                cache.inline_globals_module = globals_module;
                cache.inline_globals_version = globals_module->version;
                value_assign_fast(cache.inline_const, expected_sqrt);
              }
              return XlangVMOpFlow::Next;
            }
          }
          if (!instr_cache.empty()) {
            auto& cache = instr_cache[ip].call;
            cache.callee_object = &klass->header;
            cache.kind = CallSiteKind::UserFunction;
            cache.function = fn_obj;
            cache.function_code_version = fn_obj->code_version;
            cache.inline_function_id = UINT32_MAX;
            cache.native = nullptr;
            cache.class_version = klass->version;
          }
          if (call_arg_regs.size() == 1 && !call_args.has_keywords() &&
              !call_args.has_expansion()) {
            Value planned_result;
            if (xlang_vm_try_two_argument_double_method(runtime, module,
                    *fn_obj, regs[in.a], regs[call_arg_regs[0]], planned_result, vm_frame_count)) {
              value_assign_fast(regs[in.dst], planned_result);
              return XlangVMOpFlow::Next;
            }
          }
          if (inline_python_function_allowed(runtime, module, *fn_obj) && call_arg_regs.empty()) {
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
            // Failed speculative execution falls through to the Python frame.
          }
          const uint32_t exact_arg_count = static_cast<uint32_t>(method_args.size());
          if (!method_args.has_keywords() && !method_args.has_expansion() &&
              has_exact_positional_method_signature(fn_obj, exact_arg_count)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.kind = CallSiteKind::ExactPositionalFunction;
              cache.lhs_slot = exact_arg_count;
            }
            bool handled = false;
            const auto flow = push_exact_positional_method_frame(
                fn_obj, method_args, exact_arg_count, handled);
            if (handled) return flow;
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
          if (inline_python_function_allowed(runtime, module, *fn_obj) &&
              !call_args.has_keywords() && !call_args.has_expansion() &&
              analyze_const_method_with_args(
                  module, *fn_obj, static_cast<uint32_t>(call_arg_regs.size()), const_value)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.callee_object = &klass->header;
              cache.kind = CallSiteKind::InlineConstMethod;
              cache.function = fn_obj;
              cache.function_code_version = fn_obj->code_version;
              cache.inline_function_id = UINT32_MAX;
              cache.native = nullptr;
              cache.class_version = klass->version;
              cache.lhs_slot = static_cast<uint32_t>(call_arg_regs.size());
              value_assign_fast(cache.inline_const, const_value);
            }
            value_assign_fast(regs[in.dst], const_value);
            return XlangVMOpFlow::Next;
          }
          XlangVMSelfAttrBooleanExprMethodSpec inherited_bool_expr_spec;
          if (inline_python_function_allowed(runtime, module, *fn_obj) &&
              call_arg_regs.empty() &&
              xlang_vm_analyze_self_attr_boolean_expr_method(
                  module, *fn_obj, inherited_bool_expr_spec)) {
            std::array<uint32_t, 3> slots{};
            const bool output_overwrite_cannot_finalize =
                regs[in.dst].tag != ValueTag::Object;
            if (xlang_vm_prepare_self_attr_boolean_expr_method(
                    module, *fn_obj, *instance, inherited_bool_expr_spec, slots) &&
                xlang_vm_execute_self_attr_boolean_expr_method(
                    module, *fn_obj, *instance, slots,
                    inherited_bool_expr_spec.expression, regs[in.dst])) {
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.callee_object = &klass->header;
                cache.kind = CallSiteKind::InlineSelfAttrBooleanExprMethod;
                cache.function = fn_obj;
                cache.function_code_version = fn_obj->code_version;
                cache.inline_function_id = UINT32_MAX;
                cache.native = nullptr;
                cache.class_version = klass->version;
                cache.inline_slots = slots;
                cache.fast_method_id = inherited_bool_expr_spec.expression;
              }
              return output_overwrite_cannot_finalize
                  ? XlangVMOpFlow::NextNoMonitoringRefresh
                  : XlangVMOpFlow::Next;
            }
          }
          SelfBinaryMethodSpec inline_spec;
          if (inline_python_function_allowed(runtime, module, *fn_obj) && call_arg_regs.empty() && analyze_self_binary_method_fn(module, *fn_obj, inline_spec)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.callee_object = &klass->header;
              cache.kind = CallSiteKind::InlineSelfBinaryMethod;
              cache.function = fn_obj;
              cache.function_code_version = fn_obj->code_version;
              cache.inline_function_id = UINT32_MAX;
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
            cache.function_code_version = fn_obj->code_version;
            cache.inline_function_id = UINT32_MAX;
            cache.native = nullptr;
            cache.class_version = klass->version;
          }
          if (call_arg_regs.size() == 1 && !call_args.has_keywords() &&
              !call_args.has_expansion()) {
            Value planned_result;
            if (xlang_vm_try_two_argument_double_method(runtime, module,
                    *fn_obj, regs[in.a], regs[call_arg_regs[0]], planned_result, vm_frame_count)) {
              value_assign_fast(regs[in.dst], planned_result);
              return XlangVMOpFlow::Next;
            }
          }
          if (inline_python_function_allowed(runtime, module, *fn_obj) && call_arg_regs.empty()) {
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
            // Failed speculative execution falls through to the Python frame.
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
            raise_exception_value, monitoring_code, static_cast<int64_t>(ip))) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    return XlangVMOpFlow::Next;
  }

  Value method;
  std::string attr_error;
  if (!attribute_get(regs[in.a], name, method, attr_error)) {
    if (auto* instance = value_as_instance(regs[in.a])) {
      auto* klass = value_as_class(instance->klass);
      Value hook;
      std::string hook_error;
      if (klass != nullptr &&
          object_get_class_attr_for_instance(regs[in.a], "__getattr__", hook, hook_error)) {
        Value lookup_args[] = {regs[in.a], Value::string(name)};
        Value callable;
        std::vector<Value> arguments;
        arguments.reserve(call_arg_regs.size());
        for (auto index : call_arg_regs) arguments.push_back(regs[index]);
        if (!runtime_call_callable(runtime, hook, lookup_args, 2, callable, attr_error) ||
            !runtime_call_callable(runtime, callable, arguments.data(),
                static_cast<uint32_t>(arguments.size()), regs[in.dst], attr_error)) {
          Value pending;
          const bool handled = runtime.take_pending_exception(pending)
              ? raise_exception_value(std::move(pending)) : raise_runtime_error(attr_error);
          return handled ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
    }
    // PEP 562 module exports (for example asyncio's deprecated Windows
    // event-loop policy aliases) are resolved by module-level __getattr__.
    // A fused positional CallMethod must preserve that lookup just as LoadAttr
    // and CallMethodEx do; keep it on the miss path so indexed module hits
    // retain their direct fast path. Do not cache the result: module hooks may
    // intentionally return a fresh value on each access.
    if (value_as_module(regs[in.a]) != nullptr) {
      Value module_getattr;
      std::string getattr_error;
      if (module_get_attr(regs[in.a], "__getattr__", module_getattr,
                          getattr_error)) {
        Value attr_arg = Value::string(name);
        Value resolved;
        if (!runtime_call_callable(runtime, module_getattr, &attr_arg, 1,
                                   resolved, attr_error)) {
          Value pending;
          const bool handled = runtime.take_pending_exception(pending)
              ? raise_exception_value(std::move(pending))
              : raise_runtime_error(attr_error);
          return handled ? XlangVMOpFlow::ContinueLoop
                         : XlangVMOpFlow::ReturnResult;
        }
        if (!xlang3::xlang_vm::ops::call_callable_value(
                runtime, resolved, call_args, module, module_owner, in.dst, ip,
                native_call_args, execution_lock, regs[in.dst], pushed_frame,
                make_generator_if_needed, push_frame, raise_runtime_error,
                raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        return pushed_frame ? XlangVMOpFlow::SwitchFrame
                            : XlangVMOpFlow::Next;
      }
    }
    return raise_exception_value(runtime.make_exception("AttributeError", attr_error))
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }

  // CallMethod normally binds plain functions itself, but arbitrary
  // descriptors must run __get__ before the returned object is called.  The
  // LoadAttr path already does this; keep the fused attribute-call opcode
  // equivalent for descriptors such as Python 3.14's classonly helper.
  if (!receiver_has_direct_method_attr && object_value_has_descriptor_get(method) &&
      value_as_property(method) == nullptr) {
    Value descriptor_receiver;
    Value descriptor_owner;
    bool resolve_descriptor = false;
    if (auto* instance = value_as_instance(regs[in.a])) {
      value_assign_fast(descriptor_receiver, regs[in.a]);
      value_assign_fast(descriptor_owner, instance->klass);
      resolve_descriptor = true;
    } else if (value_as_class(regs[in.a]) != nullptr) {
      descriptor_receiver = Value::none();
      value_assign_fast(descriptor_owner, regs[in.a]);
      resolve_descriptor = true;
    }
    if (resolve_descriptor) {
      Value get_method;
      if (!object_get_attr(method, "__get__", get_method, attr_error)) {
        return raise_runtime_error(attr_error)
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      Value descriptor_args[] = {descriptor_receiver, descriptor_owner};
      Value resolved;
      if (!runtime_call_callable(runtime, get_method, descriptor_args, 2, resolved, attr_error)) {
        Value pending;
        const bool handled = runtime.take_pending_exception(pending)
            ? raise_exception_value(std::move(pending)) : raise_runtime_error(attr_error);
        return handled ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      method = std::move(resolved);
    }
  }

  // Positional obj.property(...) uses CallMethod rather than LoadAttr + Call.
  // Resolve the property first, without binding self to the returned callable
  // or caching a value that may differ across receivers/accesses.
  bool resolved_property = false;
  if (auto* property = value_as_property(method); property && value_as_instance(regs[in.a])) {
    Value callable;
    if (!runtime_call_callable(runtime, property->fget, &regs[in.a], 1, callable, attr_error)) {
      Value pending;
      const bool handled = runtime.take_pending_exception(pending)
          ? raise_exception_value(std::move(pending)) : raise_runtime_error(attr_error);
      return handled ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    method = std::move(callable);
    resolved_property = true;
  }

  if (auto* bound = value_as_bound_method(method)) {
    CallArgsView bound_args = call_args;
    bound_args.leading = &bound->self;
    bound_args.leading_count = 1;
    if (auto* native = value_as_native_function(bound->function)) {
      if (!resolved_property && !receiver_is_super && value_is(bound->self, regs[in.a]) &&
          !instr_cache.empty() && regs[in.a].tag == ValueTag::Object && regs[in.a].as.obj != nullptr) {
        auto& cache = instr_cache[ip].call;
        if (auto* receiver_class = value_as_class(regs[in.a])) {
          cache.callee_object = &receiver_class->header;
          cache.class_version = receiver_class->version;
        } else {
          cache.callee_object = nullptr;
          cache.class_version = static_cast<uint64_t>(regs[in.a].as.obj->kind);
        }
        cache.kind = CallSiteKind::BoundNativeFunction;
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
      // A classmethod may wrap any callable, including an instance with
      // __call__. Keep the fused attribute-call path equivalent to calling
      // the bound method returned by normal attribute lookup.
      if (!xlang3::xlang_vm::ops::call_callable_value(
              runtime, bound->function, bound_args, module, module_owner,
              in.dst, ip, native_call_args, execution_lock, regs[in.dst],
              pushed_frame, make_generator_if_needed, push_frame,
              raise_runtime_error, raise_exception_value)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
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
    if (!object_get_special_method(runtime, method, "__call__", call_attr, call_error)) {
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
    if (auto* metaclass = value_as_class(klass->metaclass);
        metaclass != nullptr && metaclass->name != "type") {
      Value meta_call;
      std::string meta_call_error;
      if (class_get_bound_attr(
              runtime, klass->metaclass, method, "__call__", meta_call, meta_call_error)) {
        CallArgsView meta_call_args = call_args;
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
    Value new_callable;
    if (xlang_vm_resolve_class_new_callable(runtime, method, klass, new_callable)) {
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
    XlangVMBuiltinConstructorError constructor_error;
    if (call_builtin_type_constructor_fn(runtime, *klass, call_args, execution_lock, regs[in.dst], constructor_error)) {
      if (constructor_error.fully_handled) {
        return finish_fully_handled_native_constructor(
            runtime, constructor_error, raise_exception_value);
      }
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
    Value pending_constructor_exception;
    if (runtime.take_pending_exception(pending_constructor_exception)) {
      return raise_exception_value(std::move(pending_constructor_exception))
          ? XlangVMOpFlow::ContinueLoop
          : XlangVMOpFlow::ReturnResult;
    }
    if (!constructor_error.message.empty()) {
      return raise_exception_value(runtime.make_exception(constructor_error.type, constructor_error.message))
          ? XlangVMOpFlow::ContinueLoop
          : XlangVMOpFlow::ReturnResult;
    }
    Value instance = Value::instance(method);
    initialize_exception_call_args(runtime, instance, call_args);
    CallArgsView init_args = call_args;
    init_args.leading = &instance;
    init_args.leading_count = 1;
    Value bound_init;
    std::string bound_init_error;
    if (class_get_bound_attr(runtime, method, instance, "__init__", bound_init, bound_init_error)) {
      std::vector<NativeKeywordArg> init_keyword_args;
      bool init_has_keywords = false;
      std::string init_materialize_error;
      const Value* positional = materialize_native_call_ex(
          runtime, call_args, native_call_args, init_keyword_args,
          init_has_keywords, init_materialize_error);
      if (positional == nullptr &&
          (!native_call_args.empty() || !init_materialize_error.empty())) {
        return raise_runtime_error(init_materialize_error.empty()
                                       ? "__init__ argument expansion failed"
                                       : init_materialize_error)
            ? XlangVMOpFlow::ContinueLoop
            : XlangVMOpFlow::ReturnResult;
      }
      std::vector<std::pair<std::string, Value>> init_kwargs;
      init_kwargs.reserve(init_keyword_args.size());
      for (const auto& keyword : init_keyword_args)
        init_kwargs.emplace_back(keyword.name, *keyword.value);
      Value ignored;
      execution_lock.unlock();
      const bool init_ok = runtime_call_callable_kw(
          runtime,
          bound_init,
          positional,
          static_cast<uint32_t>(native_call_args.size()),
          init_kwargs,
          ignored,
          bound_init_error);
      execution_lock.lock();
      if (!init_ok) {
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending))
              ? XlangVMOpFlow::ContinueLoop
              : XlangVMOpFlow::ReturnResult;
        }
        return raise_runtime_error(bound_init_error.empty() ? "__init__ failed" : bound_init_error)
            ? XlangVMOpFlow::ContinueLoop
            : XlangVMOpFlow::ReturnResult;
      }
      value_assign_fast(regs[in.dst], instance);
      return XlangVMOpFlow::Next;
    }
    Value init_value;
    std::string init_error;
    if (xlang_vm_get_init_attr(method, init_value, init_error) && init_value.tag != ValueTag::Invalid) {
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
        if (!push_frame(*call_module, init_fn->function_id, init_args.with_keyword_defaults(*init_fn), init_fn->closure, init_fn->defaults, init_fn->globals_module,
                        std::move(call_module_owner), in.dst,
                        FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
          return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
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
  const bool has_positional_expansion =
      original_args.star_arg != UINT32_MAX ||
      (original_args.star_args != nullptr &&
       !original_args.star_args->empty());
  if (metaclass == nullptr || metaclass->name == XlangVMNames::builtin_type ||
      !class_has_builtin_base_name(metaclass, XlangVMNames::builtin_type) ||
      original_args.size() != 3 || has_positional_expansion) {
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
  const bool has_positional_expansion =
      original_args.star_arg != UINT32_MAX ||
      (original_args.star_args != nullptr &&
       !original_args.star_args->empty());
  if (metaclass == nullptr || metaclass->name == XlangVMNames::builtin_type ||
      !class_has_builtin_base_name(metaclass, XlangVMNames::builtin_type) ||
      original_args.size() != 3 || has_positional_expansion) {
    return XlangVMOpFlow::Next;
  }

  Value init_value;
  std::string init_error;
  if (!xlang_vm_get_init_attr(metaclass_value, init_value, init_error) || init_value.tag == ValueTag::Invalid) {
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
    if (!push_frame(*call_module, fn_obj->function_id, init_args.with_keyword_defaults(*fn_obj), fn_obj->closure, fn_obj->defaults,
                    fn_obj->globals_module, std::move(call_module_owner), return_dst,
                    FrameReturnMode::StoreConstructedInstance, std::move(continuation))) {
      return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return XlangVMOpFlow::SwitchFrame;
  }
  return raise_runtime_error("__init__ is not callable") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError,
          typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_method_ex(
    const ir::Instr& in, const ir::Function& fn, const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    const Value& monitoring_code, Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs, XlangVMInstrCacheStorage& instr_cache,
    std::vector<Value>& native_call_args,
    size_t& ip, RuntimeResult& result, XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed, PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error, RaiseExceptionValue&& raise_exception_value) {
  if (in.a >= regs.size() || in.b >= fn.names.size() || in.c >= fn.call_specs.size()) {
    result.errors.push_back("invalid keyword method call");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& spec = fn.call_specs[in.c];
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::CallMethod);
  auto& cache = instr_cache[ip].call;
  CallArgsView args;
  args.registers = regs.value_data();
  args.register_args = &spec.positional;
  args.keyword_args = &spec.keywords;

  // Cache the function descriptor at this call site when the same ordinary
  // instance is called repeatedly. Guard instance shadowing and class version
  // on every hit so assignments preserve Python's normal method lookup. This
  // avoids repeated MRO/descriptor work in wrapper-heavy loops such as
  // json.dumps, while still entering the original Python method frame.
  if (!args.has_expansion()) {
    if (auto* instance = value_as_instance(regs[in.a]);
        instance != nullptr && instance->native_get_attr == nullptr) {
      auto* klass = value_as_class(instance->klass);
      const bool instance_shadowed = std::any_of(
          instance->attrs.begin(), instance->attrs.end(), [&](const auto& attr) {
            return attr.first == fn.names[in.b];
          });
      if (klass != nullptr && !klass->has_getattribute_hook && !instance_shadowed &&
          klass->instance_slot_indices.find(fn.names[in.b]) == klass->instance_slot_indices.end() &&
          cache.kind == CallSiteKind::UserFunction && cache.function != nullptr &&
          cache.callee_object == &klass->header && cache.class_version == klass->version &&
          cache.arg0_object == regs[in.a].as.obj) {
        // Cached CallMethodEx args contain only explicit arguments. Bind the
        // current receiver as `self` on every hit, just as descriptor lookup
        // does on the uncached path.
        CallArgsView method_args = args;
        method_args.leading = &regs[in.a];
        method_args.leading_count = 1;
        bool pushed_frame = false;
        if (!call_user_function(cache.function, method_args, module, module_owner, in.dst, ip,
                                regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
          return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
      }
    }
  }

  // Fuse attribute lookup and a keyword call: ordinary function descriptors
  // would otherwise allocate a bound-method object before every call. Keep
  // custom attribute hooks and shadowing on the general Python lookup path.
  if (!args.has_expansion()) {
    if (auto* instance = value_as_instance(regs[in.a]);
        instance != nullptr && instance->native_get_attr == nullptr) {
      auto* klass = value_as_class(instance->klass);
      bool shadowed = false;
      if (klass != nullptr && !klass->has_getattribute_hook) {
        for (const auto& attr : instance->attrs) {
          if (attr.first == fn.names[in.b]) { shadowed = true; break; }
        }
        if (!shadowed &&
            klass->instance_slot_indices.find(fn.names[in.b]) != klass->instance_slot_indices.end()) {
          shadowed = true;
        }
        if (!shadowed) {
          Value method;
          std::string lookup_error;
          if (object_get_class_attr_for_instance(regs[in.a], fn.names[in.b], method, lookup_error)) {
            if (auto* function = value_as_function(method)) {
              cache.callee_object = &klass->header;
              cache.arg0_object = regs[in.a].as.obj;
              cache.kind = CallSiteKind::UserFunction;
              cache.function = function;
              cache.function_code_version = function->code_version;
              cache.inline_function_id = UINT32_MAX;
              cache.native = nullptr;
              cache.class_version = klass->version;
              CallArgsView method_args = args;
              method_args.leading = &regs[in.a];
              method_args.leading_count = 1;
              bool pushed_frame = false;
              if (!call_user_function(function, method_args, module, module_owner, in.dst, ip,
                                      regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
                return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
              }
              return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
            }
          }
        }
      }
    }
  }
  Value method;
  std::string error;
  if (!xlang_vm_resolve_method_value(runtime, regs[in.a], fn.names[in.b], method, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    // A fused keyword call must perform the same PEP 562 lookup as LoadAttr.
    // Keep the hook on the miss path only: existing indexed/cached calls pay
    // no extra lookup, and a lazy export is recomputed on every missing access
    // rather than cached as if it were a stable module slot.
    Value module_getattr;
    std::string getattr_error;
    if (value_as_module(regs[in.a]) == nullptr ||
        !module_get_attr(regs[in.a], "__getattr__", module_getattr, getattr_error)) {
      return raise_exception_value(runtime.make_exception("AttributeError", error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    Value attr_arg = Value::string(fn.names[in.b]);
    if (!runtime_call_callable(runtime, module_getattr, &attr_arg, 1, method, error)) {
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  std::vector<NativeKeywordArg> native_keyword_args;
  bool pushed_frame = false;
  if (!call_callable_value_ex(runtime, method, args, module, module_owner, in.dst, ip,
                              native_call_args, native_keyword_args, execution_lock,
                              regs[in.dst], pushed_frame, make_generator_if_needed,
                              push_frame, raise_runtime_error, raise_exception_value,
                              &monitoring_code, static_cast<int64_t>(ip))) {
    return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
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
    const Value& monitoring_code,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<VMFrame>& frames,
    size_t& frame_count,
    XlangVMInstrCacheStorage& instr_cache,
    std::vector<Value>& native_call_args,
    size_t& ip,
    RuntimeResult& result,
    XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor_fn,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value, bool allow_python_new_continuation = false) {
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
  Value generic_alias_origin;
  const Value* callee_value = &regs[in.a];
  if (auto* alias = value_as_generic_alias(*callee_value)) {
    value_assign_fast(generic_alias_origin, alias->origin);
    callee_value = &generic_alias_origin;
  }
  const auto& callee = *callee_value;
  bool pushed_frame = false;
  std::vector<NativeKeywordArg> native_keyword_args;

  // A warmed CALL_FUNCTION_EX site often invokes the same Python function
  // with a changing starred argument tuple (asyncio.gather in async-tree is a
  // hot example). Guard the cached function by the live callee object, then
  // enter through call_user_function so expansion, argument binding,
  // monitoring, generator construction, and frame semantics stay unchanged.
  // The fast path only removes the repeated callable-kind dispatch chain.
  if (!instr_cache.empty() && callee.tag == ValueTag::Object &&
      callee.as.obj != nullptr) {
    auto& cache = instr_cache[ip].call;
    if (cache.kind == CallSiteKind::UserFunction &&
        cache.callee_object == callee.as.obj && cache.function != nullptr) {
      if (!xlang3::xlang_vm::ops::call_user_function(
              cache.function, call_args, module, module_owner, in.dst, ip,
              regs[in.dst], pushed_frame, make_generator_if_needed, push_frame)) {
        if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
        return XlangVMOpFlow::ContinueLoop;
      }
      if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
      return XlangVMOpFlow::Next;
    }
    // CALL_FUNCTION_EX can repeatedly construct the same class from starred
    // arguments (asyncio.gather's _GatheringFuture is a hot example). Once the
    // generic class path has established that __new__/metaclass dispatch is
    // ordinary and __init__ resolves to a plain bound Python method (or an own native method), cache only
    // that initializer. Exact class and class/metaclass version guards keep
    // later monkey-patches on the normal lookup path; dynamic argument binding
    // still runs for every call.
    if (cache.callee_object == callee.as.obj &&
        (cache.kind == CallSiteKind::UserConstructor ||
         cache.kind == CallSiteKind::GuardedUserConstructorEx ||
         cache.kind == CallSiteKind::NativeConstructor)) {
      auto* cached_class = value_as_class(callee);
      auto* metaclass = cached_class == nullptr
          ? nullptr : value_as_class(cached_class->metaclass);
      if (cached_class != nullptr && metaclass != nullptr &&
          (cache.kind != CallSiteKind::GuardedUserConstructorEx ||
           (cached_class->native_type_constructor == nullptr &&
            module_owner != nullptr &&
            call_ex_constructor_args_callback_free(call_args, regs.size()))) &&
          cache.class_version == cached_class->version &&
          cache.arg0_object == &metaclass->header &&
          cache.secondary_class_version == metaclass->version &&
          // Class/MRO and metaclass guards precede the weak function pointer.
          // __code__ mutation can turn an ordinary initializer into a generator.
          ((cache.kind != CallSiteKind::UserConstructor &&
            cache.kind != CallSiteKind::GuardedUserConstructorEx) ||
           (cache.function != nullptr &&
            cache.function_code_version == cache.function->code_version))) {
        Value initializer_owner;
        if (cache.kind == CallSiteKind::GuardedUserConstructorEx) {
          // The persistent plan is ownerless. Pin the proved live initializer
          // through binder scratch cleanup and frame publication, which can
          // reenter via finalizers, without allocating another bound method.
          initializer_owner.tag = ValueTag::Object;
          initializer_owner.as.obj = &cache.function->header;
          retain(initializer_owner);
        }
        Value instance = Value::instance(callee);
        initialize_exception_call_args(runtime, instance, call_args);
        CallArgsView init_args = call_args;
        init_args.leading = &instance;
        init_args.leading_count = 1;
        if ((cache.kind == CallSiteKind::UserConstructor ||
             cache.kind == CallSiteKind::GuardedUserConstructorEx) &&
            cache.function != nullptr) {
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
          if (!push_frame(*call_module, fn_obj->function_id, init_args.with_keyword_defaults(*fn_obj),
                          fn_obj->closure, fn_obj->defaults,
                          fn_obj->globals_module, std::move(call_module_owner),
                          in.dst, FrameReturnMode::StoreConstructedInstance,
                          std::move(constructed_instance))) {
            return result.errors.empty() ? XlangVMOpFlow::ContinueLoop
                                         : XlangVMOpFlow::ReturnResult;
          }
          return XlangVMOpFlow::SwitchFrame;
        }
        if (cache.kind == CallSiteKind::NativeConstructor &&
            cache.native != nullptr) {
          Value ignored;
          if (!xlang3::xlang_vm::ops::call_native_function(
                  runtime, cache.native, init_args, native_call_args,
                  execution_lock, ignored, raise_runtime_error,
                  raise_exception_value)) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          value_assign_fast(regs[in.dst], instance);
          return XlangVMOpFlow::Next;
        }
      }
      cache.kind = CallSiteKind::Empty;
    }
  }

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
    if (!instr_cache.empty() && callee.tag == ValueTag::Object &&
        callee.as.obj != nullptr) {
      auto& cache = instr_cache[ip].call;
      cache.callee_object = callee.as.obj;
      value_assign_fast(cache.retained_callee, callee);
      cache.kind = CallSiteKind::UserFunction;
      cache.function = fn_obj;
      cache.function_code_version = fn_obj->code_version;
      cache.inline_function_id = UINT32_MAX;
      cache.native = nullptr;
    }
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
    if (auto* metaclass = value_as_class(klass->metaclass);
        metaclass != nullptr && metaclass->name != "type") {
      Value meta_call;
      std::string meta_call_error;
      if (class_get_bound_attr(
              runtime, klass->metaclass, callee, "__call__", meta_call, meta_call_error)) {
        CallArgsView meta_call_args = call_args;
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
    // Exact native heap types can bypass the generic __new__/__init__ adapters
    // only while their class and base guards hold. The callback declines when
    // monitoring/debug hooks are active or the call shape is dynamic, leaving
    // normal descriptor dispatch to preserve Python's observable behavior.
    if (klass->native_type_constructor != nullptr &&
        klass->native_type_constructor_version == klass->version) {
      bool handled = false;
      std::string native_error;
      const bool succeeded = klass->native_type_constructor(
          runtime, *klass, call_args, handled, regs[in.dst], native_error);
      if (handled) {
        XlangVMBuiltinConstructorError constructor_error;
        constructor_error.fully_handled = true;
        if (!succeeded) {
          constructor_error.set("RuntimeError", native_error.empty()
              ? "native type constructor failed" : std::move(native_error));
        }
        return finish_fully_handled_native_constructor(
            runtime, constructor_error, raise_exception_value);
      }
    }
    Value early_new_callable;
    bool plain_python_new = false;
    if (xlang_vm_resolve_class_new_callable(runtime, callee, klass, early_new_callable, &plain_python_new)) {
      if (allow_python_new_continuation && plain_python_new) {
        bool attempted = false;
        const bool ok = xlang_vm_try_push_python_class_new(
            runtime, callee, klass, early_new_callable, call_args, module,
            module_owner, regs.size(), in.dst, ip, regs[in.dst], pushed_frame, attempted,
            make_generator_if_needed, push_frame);
        if (attempted) {
          if (!ok) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
        }
      }
      Value new_callable;
      value_assign_fast(new_callable, early_new_callable);
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
    XlangVMBuiltinConstructorError constructor_error;
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
    if (xlang_vm_resolve_class_new_callable(runtime, callee, klass, resolved_new_callable)) {
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
      if (constructor_error.fully_handled) {
        return finish_fully_handled_native_constructor(
            runtime, constructor_error, raise_exception_value);
      }
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
    Value pending_constructor_exception;
    if (runtime.take_pending_exception(pending_constructor_exception)) {
      return raise_exception_value(std::move(pending_constructor_exception))
          ? XlangVMOpFlow::ContinueLoop
          : XlangVMOpFlow::ReturnResult;
    }
    if (!constructor_error.message.empty()) {
      return raise_exception_value(runtime.make_exception(constructor_error.type, constructor_error.message))
          ? XlangVMOpFlow::ContinueLoop
          : XlangVMOpFlow::ReturnResult;
    }
    bool abstract_rejected = false;
    if (!xlang_vm_reject_abstract_class_instantiation(runtime, callee, *klass, abstract_rejected, raise_exception_value)) {
      return XlangVMOpFlow::ReturnResult;
    }
    if (abstract_rejected) {
      return XlangVMOpFlow::ContinueLoop;
    }
    Value instance = Value::instance(callee);
    initialize_exception_call_args(runtime, instance, call_args);
    Value bound_init;
    std::string init_error;
    if (class_get_bound_attr(runtime, callee, instance, "__init__", bound_init, init_error)) {
      Value raw_init;
      const auto raw_init_it = klass->attrs.find("__init__");
      bool raw_init_found = raw_init_it != klass->attrs.end();
      if (raw_init_found) value_assign_fast(raw_init, raw_init_it->second);
      else {
        // Expanded construction often inherits a plain Python initializer.
        // Resolve through the same MRO as the bound lookup; do not copy it into
        // subclass attrs or specialize an arbitrary descriptor/native result.
        // The retained class owns its bases; class/base mutation invalidates
        // descendant versions before releasing a replaced initializer owner.
        Value inherited_init;
        std::string inherited_error;
        if (object_lookup_class_attr(callee, "__init__", inherited_init, inherited_error)) {
          if (auto* inherited_function = value_as_function(inherited_init)) {
            const auto* inherited_module = inherited_function->module != nullptr
                ? inherited_function->module.get() : &module;
            if (inherited_function->function_id < inherited_module->functions.size()) {
              const auto& inherited_ir = inherited_module->functions[inherited_function->function_id];
              // Warm push_frame does not run make_generator_if_needed.
              if (!inherited_ir.is_generator && !inherited_ir.is_async) {
                raw_init = std::move(inherited_init);
                raw_init_found = true;
              }
            }
          }
        }
      }
      if (auto* bound = value_as_bound_method(bound_init);
          raw_init_found && bound != nullptr &&
          bound->self.tag == ValueTag::Object &&
          bound->self.as.obj == instance.as.obj && !instr_cache.empty()) {
        auto& cache = instr_cache[ip].call;
        if (auto* init_function = value_as_function(raw_init);
            init_function != nullptr && init_function == value_as_function(bound->function)) {
          auto* metaclass = value_as_class(klass->metaclass);
          const ir::Function* init_ir = init_function->module != nullptr &&
              init_function->function_id < init_function->module->functions.size()
              ? &init_function->module->functions[init_function->function_id] : nullptr;
          const bool persistent = module_owner != nullptr && metaclass != nullptr &&
              klass->native_type_constructor == nullptr && init_ir != nullptr &&
              !init_ir->is_generator && !init_ir->is_async && !init_ir->is_coroutine &&
              call_ex_constructor_args_callback_free(call_args, regs.size());
          if (persistent) {
            // Snapshot the proof while raw_init/bound_init own the target.
            // Releasing a previous cache owner can mutate class/meta/code.
            // Never pair the held old function with fresh post-cleanup tags.
            const uint64_t class_version = klass->version;
            const uint64_t meta_version = metaclass->version;
            const uint64_t code_version = init_function->code_version;
            Object* meta_identity = &metaclass->header;
            CallSiteCache retired = std::move(cache);
            cache = CallSiteCache{};
            retired = CallSiteCache{};
            auto* live_meta = value_as_class(klass->metaclass);
            if (class_version != 0 && meta_version != 0 &&
                klass->version == class_version &&
                klass->native_type_constructor == nullptr &&
                live_meta != nullptr && &live_meta->header == meta_identity &&
                live_meta->version == meta_version &&
                init_function->code_version == code_version) {
              cache.callee_object = callee.as.obj;
              cache.kind = CallSiteKind::GuardedUserConstructorEx;
              cache.function = init_function;
              cache.function_code_version = code_version;
              cache.class_version = class_version;
              cache.arg0_object = meta_identity;
              cache.secondary_class_version = meta_version;
            }
          } else {
            // Unknown/custom expansion, native hooks and nonordinary IR retain
            // the existing owning, per-activation constructor cache policy.
            cache.callee_object = callee.as.obj;
            value_assign_fast(cache.retained_callee, callee);
            cache.kind = CallSiteKind::UserConstructor;
            cache.function = init_function;
            cache.function_code_version = init_function->code_version;
            cache.inline_function_id = UINT32_MAX;
            cache.native = nullptr;
            cache.class_version = klass->version;
            if (metaclass != nullptr) {
              cache.arg0_object = &metaclass->header;
              cache.secondary_class_version = metaclass->version;
            }
          }
        } else if (auto* init_native = value_as_native_function(raw_init);
                   init_native != nullptr && init_native->bind_as_descriptor &&
                   init_native == value_as_native_function(bound->function)) {
          cache.callee_object = callee.as.obj;
          value_assign_fast(cache.retained_callee, callee);
          cache.kind = CallSiteKind::NativeConstructor;
          cache.function = nullptr;
          cache.native = init_native;
          cache.class_version = klass->version;
          if (auto* metaclass = value_as_class(klass->metaclass)) {
            cache.arg0_object = &metaclass->header;
            cache.secondary_class_version = metaclass->version;
          }
        }
      }
      if (!xlang3::xlang_vm::ops::call_callable_value_ex(runtime, bound_init, call_args, module, module_owner, in.dst, ip, native_call_args, native_keyword_args, execution_lock, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame, raise_runtime_error, raise_exception_value)) {
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
  } else if (value_as_event(callee) != nullptr) {
    std::string error;
    const Value* args = materialize_native_args(call_args, native_call_args);
    if (!event_fire(runtime, callee, args, static_cast<uint32_t>(call_args.size()), regs[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  } else if (value_as_instance(callee) != nullptr) {
    Value call_attr;
    std::string attr_error;
    if (!object_get_special_method(runtime, callee, "__call__", call_attr, attr_error)) {
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

XLANG3_HOT_INLINE void xlang_vm_retire_canonical_constructor_arguments(
    const ir::Instr& in, const ir::Function& caller,
    const std::vector<uint32_t>& argument_registers,
    const Value& callee, const Value& constructed,
    XlangVMSmallRegisterBuffer& registers, size_t ip) {
  const auto metadata = std::atomic_load_explicit(
      &caller.execution_metadata, std::memory_order_acquire);
  if (metadata == nullptr || metadata->owner != &caller ||
      !metadata->linear_constructor_argument_liveness) return;
  // The complete slot plan already owns every explicit argument. Clearing a
  // consumed owning register here cannot finalize an argument; it removes only
  // a redundant root before old output/local replacement can run a finalizer.
  for (uint32_t reg : argument_registers) {
    if (reg == in.dst || reg == in.a || reg >= registers.size() ||
        reg >= metadata->register_last_use.size() ||
        metadata->register_last_use[reg] != ip ||
        (reg < metadata->register_loop_carried.size() && metadata->register_loop_carried[reg])) continue;
    auto& value = registers[reg];
    if (value.tag != ValueTag::Object ||
        (value.flags & kXlangValueBorrowedRefFlag) != 0 ||
        value_is(value, callee) || value_is(value, constructed)) continue;
    value_set_invalid(value);
  }
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
    XlangVMInstrCacheStorage& instr_cache,
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
    RaiseExceptionValue&& raise_exception_value,
    XlangVMSmallValueBuffer* locals = nullptr, bool allow_python_new_continuation = false) {
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
  Value generic_alias_origin;
  const Value* callee_value = &regs[in.a];
  if (auto* alias = value_as_generic_alias(*callee_value)) {
    value_assign_fast(generic_alias_origin, alias->origin);
    callee_value = &generic_alias_origin;
  }
  const auto& callee = *callee_value;
  bool pushed_frame = false;
    if (!instr_cache.empty() && callee.tag == ValueTag::Object && callee.as.obj != nullptr) {
      auto& cache = instr_cache[ip].call;
      if (cache.callee_object == callee.as.obj) {
      if (cache.kind == CallSiteKind::UserFunction) {
        if (!xlang3::xlang_vm::ops::call_user_function(cache.function, call_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame,
                FrameReturnMode::StoreReturnValue, Value::invalid(), &runtime)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
        return XlangVMOpFlow::Next;
      }
      if (cache.kind == CallSiteKind::BoundPythonMethod) {
        if (auto* bound = value_as_bound_method(cache.retained_callee);
            bound != nullptr && value_as_function(bound->function) == cache.function) {
          // `LOAD_ATTR; CALL` is the common shape for a bound Python method
          // saved on an instance (pickle's `self.write` is a hot example).
          // The exact callee-object guard above keeps rebinding observable;
          // retain the bound method so its receiver and identity stay alive.
          CallArgsView bound_args = call_args;
          bound_args.leading = &bound->self;
          bound_args.leading_count = 1;
          if (!xlang3::xlang_vm::ops::call_user_function(
                  cache.function, bound_args, module, module_owner, in.dst, ip,
                  regs[in.dst], pushed_frame, make_generator_if_needed, push_frame,
                  FrameReturnMode::StoreReturnValue, Value::invalid(), &runtime)) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
          return XlangVMOpFlow::Next;
        }
        cache.kind = CallSiteKind::Empty;
        cache.function = nullptr;
      }
      if (cache.kind == CallSiteKind::NativeFunction) {
        if (!xlang3::xlang_vm::ops::call_native_function(runtime, cache.native, call_args, native_call_args, execution_lock, regs[in.dst], raise_runtime_error, raise_exception_value)) {
          if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
          return XlangVMOpFlow::ContinueLoop;
        }
        return XlangVMOpFlow::Next;
      }
      if (cache.kind == CallSiteKind::InlineBuiltinTypeValue) {
        if (inline_calls_allowed(runtime) && call_args.size() == 1 &&
            !call_args.has_keywords() && !call_args.has_expansion() &&
            runtime_type_of_value(runtime, call_args.get(0), regs[in.dst])) {
          return XlangVMOpFlow::Next;
        }
        cache.kind = CallSiteKind::Empty;
      }
      if (cache.kind == CallSiteKind::InlineCanonicalSlotConstructor) {
        auto* cached_class = value_as_class(callee);
        auto* metaclass = cached_class == nullptr ? nullptr : value_as_class(cached_class->metaclass);
        // Check live class/metaclass generations before the weak initializer.
        // SDK native hooks can attach without changing a class version.
        const bool shape_matches = cached_class != nullptr && metaclass != nullptr &&
            cache.class_version == cached_class->version &&
            cache.arg0_object == &metaclass->header &&
            cache.secondary_class_version == metaclass->version &&
            cached_class->native_type_constructor == nullptr;
        if (shape_matches && cache.function != nullptr &&
            cache.function_code_version == cache.function->code_version &&
            inline_calls_allowed(runtime)) {
          Value initializer_owner;
          initializer_owner.tag = ValueTag::Object;
          initializer_owner.as.obj = &cache.function->header;
          retain(initializer_owner);
          Value instance = Value::instance(callee);
          Value completed;
          std::string error;
          if (xlang_vm_execute_own_canonical_slot_constructor(
                  instance, module, *cache.function, call_args,
                  cache.slot_constructor_args, completed, error)) {
            // Pin the function and completed instance through the old output's
            // finalizer. Never revisit the borrowed cache or arguments after it.
            xlang_vm_retire_canonical_constructor_arguments(
                in, fn, call_arg_regs, callee, completed, regs, ip);
            value_move_assign_fast(regs[in.dst], completed);
            return XlangVMOpFlow::Next;
          }
        }
        cache.kind = CallSiteKind::Empty;
        cache.function = nullptr;
      }
      const bool is_cached_arg_inline =
          cache.kind == CallSiteKind::InlineArgBinaryFunction;
      const bool allow_cached_python_inline = cache.function != nullptr &&
          cache.function_code_version == cache.function->code_version &&
          (is_cached_arg_inline
               ? inline_cached_arg_function_allowed(
                     runtime, module, *cache.function, cache)
               : inline_python_function_allowed(runtime, module, *cache.function));
      if (allow_cached_python_inline && is_cached_arg_inline) {
        if (locals != nullptr &&
            xlang_vm_try_cached_call_local_accumulate(
                in, fn, cache, call_args, regs, *locals, ip)) {
          return XlangVMOpFlow::NextNoMonitoringRefresh;
        }
        ArgBinaryFunctionSpec spec;
        spec.lhs_arg = cache.lhs_slot;
        spec.rhs_arg = cache.rhs_slot;
        spec.op = cache.inline_op;
        spec.next_arg = cache.next_arg;
        spec.next_op = cache.next_op;
        spec.has_next = cache.has_next;
        spec.next_is_constant = cache.next_is_constant;
        if (spec.next_is_constant) {
          value_assign_fast(spec.next_constant, cache.inline_const);
        }
        if (xlang_vm_arg_binary_inline_values_supported(call_args, spec)) {
          const bool output_overwrite_cannot_finalize =
              regs[in.dst].tag != ValueTag::Object;
          std::string error;
          if (!execute_arg_binary_function_fn(call_args, spec, regs[in.dst], error)) {
            if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
            return XlangVMOpFlow::ReturnResult;
          }
          return output_overwrite_cannot_finalize
              ? XlangVMOpFlow::NextNoMonitoringRefresh
              : XlangVMOpFlow::Next;
        }
      }
      if (allow_cached_python_inline &&
          cache.kind == CallSiteKind::InlineConditionalArgFunction &&
          cache.cached_values.size() == 3) {
        XlangVMConditionalArgFunctionSpec spec;
        spec.condition_arg = cache.lhs_slot;
        spec.true_arg = cache.rhs_slot;
        spec.false_arg = cache.next_arg;
        spec.compare = static_cast<ir::CompareOp>(cache.fast_method_id);
        spec.true_op = cache.inline_op;
        spec.false_op = cache.next_op;
        value_assign_fast(spec.condition_constant, cache.cached_values[0]);
        value_assign_fast(spec.true_constant, cache.cached_values[1]);
        value_assign_fast(spec.false_constant, cache.cached_values[2]);
        std::string inline_error;
        if (xlang_vm_execute_conditional_arg_function(
                call_args, spec, regs[in.dst], inline_error)) {
          return XlangVMOpFlow::Next;
        }
      }
      if (allow_cached_python_inline && cache.kind == CallSiteKind::InlineTrivialFunction) {
        XlangVMTrivialFunctionSpec spec;
        spec.returns_argument = cache.has_next;
        spec.argument = cache.lhs_slot;
        value_assign_fast(spec.constant, cache.inline_const);
        if (!xlang_vm_execute_trivial_function(call_args, spec, regs[in.dst])) {
          return raise_runtime_error("invalid inline function argument")
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
      // Cached Python-function specializations use their narrower per-function
      // monitoring guard above; defer the global inline gate to constructor paths.
      const bool allow_inline_calls = inline_calls_allowed(runtime);
      if (cache.kind == CallSiteKind::UserConstructor || cache.kind == CallSiteKind::NativeConstructor ||
          (allow_inline_calls &&
           (cache.kind == CallSiteKind::InlineSlotConstructor ||
            cache.kind == CallSiteKind::InlineMathPointConstructor))) {
        auto* cached_class = value_as_class(callee);
        if (cached_class == nullptr || cache.class_version != cached_class->version ||
            (cache.function != nullptr && cache.function_code_version != cache.function->code_version)) {
          cache.kind = CallSiteKind::Empty;
        } else {
        if (allow_inline_calls &&
            cache.kind == CallSiteKind::InlineMathPointConstructor) {
          Value instance = Value::instance(callee);
          if (cache.function != nullptr && cache.cached_values.size() == 1 &&
              !call_args.has_keywords() && !call_args.has_expansion() &&
              xlang_vm_execute_float_point_constructor(
                  instance, callee, *cache.function, call_args,
                  cache.inline_slots, cache.inline_globals_module,
                  cache.inline_globals_version, cache.inline_const,
                  cache.cached_values[0], regs[in.dst])) {
            return XlangVMOpFlow::Next;
          }
          cache.kind = CallSiteKind::Empty;
        }
        if (allow_inline_calls && cache.kind == CallSiteKind::InlineSlotConstructor) {
          Value instance = Value::instance(callee);
          // Inline-constructor eligibility rejects BaseException subclasses,
          // and the class-version guard above invalidates this cache if its
          // inheritance changes. Skip the generic exception setup here so an
          // ordinary cached class call avoids a BaseException lookup/MRO walk.
          std::string error;
          if (cache.function && execute_slot_constructor_fn(
                  instance, module, *cache.function, call_args,
                  cache.slot_constructor_args, regs[in.dst], error)) {
            return XlangVMOpFlow::Next;
          }
          cache.kind = CallSiteKind::Empty;
          if (!error.empty()) {
            if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
            return XlangVMOpFlow::ReturnResult;
          }
        }
        if (cache.kind != CallSiteKind::Empty) {
        Value instance = Value::instance(callee);
        initialize_exception_call_args(runtime, instance, call_args);
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
          if (!push_frame(*call_module, fn_obj->function_id, init_args.with_keyword_defaults(*fn_obj), fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                          std::move(call_module_owner), in.dst,
                          FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
            return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
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
  }
  if (auto* fn_obj = value_as_function(callee)) {
    XlangVMConditionalArgFunctionSpec conditional_spec;
    if (inline_python_function_allowed(runtime, module, *fn_obj) &&
        xlang_vm_analyze_conditional_arg_function(
            module, *fn_obj, static_cast<uint32_t>(call_args.size()), conditional_spec)) {
      std::string inline_error;
      if (xlang_vm_execute_conditional_arg_function(
              call_args, conditional_spec, regs[in.dst], inline_error)) {
        if (!instr_cache.empty() && callee.tag == ValueTag::Object) {
          auto& cache = instr_cache[ip].call;
          cache.callee_object = callee.as.obj;
          value_assign_fast(cache.retained_callee, callee);
          cache.kind = CallSiteKind::InlineConditionalArgFunction;
          cache.function = fn_obj;
          cache.function_code_version = fn_obj->code_version;
          cache.inline_function_id = UINT32_MAX;
          cache.native = nullptr;
          cache.class_version = 0;
          cache.lhs_slot = conditional_spec.condition_arg;
          cache.rhs_slot = conditional_spec.true_arg;
          cache.next_arg = conditional_spec.false_arg;
          cache.fast_method_id = static_cast<uint32_t>(conditional_spec.compare);
          cache.inline_op = conditional_spec.true_op;
          cache.next_op = conditional_spec.false_op;
          cache.cached_values = {
              conditional_spec.condition_constant,
              conditional_spec.true_constant,
              conditional_spec.false_constant,
          };
        }
        return XlangVMOpFlow::Next;
      }
    }
    XlangVMTrivialFunctionSpec trivial_spec;
    if (inline_python_function_allowed(runtime, module, *fn_obj) &&
        xlang_vm_analyze_trivial_function(
            module, *fn_obj, static_cast<uint32_t>(call_args.size()), trivial_spec)) {
      if (!instr_cache.empty() && callee.tag == ValueTag::Object) {
        auto& cache = instr_cache[ip].call;
        cache.callee_object = callee.as.obj;
        value_assign_fast(cache.retained_callee, callee);
        cache.kind = CallSiteKind::InlineTrivialFunction;
        cache.function = fn_obj;
        cache.function_code_version = fn_obj->code_version;
        cache.inline_function_id = UINT32_MAX;
        cache.native = nullptr;
        cache.class_version = 0;
        cache.lhs_slot = trivial_spec.argument;
        cache.has_next = trivial_spec.returns_argument;
        value_assign_fast(cache.inline_const, trivial_spec.constant);
      }
      if (!xlang_vm_execute_trivial_function(call_args, trivial_spec, regs[in.dst])) {
        return raise_runtime_error("invalid inline function argument")
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return XlangVMOpFlow::Next;
    }
    ArgBinaryFunctionSpec inline_spec;
    if (inline_python_function_allowed(runtime, module, *fn_obj) &&
        analyze_arg_binary_function_fn(module, *fn_obj, static_cast<uint32_t>(call_args.size()), inline_spec) &&
        xlang_vm_arg_binary_inline_values_supported(call_args, inline_spec)) {
      if (!instr_cache.empty() && callee.tag == ValueTag::Object) {
        auto& cache = instr_cache[ip].call;
        cache.callee_object = callee.as.obj;
        value_assign_fast(cache.retained_callee, callee);
        cache.kind = CallSiteKind::InlineArgBinaryFunction;
        cache.function = fn_obj;
        cache.function_code_version = fn_obj->code_version;
        cache.inline_function_id = UINT32_MAX;
        cache.native = nullptr;
        cache.class_version = 0;
        cache.lhs_slot = inline_spec.lhs_arg;
        cache.rhs_slot = inline_spec.rhs_arg;
        cache.inline_op = inline_spec.op;
        cache.next_arg = inline_spec.next_arg;
        cache.next_op = inline_spec.next_op;
        value_assign_fast(cache.inline_const, inline_spec.next_constant);
        cache.has_next = inline_spec.has_next;
        cache.next_is_constant = inline_spec.next_is_constant;
      }
      const bool output_overwrite_cannot_finalize =
          regs[in.dst].tag != ValueTag::Object;
      std::string error;
      if (!execute_arg_binary_function_fn(call_args, inline_spec, regs[in.dst], error)) {
        if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
        return XlangVMOpFlow::ReturnResult;
      }
      return output_overwrite_cannot_finalize
          ? XlangVMOpFlow::NextNoMonitoringRefresh
          : XlangVMOpFlow::Next;
    }
    if (!instr_cache.empty() && callee.tag == ValueTag::Object) {
      auto& cache = instr_cache[ip].call;
      cache.callee_object = callee.as.obj;
      value_assign_fast(cache.retained_callee, callee);
      cache.kind = CallSiteKind::UserFunction;
      cache.function = fn_obj;
      cache.function_code_version = fn_obj->code_version;
      cache.inline_function_id = UINT32_MAX;
      cache.native = nullptr;
      cache.class_version = 0;
    }
    if (!xlang3::xlang_vm::ops::call_user_function(fn_obj, call_args, module, module_owner, in.dst, ip, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame,
            FrameReturnMode::StoreReturnValue, Value::invalid(), &runtime)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else if (auto* bound = value_as_bound_method(callee)) {
    CallArgsView bound_args = call_args;
    bound_args.leading = &bound->self;
    bound_args.leading_count = 1;
    if (auto* fn_obj = value_as_function(bound->function);
        fn_obj != nullptr && !instr_cache.empty()) {
      auto& cache = instr_cache[ip].call;
      cache.callee_object = callee.as.obj;
      value_assign_fast(cache.retained_callee, callee);
      cache.kind = CallSiteKind::BoundPythonMethod;
      cache.function = fn_obj;
      cache.function_code_version = fn_obj->code_version;
      cache.inline_function_id = UINT32_MAX;
      cache.native = nullptr;
      cache.class_version = 0;
    }
    if (!xlang3::xlang_vm::ops::call_callable_value(runtime, bound->function, bound_args, module, module_owner, in.dst, ip, native_call_args, execution_lock, regs[in.dst], pushed_frame, make_generator_if_needed, push_frame, raise_runtime_error, raise_exception_value)) {
      if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
      return XlangVMOpFlow::ContinueLoop;
    }
    if (pushed_frame) return XlangVMOpFlow::SwitchFrame;
  } else if (auto* klass = value_as_class(callee)) {
    const bool allow_inline_calls = inline_calls_allowed(runtime);
    // A short callback loses its owning instruction cache on return. The live
    // class proof survives without Python owners and reuses the same physical
    // stores only while selection/body generations and observability permit it.
    if (allow_inline_calls && klass->canonical_slot_constructor_cache != nullptr) {
      Value initializer_owner;
      Value completed;
      if (xlang_vm_try_class_canonical_slot_constructor(
              runtime, module, callee, call_args, initializer_owner, completed)) {
        xlang_vm_retire_canonical_constructor_arguments(
            in, fn, call_arg_regs, callee, completed, regs, ip);
        value_move_assign_fast(regs[in.dst], completed);
        // Old output can run a finalizer: do not revisit a borrowed class plan,
        // function, or argument after publishing the completed instance.
        return XlangVMOpFlow::Next;
      }
    }
    // `type(value)` is a common pure-Python dispatch primitive (including
    // copy.deepcopy). Cache its exact builtin call shape to avoid routing a
    // one-argument query through generic class construction on every call.
    // Monitoring/debug modes keep the ordinary path so call observability is
    // preserved; unsupported runtime values also fall through unchanged.
    if (allow_inline_calls && call_args.size() == 1 &&
        !call_args.has_keywords() && !call_args.has_expansion()) {
      const Value* builtin_type = runtime.find_builtin("type");
      if (builtin_type != nullptr && builtin_type->tag == ValueTag::Object &&
          builtin_type->as.obj == callee.as.obj &&
          runtime_type_of_value(runtime, call_args.get(0), regs[in.dst])) {
        if (!instr_cache.empty() && callee.tag == ValueTag::Object) {
          auto& cache = instr_cache[ip].call;
          cache.callee_object = callee.as.obj;
          value_assign_fast(cache.retained_callee, callee);
          cache.kind = CallSiteKind::InlineBuiltinTypeValue;
          cache.function = nullptr;
          cache.native = nullptr;
        }
        return XlangVMOpFlow::Next;
      }
    }
    if (call_args.size() == 1 && !call_args.has_keywords() && !call_args.has_expansion()) {
      Value enum_member;
      if (class_try_enum_value_lookup(callee, call_args.get(0), enum_member)) {
        value_assign_fast(regs[in.dst], enum_member);
        return XlangVMOpFlow::Next;
      }
    }
    if (auto* metaclass = value_as_class(klass->metaclass);
        metaclass != nullptr && metaclass->name != "type") {
      Value meta_call;
      std::string meta_call_error;
      if (class_get_bound_attr(
              runtime, klass->metaclass, callee, "__call__", meta_call, meta_call_error)) {
        CallArgsView meta_call_args = call_args;
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
    Value early_new_callable;
    bool plain_python_new = false;
    if (xlang_vm_resolve_class_new_callable(runtime, callee, klass, early_new_callable, &plain_python_new)) {
      if (allow_python_new_continuation && plain_python_new) {
        bool attempted = false;
        const bool ok = xlang_vm_try_push_python_class_new(
            runtime, callee, klass, early_new_callable, call_args, module,
            module_owner, regs.size(), in.dst, ip, regs[in.dst], pushed_frame, attempted,
            make_generator_if_needed, push_frame);
        if (attempted) {
          if (!ok) {
            if (!result.errors.empty()) return XlangVMOpFlow::ReturnResult;
            return XlangVMOpFlow::ContinueLoop;
          }
          return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
        }
      }
      Value new_callable;
      value_assign_fast(new_callable, early_new_callable);
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
    XlangVMBuiltinConstructorError constructor_error;
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
      if (constructor_error.fully_handled) {
        return finish_fully_handled_native_constructor(
            runtime, constructor_error, raise_exception_value);
      }
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
    Value pending_constructor_exception;
    if (runtime.take_pending_exception(pending_constructor_exception)) {
      return raise_exception_value(std::move(pending_constructor_exception))
          ? XlangVMOpFlow::ContinueLoop
          : XlangVMOpFlow::ReturnResult;
    }
    if (!constructor_error.message.empty()) {
      return raise_exception_value(runtime.make_exception(constructor_error.type, constructor_error.message))
          ? XlangVMOpFlow::ContinueLoop
          : XlangVMOpFlow::ReturnResult;
    }
    bool abstract_rejected = false;
    if (!xlang_vm_reject_abstract_class_instantiation(runtime, callee, *klass, abstract_rejected, raise_exception_value)) {
      return XlangVMOpFlow::ReturnResult;
    }
    if (abstract_rejected) {
      return XlangVMOpFlow::ContinueLoop;
    }
    Value instance = Value::instance(callee);
    initialize_exception_call_args(runtime, instance, call_args);
    CallArgsView init_args = call_args;
    init_args.leading = &instance;
    init_args.leading_count = 1;
    Value raw_init;
    std::string raw_init_error;
    if (object_lookup_class_attr(callee, "__init__", raw_init, raw_init_error) &&
        value_as_native_function(raw_init) == nullptr && value_as_function(raw_init) == nullptr) {
      Value bound_init;
      std::string bound_init_error;
      if (!class_get_bound_attr(runtime, callee, instance, "__init__", bound_init, bound_init_error)) {
        return raise_runtime_error(bound_init_error.empty() ? "__init__ lookup failed" : bound_init_error)
            ? XlangVMOpFlow::ContinueLoop
            : XlangVMOpFlow::ReturnResult;
      }
      std::vector<NativeKeywordArg> init_keyword_args;
      bool init_has_keywords = false;
      std::string init_materialize_error;
      const Value* positional = materialize_native_call_ex(
          runtime, call_args, native_call_args, init_keyword_args,
          init_has_keywords, init_materialize_error);
      if (positional == nullptr &&
          (!native_call_args.empty() || !init_materialize_error.empty())) {
        return raise_runtime_error(init_materialize_error.empty()
                                       ? "__init__ argument expansion failed"
                                       : init_materialize_error)
            ? XlangVMOpFlow::ContinueLoop
            : XlangVMOpFlow::ReturnResult;
      }
      std::vector<std::pair<std::string, Value>> init_kwargs;
      init_kwargs.reserve(init_keyword_args.size());
      for (const auto& keyword : init_keyword_args)
        init_kwargs.emplace_back(keyword.name, *keyword.value);
      Value ignored;
      execution_lock.unlock();
      const bool init_ok = runtime_call_callable_kw(
          runtime,
          bound_init,
          positional,
          static_cast<uint32_t>(native_call_args.size()),
          init_kwargs,
          ignored,
          bound_init_error);
      execution_lock.lock();
      if (!init_ok) {
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending))
              ? XlangVMOpFlow::ContinueLoop
              : XlangVMOpFlow::ReturnResult;
        }
        return raise_runtime_error(bound_init_error.empty() ? "__init__ failed" : bound_init_error)
            ? XlangVMOpFlow::ContinueLoop
            : XlangVMOpFlow::ReturnResult;
      }
      value_assign_fast(regs[in.dst], instance);
      return XlangVMOpFlow::Next;
    }
    if (!instr_cache.empty()) {
      auto& cache = instr_cache[ip].call;
      if (cache.callee_object == callee.as.obj && cache.class_version == klass->version &&
          (cache.function == nullptr || cache.function_code_version == cache.function->code_version)) {
        if (allow_inline_calls && cache.kind == CallSiteKind::InlineSlotConstructor) {
          std::string error;
          if (cache.function && execute_slot_constructor_fn(
                  instance, module, *cache.function, call_args,
                  cache.slot_constructor_args, regs[in.dst], error)) {
            return XlangVMOpFlow::Next;
          }
          cache.kind = CallSiteKind::Empty;
          if (!error.empty()) {
            if (raise_runtime_error(error)) return XlangVMOpFlow::ContinueLoop;
            return XlangVMOpFlow::ReturnResult;
          }
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
          if (!push_frame(*call_module, fn_obj->function_id, init_args.with_keyword_defaults(*fn_obj), fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                          std::move(call_module_owner), in.dst,
                          FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
            return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
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
    if (xlang_vm_get_init_attr(callee, init_value, init_error) && init_value.tag != ValueTag::Invalid) {
      if (auto* native = value_as_native_function(init_value)) {
        if (!instr_cache.empty()) {
          auto& cache = instr_cache[ip].call;
          cache.callee_object = callee.as.obj;
          value_assign_fast(cache.retained_callee, callee);
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
        if (allow_inline_calls && !call_args.has_keywords() &&
            !call_args.has_expansion()) {
          XlangVMFloatPointConstructorSpec point_spec;
          std::array<uint32_t, 3> slots{};
          ModuleObject* globals_module = nullptr;
          Value expected_sin = Value::invalid();
          Value expected_cos = Value::invalid();
          if (xlang_vm_analyze_float_point_constructor(
                  module, *fn_obj, point_spec) &&
              xlang_vm_prepare_float_point_constructor(
                  runtime, module, callee, *fn_obj, point_spec, slots,
                  globals_module, expected_sin, expected_cos) &&
              xlang_vm_execute_float_point_constructor(
                  instance, callee, *fn_obj, call_args, slots,
                  globals_module, globals_module->version,
                  expected_sin, expected_cos, regs[in.dst])) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.callee_object = callee.as.obj;
              value_assign_fast(cache.retained_callee, callee);
              cache.kind = CallSiteKind::InlineMathPointConstructor;
              cache.function = fn_obj;
              cache.function_code_version = fn_obj->code_version;
              cache.inline_function_id = UINT32_MAX;
              cache.native = nullptr;
              cache.class_version = klass->version;
              cache.inline_slots = slots;
              cache.inline_globals_module = globals_module;
              cache.inline_globals_version = globals_module->version;
              value_assign_fast(cache.inline_const, expected_sin);
              cache.cached_values.clear();
              cache.cached_values.push_back(expected_cos);
            }
            return XlangVMOpFlow::Next;
          }
        }
        if (allow_inline_calls && klass->restrict_instance_attrs &&
            !call_args.has_keywords() && !call_args.has_expansion()) {
          SlotConstructorSpec names;
          SlotConstructorSpec slots;
          // Do not retire an unrelated cache owner during proof publication:
          // its finalizer could mutate the class/code or enable observers.
          const bool safe_cache_payload = instr_cache.empty() ||
              ((instr_cache[ip].call.retained_callee.tag == ValueTag::Invalid ||
                value_is(instr_cache[ip].call.retained_callee, callee)) &&
               instr_cache[ip].call.inline_const.tag == ValueTag::Invalid &&
               instr_cache[ip].call.cached_values.empty());
          if (safe_cache_payload && analyze_slot_constructor_fn(module, *fn_obj, names) &&
              xlang_vm_prepare_own_canonical_slot_constructor(
                  runtime, module, callee, *fn_obj, names, slots)) {
            auto* metaclass = value_as_class(klass->metaclass);
            const uint64_t class_version = klass->version;
            const uint64_t metaclass_version = metaclass->version;
            const uint64_t code_version = fn_obj->code_version;
            Value completed;
            std::string error;
            if (xlang_vm_execute_own_canonical_slot_constructor(
                    instance, module, *fn_obj, call_args, slots, completed, error)) {
              // Class-level reuse starts only after this call has completed all
              // ordinary meta/new/abstract/init selection and the existing proof.
              xlang_vm_remember_class_canonical_slot_constructor(
                  runtime, callee, *fn_obj, slots);
              if (!instr_cache.empty()) {
                auto& cache = instr_cache[ip].call;
                cache.callee_object = callee.as.obj;
                value_assign_fast(cache.retained_callee, callee);
                cache.function = fn_obj;
                cache.function_code_version = code_version;
                cache.native = nullptr;
                cache.class_version = class_version;
                cache.arg0_object = &metaclass->header;
                cache.secondary_class_version = metaclass_version;
                cache.slot_constructor_args = std::move(slots);
                cache.kind = CallSiteKind::InlineCanonicalSlotConstructor;
              }
              // Publish the captured proof before releasing old output. The
              // owning init_value and completed instance protect reentry; a
              // finalizer mutation invalidates the already-stamped generation.
              // Returning immediately prevents stale post-finalizer publication.
              xlang_vm_retire_canonical_constructor_arguments(
                  in, fn, call_arg_regs, callee, completed, regs, ip);
              value_move_assign_fast(regs[in.dst], completed);
              return XlangVMOpFlow::Next;
            }
          }
        }
        SlotConstructorSpec slot_constructor_spec;
        if (allow_inline_calls && !call_args.has_keywords() && !call_args.has_expansion() &&
            analyze_slot_constructor_fn(module, *fn_obj, slot_constructor_spec) &&
            xlang_vm_slot_constructor_attrs_safe(module, callee, *fn_obj, slot_constructor_spec)) {
          std::string error;
          if (execute_slot_constructor_fn(
                  instance, module, *fn_obj, call_args, slot_constructor_spec,
                  regs[in.dst], error)) {
            if (!instr_cache.empty()) {
              auto& cache = instr_cache[ip].call;
              cache.callee_object = callee.as.obj;
              value_assign_fast(cache.retained_callee, callee);
              cache.kind = CallSiteKind::InlineSlotConstructor;
              cache.function = fn_obj;
              cache.function_code_version = fn_obj->code_version;
              cache.inline_function_id = UINT32_MAX;
              cache.native = nullptr;
              cache.class_version = klass->version;
              cache.slot_constructor_args = slot_constructor_spec;
            }
            return XlangVMOpFlow::Next;
          }
        }
        if (!instr_cache.empty()) {
          auto& cache = instr_cache[ip].call;
          cache.callee_object = callee.as.obj;
          value_assign_fast(cache.retained_callee, callee);
          cache.kind = CallSiteKind::UserConstructor;
          cache.function = fn_obj;
          cache.function_code_version = fn_obj->code_version;
          cache.inline_function_id = UINT32_MAX;
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
        if (!push_frame(*call_module, fn_obj->function_id, init_args.with_keyword_defaults(*fn_obj), fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                        std::move(call_module_owner), in.dst,
                        FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
          return result.errors.empty() ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
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
      value_assign_fast(cache.retained_callee, callee);
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
    if (value_as_event(callee) != nullptr) {
      std::string error;
      const Value* args = materialize_native_args(call_args, native_call_args);
      if (!event_fire(runtime, callee, args, static_cast<uint32_t>(call_args.size()), regs[in.dst], error)) {
        return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return XlangVMOpFlow::Next;
    }
    if (value_as_instance(callee) != nullptr) {
      Value call_attr;
      std::string attr_error;
      if (object_get_special_method(runtime, callee, "__call__", call_attr, attr_error)) {
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
  NativeCallArgsScope args_scope{native_call_args};
  std::string& error = xlang_vm_native_error_scratch();
  const bool needs_materialized_expansion = values.has_expansion();
  const bool use_fast = native->fast_callback != nullptr && !needs_materialized_expansion;
  xlang_perf_count_native_call(use_fast);
  xlang_perf_count_native_name(native->name, use_fast);
  const Value* native_args = nullptr;
  uint32_t native_argc = static_cast<uint32_t>(values.size());
  if (needs_materialized_expansion) {
    std::vector<NativeKeywordArg> native_keyword_args;
    bool has_keywords = false;
    native_args = materialize_native_call_ex(runtime, values, native_call_args, native_keyword_args, has_keywords, error);
    if (native_args == nullptr && !error.empty()) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (raise_exception_value(std::move(pending))) return false;
        return false;
      }
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
  Value code = Value::none();
  const int64_t native_monitoring_mask =
      kSysMonitoringEventCall | kSysMonitoringEventCRaise | kSysMonitoringEventCReturn;
  const bool native_monitoring_possible =
      sys_monitoring_event_may_dispatch(native_monitoring_mask);
  const bool monitor_call = native_monitoring_possible &&
      sys_monitoring_global_event_may_dispatch(kSysMonitoringEventCall);
  const bool monitor_raise = native_monitoring_possible &&
      sys_monitoring_global_event_may_dispatch(kSysMonitoringEventCRaise);
  const bool monitor_return = native_monitoring_possible &&
      sys_monitoring_global_event_may_dispatch(kSysMonitoringEventCReturn);
  const bool monitoring_enabled = monitor_call || monitor_raise || monitor_return;
  bool profile_enabled = false;
  if (runtime.profile_event_may_dispatch()) {
    const Value& profile_hook = runtime.profile_function();
    profile_enabled = profile_hook.tag != ValueTag::Invalid &&
                      profile_hook.tag != ValueTag::None &&
                      !runtime.profile_dispatch_active();
  }
  Value callable;
  Value profile_frame;
  if (monitoring_enabled || profile_enabled) {
    callable = Value::string(native->name);
    if (profile_enabled) {
      profile_frame = runtime.current_frame_snapshot();
    }
    if (monitor_call &&
        !sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCall, code, -1, &callable, error)) {
      if (raise_runtime_error(error.empty() ? "monitoring callback failed" : error)) return false;
      return false;
    }
    if (profile_enabled && !runtime.emit_profile_event_for_frame(profile_frame, "c_call", callable, error)) {
      if (raise_runtime_error(error.empty() ? "profile callback failed" : error)) return false;
      return false;
    }
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
    if (error.rfind("invalid builtin method fast call", 0) == 0) {
      error += " for '" + native->name + "'";
    }
    if (monitor_raise) {
      std::string monitoring_error;
      (void)sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCRaise, code, -1, &callable, monitoring_error);
    }
    if (profile_enabled) {
      std::string profile_error;
      (void)runtime.emit_profile_event_for_frame(profile_frame, "c_exception", callable, profile_error);
    }
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      if (raise_exception_value(std::move(pending))) return false;
      return false;
    }
    if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
    return false;
  }
  if (monitor_return && !sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCReturn, code, -1, &callable, error)) {
    if (raise_runtime_error(error.empty() ? "monitoring callback failed" : error)) return false;
    return false;
  }
  if (profile_enabled && !runtime.emit_profile_event_for_frame(profile_frame, "c_return", callable, error)) {
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
    const Value* monitoring_code,
    int64_t monitoring_instruction_offset,
    const Value* observed_callable) {
  NativeCallArgsScope args_scope{native_call_args};
  std::string error;
  auto raise_observer_failure = [&](const std::string& diagnostic) -> bool {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      (void)raise_exception_value(std::move(pending));
      return false;
    }
    (void)raise_runtime_error(diagnostic.empty() ? "native observer callback failed" : diagnostic);
    return false;
  };
  bool has_materialized_keywords = false;
  const bool needs_materialized_ex = values.has_keywords() || values.has_expansion();
  const bool use_fast_keyword = native->fast_keyword_predicate != nullptr &&
      native->fast_keyword_callback != nullptr &&
      native->fast_keyword_predicate(values);
  CallArgsView fast_values = values;
  bool fast_empty_star_expansion = false;
  const bool has_positional_star = values.star_arg != UINT32_MAX ||
      (values.star_args != nullptr && !values.star_args->empty());
  // Check for a positional expansion first: ordinary native calls take this
  // path often and should pay only the cheap expansion-presence checks.
  if (native->fast_callback != nullptr && has_positional_star &&
      !values.has_keywords() &&
      values.kw_star_arg == UINT32_MAX &&
      (values.kw_star_args == nullptr || values.kw_star_args->empty()) &&
      values.registers != nullptr) {
    // `Handle._run` commonly calls `Context.run(callback, *args)` with an
    // exact empty tuple. Expanding an exact empty list/tuple has no user-code
    // effects, so skip iterator setup and keep this native callback on its
    // stack-backed fast-call path. Subclasses and arbitrary iterables still
    // take the general expansion path, where their iteration hooks run.
    auto exact_empty_sequence = [&](uint32_t reg) {
      const Value& value = values.registers[reg];
      if (const auto* tuple = value_as_tuple(value)) return tuple->items.empty();
      if (const auto* list = value_as_list(value)) return list->items.empty();
      return false;
    };
    fast_empty_star_expansion = true;
    if (values.star_args != nullptr && !values.star_args->empty()) {
      for (uint32_t reg : *values.star_args) {
        if (!exact_empty_sequence(reg)) {
          fast_empty_star_expansion = false;
          break;
        }
      }
    } else if (!exact_empty_sequence(values.star_arg)) {
      fast_empty_star_expansion = false;
    }
    if (fast_empty_star_expansion) {
      fast_values.star_arg = UINT32_MAX;
      fast_values.kw_star_arg = UINT32_MAX;
      fast_values.star_args = nullptr;
      fast_values.kw_star_args = nullptr;
    }
  }
  const bool use_fast = native->fast_callback != nullptr &&
      (!needs_materialized_ex || fast_empty_star_expansion);
  const bool use_fast_native_call = use_fast || use_fast_keyword;
  xlang_perf_count_native_call(use_fast_native_call);
  xlang_perf_count_native_name(native->name, use_fast_native_call);
  const Value* native_args = nullptr;
  if (needs_materialized_ex && !fast_empty_star_expansion && !use_fast_keyword) {
    native_args = materialize_native_call_ex(runtime, values, native_call_args, native_keyword_args, has_materialized_keywords, error);
    if (native_args == nullptr && !error.empty()) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (raise_exception_value(std::move(pending))) return false;
        return false;
      }
      if (raise_runtime_error(error)) return false;
      return false;
    }
    if (has_materialized_keywords && native->keyword_callback == nullptr) {
      if (raise_runtime_error("native function '" + native->name + "' does not accept keyword arguments")) return false;
      return false;
    }
  } else if (!use_fast && !use_fast_keyword) {
    native_args = materialize_native_args(values, native_call_args);
  }
  auto monitoring_event_enabled = [&](int64_t event) {
    if (monitoring_code == nullptr) return false;
    auto* code_object = value_as_code(*monitoring_code);
    return code_object != nullptr
        ? sys_monitoring_location_may_dispatch(
              code_object->module.get(), code_object->function_id, event,
              monitoring_instruction_offset)
        : sys_monitoring_global_event_may_dispatch(event);
  };
  const int64_t native_monitoring_mask =
      kSysMonitoringEventCall | kSysMonitoringEventCRaise | kSysMonitoringEventCReturn;
  const bool native_monitoring_possible =
      sys_monitoring_event_may_dispatch(native_monitoring_mask);
  const bool monitor_call = native_monitoring_possible &&
      monitoring_event_enabled(kSysMonitoringEventCall);
  const bool monitor_raise = native_monitoring_possible &&
      monitoring_event_enabled(kSysMonitoringEventCRaise);
  const bool monitor_return = native_monitoring_possible &&
      monitoring_event_enabled(kSysMonitoringEventCReturn);
  const bool monitoring_enabled = monitor_call || monitor_raise || monitor_return;
  bool profile_enabled = false;
  if (runtime.profile_event_may_dispatch()) {
    const Value& profile_hook = runtime.profile_function();
    profile_enabled = profile_hook.tag != ValueTag::Invalid &&
                      profile_hook.tag != ValueTag::None &&
                      !runtime.profile_dispatch_active();
  }
  Value callable;
  Value profile_frame;
  if (monitoring_enabled || profile_enabled) {
    if (observed_callable != nullptr) {
      value_assign_fast(callable, *observed_callable);
    } else {
      callable = Value::string(native->name);
    }
  }
  if (profile_enabled) {
    profile_frame = runtime.current_frame_snapshot();
  }
  if (monitor_call) {
    std::string monitoring_error;
    if (!sys_monitoring_dispatch_event(
            runtime,
            kSysMonitoringEventCall,
            *monitoring_code,
            monitoring_instruction_offset,
            &callable,
            monitoring_error)) {
      if (observed_callable != nullptr) return raise_observer_failure(monitoring_error);
      if (raise_runtime_error(monitoring_error.empty() ? "monitoring callback failed" : monitoring_error)) return false;
      return false;
    }
  }
  if (profile_enabled && !runtime.emit_profile_event_for_frame(profile_frame, "c_call", callable, error)) {
    if (observed_callable != nullptr) return raise_observer_failure(error);
    if (raise_runtime_error(error.empty() ? "profile callback failed" : error)) return false;
    return false;
  }
  bool ok = false;
  if (use_fast_keyword) {
    Value native_result;
    ok = native->fast_keyword_callback(
        runtime, values, execution_lock, native_result, error, native->user_data);
    if (ok) out = std::move(native_result);
  } else if (use_fast && !native->fast_releases_vm_lock) {
    ok = native->fast_callback(
        runtime,
        fast_values.leading,
        fast_values.leading_count,
        fast_values.registers,
        fast_values.register_args == nullptr ? nullptr : fast_values.register_args->data(),
        fast_values.register_args == nullptr ? 0 : static_cast<uint32_t>(fast_values.register_args->size()),
        out,
        error,
        native->user_data);
  } else {
    Value native_result;
    execution_lock.unlock();
    if (use_fast) {
      ok = native->fast_callback(
          runtime,
          fast_values.leading,
          fast_values.leading_count,
          fast_values.registers,
          fast_values.register_args == nullptr ? nullptr : fast_values.register_args->data(),
          fast_values.register_args == nullptr ? 0 : static_cast<uint32_t>(fast_values.register_args->size()),
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
    // Observers may enter Python/native code and replace the shared pending
    // slot. Keep the native failure owned until successful events finish; an
    // observer failure deliberately propagates the observer's own exception.
    Value observed_pending;
    if (observed_callable != nullptr) {
      (void)runtime.take_pending_exception(observed_pending);
    }
    if (monitor_raise) {
      std::string monitoring_error;
      if (!sys_monitoring_dispatch_event(
              runtime,
              kSysMonitoringEventCRaise,
              *monitoring_code,
              monitoring_instruction_offset,
              &callable,
              monitoring_error)) {
        if (observed_callable != nullptr) return raise_observer_failure(monitoring_error);
        if (raise_runtime_error(monitoring_error.empty() ? "monitoring callback failed" : monitoring_error)) return false;
        return false;
      }
    }
    if (profile_enabled) {
      std::string profile_error;
      const bool profile_ok = runtime.emit_profile_event_for_frame(
          profile_frame, "c_exception", callable, profile_error);
      if (!profile_ok && observed_callable != nullptr) return raise_observer_failure(profile_error);
    }
    if (observed_pending.tag != ValueTag::Invalid) {
      Value observer_pending;
      (void)runtime.take_pending_exception(observer_pending);
      (void)raise_exception_value(std::move(observed_pending));
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
  if (monitor_return) {
    std::string monitoring_error;
    if (!sys_monitoring_dispatch_event(
            runtime,
            kSysMonitoringEventCReturn,
            *monitoring_code,
            monitoring_instruction_offset,
            &callable,
            monitoring_error)) {
      if (observed_callable != nullptr) return raise_observer_failure(monitoring_error);
      if (raise_runtime_error(monitoring_error.empty() ? "monitoring callback failed" : monitoring_error)) return false;
      return false;
    }
  }
  if (profile_enabled && !runtime.emit_profile_event_for_frame(profile_frame, "c_return", callable, error)) {
    if (observed_callable != nullptr) return raise_observer_failure(error);
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
    FrameReturnMode return_mode,
    Value continuation_value,
    Runtime* ordinary_call_runtime) {
  const ir::Module* call_module = &module;
  auto call_module_owner = module_owner;
  if (fn_obj->module != nullptr) {
    call_module = fn_obj->module.get();
    call_module_owner = fn_obj->module;
  }
  if (fn_obj->function_id >= call_module->functions.size() ||
      call_module->functions[fn_obj->function_id].is_generator) {
    // Ordinary Python calls dominate this path. Generator construction needs
    // the extra callback only for generator IR or invalid IDs (which retain
    // the callback's existing error); reuse the selected module for the frame.
    bool made_generator = false;
    if (!make_generator_if_needed(fn_obj, values, out, made_generator)) {
      return false;
    }
    if (made_generator) {
      return true;
    }
  }
  if (ordinary_call_runtime != nullptr &&
      return_mode == FrameReturnMode::StoreReturnValue &&
      continuation_value.tag == ValueTag::Invalid) {
    XlangVMCapturedItemFunctionSpec spec;
    if (xlang_vm_analyze_captured_item_function(
            *call_module, *fn_obj, static_cast<uint32_t>(values.size()), spec) &&
        !values.has_keywords() && !values.has_expansion() &&
        interpreter_pending_events() == 0 &&
        inline_python_function_allowed(*ordinary_call_runtime, *call_module, *fn_obj)) {
      const auto* cell = value_as_cell(fn_obj->closure[spec.free_slot]);
      // Indexed binding already avoids name lookup; this proven hit also
      // avoids a frame's register/local setup and ownership traffic. Analyze
      // current code and resolve the live cell even on warmed call sites.
      // Opt in only ordinary CALL contexts: their Next path refreshes
      // monitoring after output finalizers. Property/subscript/constructor
      // contexts retain frames and their existing return/refresh semantics.
      // A miss or user protocol must execute the original Python frame once.
      if (cell != nullptr && mapping_get_intrinsic_item_if_present(
              cell->value, values.get(spec.argument), out)) return true;
    }
  }
  ++ip;
  if (!push_frame(*call_module, fn_obj->function_id, values.with_keyword_defaults(*fn_obj), fn_obj->closure, fn_obj->defaults,
                  fn_obj->globals_module, std::move(call_module_owner), return_dst, return_mode,
                  std::move(continuation_value))) {
    return false;
  }
  pushed_frame = true;
  return true;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
inline bool call_callable_value(
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
  if (auto* alias = value_as_generic_alias(function_value)) {
    return call_callable_value(
        runtime, alias->origin, values, module, module_owner, return_dst, ip, native_call_args,
        execution_lock, out, pushed_frame, make_generator_if_needed, push_frame,
        raise_runtime_error, raise_exception_value);
  }
  if (auto* bound = value_as_bound_method(function_value)) {
    CallArgsView bound_args = values;
    std::vector<Value> combined_leading;
    if (values.leading_count == 0) {
      bound_args.leading = &bound->self;
      bound_args.leading_count = 1;
    } else {
      combined_leading.reserve(static_cast<size_t>(values.leading_count) + 1);
      combined_leading.push_back(bound->self);
      for (uint32_t i = 0; i < values.leading_count; ++i) {
        combined_leading.push_back(values.leading[i]);
      }
      bound_args.leading = combined_leading.data();
      bound_args.leading_count = static_cast<uint32_t>(combined_leading.size());
    }
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
  if (value_as_class(function_value) != nullptr) {
    // This synchronous fallback owns packed argument copies, including class
    // attributes passed to weakref.ref. Release them on both exits; retaining
    // scratch capacity must not root a referent until its caller frame returns.
    NativeCallArgsScope args_scope{native_call_args};
    std::string error;
    const Value* args = materialize_native_args(values, native_call_args);
    if (!runtime_call_callable(runtime, function_value, args,
            static_cast<uint32_t>(values.size()), out, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) raise_exception_value(std::move(pending));
      else raise_runtime_error(error);
      return false;
    }
    return true;
  }
  if (value_as_event(function_value) != nullptr) {
    std::string error;
    const Value* args = materialize_native_args(values, native_call_args);
    if (!event_fire(runtime, function_value, args, static_cast<uint32_t>(values.size()), out, error)) {
      return raise_runtime_error(error);
    }
    return true;
  }
  if (value_as_instance(function_value) != nullptr) {
    Value call_attr;
    std::string attr_error;
    if (!object_get_special_method(runtime, function_value, "__call__", call_attr, attr_error)) {
      if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return false;
      return false;
    }
    return call_callable_value(
        runtime,
        call_attr,
        values,
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
    std::vector<VMFrame>& frames,
    size_t& frame_count,
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
  if (runtime_value_contains(runtime, regs[in.b], regs[in.a], contains_value, error)) {
    value_set_bool(regs[in.dst], contains_value != (in.c != 0));
    return XlangVMOpFlow::Next;
  }

  // runtime_value_contains() may have called a native __contains__ method.
  // Do not discard its pending exception and invoke that method a second time:
  // doing so both repeats user comparisons and bypasses the surrounding
  // exception handler (notably deque's mutation-during-search guard).
  Value pending;
  if (runtime.take_pending_exception(pending)) {
    return raise_exception_value(std::move(pending))
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }

  Value contains_method;
  std::string attr_error;
  if (!object_get_attr(regs[in.b], "__contains__", contains_method, attr_error)) {
    const std::string message = error.empty() ? attr_error : error;
    return raise_exception_value(runtime.make_exception("TypeError", message))
        ? XlangVMOpFlow::ContinueLoop
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
    frames[frame_count - 1].return_mode = in.c != 0 ?
        FrameReturnMode::StoreNegatedBoolean : FrameReturnMode::StoreBoolean;
    return XlangVMOpFlow::SwitchFrame;
  }
  value_set_bool(regs[in.dst], value_truthy(regs[in.dst]) != (in.c != 0));
  return XlangVMOpFlow::Next;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
inline bool call_callable_value_ex(
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
    const Value* monitoring_code,
    int64_t monitoring_instruction_offset) {
  if (auto* alias = value_as_generic_alias(function_value)) {
    return call_callable_value_ex(
        runtime, alias->origin, values, module, module_owner, return_dst, ip, native_call_args,
        native_keyword_args, execution_lock, out, pushed_frame, make_generator_if_needed,
        push_frame, raise_runtime_error, raise_exception_value, monitoring_code,
        monitoring_instruction_offset);
  }
  if (auto* bound = value_as_bound_method(function_value)) {
    CallArgsView bound_args = values;
    std::vector<Value> combined_leading;
    if (values.leading_count == 0) {
      bound_args.leading = &bound->self;
      bound_args.leading_count = 1;
    } else {
      combined_leading.reserve(static_cast<size_t>(values.leading_count) + 1);
      combined_leading.push_back(bound->self);
      for (uint32_t i = 0; i < values.leading_count; ++i) {
        combined_leading.push_back(values.leading[i]);
      }
      bound_args.leading = combined_leading.data();
      bound_args.leading_count = static_cast<uint32_t>(combined_leading.size());
    }
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
  if (value_as_class(function_value) != nullptr) {
    bool has_keywords = false;
    std::string error;
    const Value* args = materialize_native_call_ex(
        runtime, values, native_call_args, native_keyword_args, has_keywords, error);
    if (args == nullptr && !error.empty()) {
      Value pending;
      if (runtime.take_pending_exception(pending)) raise_exception_value(std::move(pending));
      else raise_runtime_error(error);
      return false;
    }
    std::vector<std::pair<std::string, Value>> kwargs;
    kwargs.reserve(native_keyword_args.size());
    for (const auto& item : native_keyword_args) {
      kwargs.push_back({item.name == nullptr ? "" : item.name,
                        item.value == nullptr ? Value::none() : *item.value});
    }
    if (!runtime_call_callable_kw(runtime, function_value, args,
            static_cast<uint32_t>(native_call_args.size()), kwargs, out, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) raise_exception_value(std::move(pending));
      else raise_runtime_error(error);
      return false;
    }
    return true;
  }
  if (auto* fn_obj = value_as_function(function_value)) {
    return call_user_function(fn_obj, values, module, module_owner, return_dst, ip, out, pushed_frame,
                              make_generator_if_needed, push_frame);
  }
  if (value_as_instance(function_value) != nullptr) {
    Value call_attr;
    std::string attr_error;
    if (!object_get_special_method(runtime, function_value, "__call__", call_attr, attr_error)) {
      if (xlang_vm_raise_not_callable(runtime, raise_exception_value)) return false;
      return false;
    }
    return call_callable_value_ex(
        runtime,
        call_attr,
        values,
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
  if (value_as_event(function_value) != nullptr) {
    std::string error;
    const Value* args = materialize_native_args(values, native_call_args);
    if (!event_fire(runtime, function_value, args, static_cast<uint32_t>(values.size()), out, error)) {
      return raise_runtime_error(error);
    }
    return true;
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
    const Value& monitoring_code,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    const Value& globals_module,
    XlangVMInstrCacheStorage& instr_cache,
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
  if (globals_module_obj == nullptr) {
    return raise_runtime_error("module slot is not bound") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  uint32_t resolved_module_slot = 0;
  auto& global_cache = instr_cache[ip].global;
  // in.a names a compiled global, not the live Module's physical slot. Reuse
  // only that binding's index under the same mutation guard as LoadModuleSlot;
  // always reread its live Value before the existing export/call dispatch.
  // The frame owns globals_module throughout this activation, and frame pop
  // clears the global payload even when a scalar CallMethod proof survives.
  // Do not retain a receiver/callee or carry this local-version proof across
  // activations: Module versions alone do not prevent pointer-reuse ABA.
  if (global_cache.kind == 1 && global_cache.version == globals_module_obj->version &&
      global_cache.slot < globals_module_obj->slots.size() &&
      globals_module_obj->slots[global_cache.slot].tag != ValueTag::Invalid) {
    resolved_module_slot = global_cache.slot;
  } else {
    std::string module_slot_error;
    if (!module_find_attr_slot(globals_module, module.global_slots[in.a], resolved_module_slot, module_slot_error) ||
        resolved_module_slot >= globals_module_obj->slots.size()) {
      return raise_runtime_error("module slot is not bound") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    // Publish scalar binding metadata before any export/property/call callback.
    global_cache.kind = 1;
    global_cache.slot = resolved_module_slot;
    global_cache.version = globals_module_obj->version;
  }
  const auto& module_value = globals_module_obj->slots[resolved_module_slot];
  auto* module_object = value_as_module(module_value);
  const auto& call_arg_regs = fn.call_args[in.c];
  CallArgsView call_args;
  call_args.registers = regs.value_data();
  call_args.register_args = &call_arg_regs;
  if (module_object == nullptr) {
    Value callee;
    std::string attr_error;
    const Value* getattr_builtin = runtime.find_builtin("getattr");
    const Value getattr_args[] = {module_value, Value::string(fn.names[in.b])};
    if (getattr_builtin == nullptr ||
        !runtime_call_callable(runtime, *getattr_builtin, getattr_args, 2,
                               callee, attr_error)) {
      Value pending;
      if (runtime.take_pending_exception(pending))
        return raise_exception_value(std::move(pending))
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      return raise_exception_value(runtime.make_exception("AttributeError", attr_error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    std::vector<NativeKeywordArg> native_keyword_args;
    bool pushed_frame = false;
    if (!call_callable_value_ex(
            runtime, callee, call_args, module, module_owner, in.dst, ip,
            native_call_args, native_keyword_args, execution_lock, regs[in.dst],
            pushed_frame, make_generator_if_needed, push_frame,
            raise_runtime_error, raise_exception_value)) {
      return result.errors.empty()
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
  }

  auto monitoring_event_enabled = [&](int64_t event) {
    auto* code_object = value_as_code(monitoring_code);
    return code_object != nullptr
        ? sys_monitoring_location_may_dispatch(
              code_object->module.get(), code_object->function_id, event,
              static_cast<int64_t>(ip))
        : sys_monitoring_global_event_may_dispatch(event);
  };
  const int64_t native_monitoring_mask =
      kSysMonitoringEventCall | kSysMonitoringEventCReturn | kSysMonitoringEventCRaise;
  const bool native_monitoring_enabled =
      sys_monitoring_event_may_dispatch(native_monitoring_mask) &&
      (monitoring_event_enabled(kSysMonitoringEventCall) ||
       monitoring_event_enabled(kSysMonitoringEventCReturn) ||
       monitoring_event_enabled(kSysMonitoringEventCRaise));

  auto materialize_native_args = [&](CallArgsView values) -> const Value* {
    native_call_args.clear();
    native_call_args.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      native_call_args.push_back(values.get(i));
    }
    return native_call_args.data();
  };

  auto dispatch_native_monitoring_event = [&](int64_t event, const Value& callable) -> bool {
    if (!native_monitoring_enabled) return true;
    const bool enabled = monitoring_event_enabled(event);
    if (!enabled) return true;
    std::string monitoring_error;
    if (!sys_monitoring_dispatch_event(runtime, event, monitoring_code, static_cast<int64_t>(ip), &callable, monitoring_error)) {
      return raise_runtime_error(monitoring_error) ? false : false;
    }
    return true;
  };

  auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
    std::string error;
    Value native_result;
    Value callable = native_monitoring_enabled ? Value::string(native->name) : Value::none();
    if (!dispatch_native_monitoring_event(kSysMonitoringEventCall, callable)) {
      return false;
    }
    const bool use_fast = native->fast_callback != nullptr;
    xlang_perf_count_native_call(use_fast);
    xlang_perf_count_native_name(native->name, use_fast);
    const Value* native_args = nullptr;
    if (!use_fast) {
      native_args = materialize_native_args(values);
    }
    bool ok = false;
    if (use_fast && !native->fast_releases_vm_lock) {
      ok = native->fast_callback(
          runtime, values.leading, values.leading_count, values.registers,
          values.register_args == nullptr ? nullptr : values.register_args->data(),
          values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
          native_result, error, native->user_data);
    } else {
      execution_lock.unlock();
      ok = use_fast
          ? native->fast_callback(
                runtime, values.leading, values.leading_count, values.registers,
                values.register_args == nullptr ? nullptr : values.register_args->data(),
                values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                native_result, error, native->user_data)
          : native->callback != nullptr && native->callback(
                runtime, native_args, static_cast<uint32_t>(values.size()),
                native_result, error, native->user_data);
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
    Value callable = native_monitoring_enabled && cache.native != nullptr
        ? Value::string(cache.native->name) : Value::none();
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
    Value module_getattr;
    std::string getattr_error;
    if (module_get_attr(module_value, "__getattr__", module_getattr,
                        getattr_error)) {
      Value attr_arg = Value::string(name);
      Value dynamic_callee;
      std::string call_error;
      if (!runtime_call_callable(runtime, module_getattr, &attr_arg, 1,
                                 dynamic_callee, call_error)) {
        Value pending;
        if (runtime.take_pending_exception(pending))
          return raise_exception_value(std::move(pending))
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        return raise_runtime_error(call_error)
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      std::vector<NativeKeywordArg> native_keyword_args;
      bool pushed_frame = false;
      if (!call_callable_value_ex(
              runtime, dynamic_callee, call_args, module, module_owner,
              in.dst, ip, native_call_args, native_keyword_args,
              execution_lock, regs[in.dst], pushed_frame,
              make_generator_if_needed, push_frame, raise_runtime_error,
              raise_exception_value)) {
        return result.errors.empty()
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
    }
    return raise_exception_value(runtime.make_exception(
               "AttributeError",
               module_error.empty() ? "module method not found" : module_error))
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  Value property_value;
  const auto* property = value_as_property(module_object->slots[module_slot]);
  const bool module_property = property && property->native_module_runtime;
  if (module_property && !module_get_attr(module_value, name, property_value, module_error)) {
    return raise_runtime_error(module_error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const Value& callee = module_property ? property_value : module_object->slots[module_slot];
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
      if (auto* metaclass = value_as_class(klass->metaclass);
          metaclass != nullptr && metaclass->name != "type") {
        Value meta_call;
        std::string meta_call_error;
        if (class_get_bound_attr(
                runtime, klass->metaclass, callee, "__call__", meta_call, meta_call_error)) {
          bool pushed_frame = false;
          CallArgsView meta_call_args = call_args;
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
      Value new_callable;
      if (xlang_vm_resolve_class_new_callable(runtime, callee, klass, new_callable)) {
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
      XlangVMBuiltinConstructorError constructor_error;
      if (call_builtin_type_constructor(runtime, *klass, call_args, execution_lock, regs[in.dst], constructor_error)) {
        if (constructor_error.fully_handled) {
          return finish_fully_handled_native_constructor(
              runtime, constructor_error, raise_exception_value);
        }
        if (value_as_class(regs[in.dst]) == nullptr) {
          return XlangVMOpFlow::Next;
        }
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
      Value pending_constructor_exception;
      if (runtime.take_pending_exception(pending_constructor_exception)) {
        return raise_exception_value(std::move(pending_constructor_exception))
            ? XlangVMOpFlow::ContinueLoop
            : XlangVMOpFlow::ReturnResult;
      }
      if (!constructor_error.message.empty()) {
        return raise_exception_value(runtime.make_exception(constructor_error.type, constructor_error.message))
            ? XlangVMOpFlow::ContinueLoop
            : XlangVMOpFlow::ReturnResult;
      }
      Value instance = Value::instance(callee);
      initialize_exception_call_args(runtime, instance, call_args);
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
  if (!module_property && !instr_cache.empty()) {
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

template <
    typename MakeGeneratorIfNeeded, typename PushFrame,
    typename CallBuiltinTypeConstructor, typename AnalyzeArgBinaryFunction,
    typename ExecuteArgBinaryFunction, typename AnalyzeSlotConstructor,
    typename ExecuteSlotConstructor, typename RaiseRuntimeError,
    typename RaiseExceptionValue, typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow call_local(
    const ir::Instr& in, const ir::Function& fn, const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner, Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs, XlangVMSmallValueBuffer& locals,
    XlangVMInstrCacheStorage& instr_cache, std::vector<Value>& native_call_args,
    size_t& ip, RuntimeResult& result, XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed, PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor,
    AnalyzeArgBinaryFunction&& analyze_arg_binary_function,
    ExecuteArgBinaryFunction&& execute_arg_binary_function,
    AnalyzeSlotConstructor&& analyze_slot_constructor,
    ExecuteSlotConstructor&& execute_slot_constructor,
    RaiseRuntimeError&& raise_runtime_error, RaiseExceptionValue&& raise_exception_value,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  if (in.a >= locals.size() || in.dst >= regs.size()) {
    result.errors.push_back("invalid local slot in call");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], locals[in.a]);
  const ir::Instr call_in{ir::Op::Call, in.dst, in.dst, in.b, 0};
  return call(
      call_in, fn, module, module_owner, runtime, regs, instr_cache,
      native_call_args, ip, result, execution_lock,
      std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
      std::forward<PushFrame>(push_frame),
      std::forward<CallBuiltinTypeConstructor>(call_builtin_type_constructor),
      std::forward<AnalyzeArgBinaryFunction>(analyze_arg_binary_function),
      std::forward<ExecuteArgBinaryFunction>(execute_arg_binary_function),
      std::forward<AnalyzeSlotConstructor>(analyze_slot_constructor),
      std::forward<ExecuteSlotConstructor>(execute_slot_constructor),
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
}

template <
    typename MakeGeneratorIfNeeded, typename PushFrame,
    typename CallBuiltinTypeConstructor, typename AnalyzeArgBinaryFunction,
    typename ExecuteArgBinaryFunction, typename AnalyzeSlotConstructor,
    typename ExecuteSlotConstructor, typename RaiseRuntimeError,
    typename RaiseExceptionValue, typename RaiseNameError>
XLANG3_HOT_INLINE XlangVMOpFlow call_global(
    const ir::Instr& in, const ir::Function& fn, const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner, Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs, Value& globals_module,
    std::unordered_map<std::string, Value>& globals, uint64_t& globals_version,
    XlangVMInstrCacheStorage& instr_cache, std::vector<Value>& native_call_args,
    size_t& ip, RuntimeResult& result, XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed, PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor,
    AnalyzeArgBinaryFunction&& analyze_arg_binary_function,
    ExecuteArgBinaryFunction&& execute_arg_binary_function,
    AnalyzeSlotConstructor&& analyze_slot_constructor,
    ExecuteSlotConstructor&& execute_slot_constructor,
    RaiseRuntimeError&& raise_runtime_error, RaiseExceptionValue&& raise_exception_value,
    RaiseNameError&& raise_name_error) {
  const ir::Instr load_in{ir::Op::LoadGlobal, in.dst, in.a, 0, 0};
  const auto load_flow = load_global(
      load_in, fn, runtime, regs, globals_module, globals, globals_version,
      instr_cache[ip], result, raise_name_error, raise_exception_value);
  if (load_flow != XlangVMOpFlow::Next) return load_flow;
  const ir::Instr call_in{ir::Op::Call, in.dst, in.dst, in.b, 0};
  return call(
      call_in, fn, module, module_owner, runtime, regs, instr_cache,
      native_call_args, ip, result, execution_lock,
      std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
      std::forward<PushFrame>(push_frame),
      std::forward<CallBuiltinTypeConstructor>(call_builtin_type_constructor),
      std::forward<AnalyzeArgBinaryFunction>(analyze_arg_binary_function),
      std::forward<ExecuteArgBinaryFunction>(execute_arg_binary_function),
      std::forward<AnalyzeSlotConstructor>(analyze_slot_constructor),
      std::forward<ExecuteSlotConstructor>(execute_slot_constructor),
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
}

template <
    typename MakeGeneratorIfNeeded, typename PushFrame,
    typename CallBuiltinTypeConstructor, typename AnalyzeConstMethod,
    typename AnalyzeSelfBinaryMethod, typename ExecuteSelfBinaryMethod,
    typename AnalyzeSelfSlotMethod, typename ExecuteSelfSlotMethod,
    typename AnalyzeSelfSlotConstSumMethod, typename ExecuteSelfSlotConstSumMethod,
    typename RaiseRuntimeError, typename RaiseExceptionValue,
    typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow call_local_method(
    const ir::Instr& in, const ir::Function& fn, const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner, Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs, XlangVMSmallValueBuffer& locals,
    XlangVMInstrCacheStorage& instr_cache, std::vector<Value>& native_call_args,
    size_t& ip, RuntimeResult& result, XlangRuntimeExecutionGuard& execution_lock,
    MakeGeneratorIfNeeded&& make_generator_if_needed, PushFrame&& push_frame,
    CallBuiltinTypeConstructor&& call_builtin_type_constructor,
    AnalyzeConstMethod&& analyze_const_method,
    AnalyzeSelfBinaryMethod&& analyze_self_binary_method,
    ExecuteSelfBinaryMethod&& execute_self_binary_method,
    AnalyzeSelfSlotMethod&& analyze_self_slot_method,
    ExecuteSelfSlotMethod&& execute_self_slot_method,
    AnalyzeSelfSlotConstSumMethod&& analyze_self_slot_const_sum_method,
    ExecuteSelfSlotConstSumMethod&& execute_self_slot_const_sum_method,
    RaiseRuntimeError&& raise_runtime_error, RaiseExceptionValue&& raise_exception_value,
    RaiseUnboundLocalError&& raise_unbound_local_error,
    const Value* monitoring_code = nullptr, size_t vm_frame_count = 0) {
  if (in.a >= locals.size() || in.dst >= regs.size()) {
    result.errors.push_back("invalid local slot in method call");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], locals[in.a]);
  const ir::Instr call_in{ir::Op::CallMethod, in.dst, in.dst, in.b, in.c};
  return call_method(
      call_in, fn, module, module_owner, runtime, regs, instr_cache,
      native_call_args, ip, result, execution_lock,
      std::forward<MakeGeneratorIfNeeded>(make_generator_if_needed),
      std::forward<PushFrame>(push_frame),
      std::forward<CallBuiltinTypeConstructor>(call_builtin_type_constructor),
      std::forward<AnalyzeConstMethod>(analyze_const_method),
      std::forward<AnalyzeSelfBinaryMethod>(analyze_self_binary_method),
      std::forward<ExecuteSelfBinaryMethod>(execute_self_binary_method),
      std::forward<AnalyzeSelfSlotMethod>(analyze_self_slot_method),
      std::forward<ExecuteSelfSlotMethod>(execute_self_slot_method),
      std::forward<AnalyzeSelfSlotConstSumMethod>(analyze_self_slot_const_sum_method),
      std::forward<ExecuteSelfSlotConstSumMethod>(execute_self_slot_const_sum_method),
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value), monitoring_code, vm_frame_count);
}

} // namespace xlang3::xlang_vm::ops
