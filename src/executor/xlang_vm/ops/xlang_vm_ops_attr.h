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
#include "../xlang_vm_op_switch.h"
#include "../xlang_vm_property_inline.h"

#include "runtime_lock.h"
#include "xlang_vm_ops_call.h"

#include "xlang3/attribute.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <memory>
#include <string>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE bool xlang_vm_is_default_object_hook(const Value& hook, const char* name) {
  auto* native = value_as_native_function(hook);
  return native != nullptr && native->name == name;
}

XLANG3_HOT_INLINE bool xlang_vm_descriptor_method(const Value& descriptor, const char* name, Value& out) {
  std::string error;
  return object_get_attr(descriptor, name, out, error);
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow slot_descriptor_get(
    const SlotDescriptorObject& descriptor,
    const Value& receiver,
    Value& out,
    RaiseRuntimeError&& raise_runtime_error) {
  if (receiver.tag == ValueTag::None) {
    Value descriptor_value;
    descriptor_value.tag = ValueTag::Object;
    descriptor_value.flags = kXlangValueBorrowedRefFlag;
    descriptor_value.as.obj = const_cast<Object*>(&descriptor.header);
    value_assign_fast(out, descriptor_value);
    return XlangVMOpFlow::Next;
  }
  auto* instance = value_as_instance(receiver);
  if (instance == nullptr || descriptor.index >= instance_slot_count(instance)) {
    return raise_runtime_error("descriptor does not apply to this object")
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  const auto& slot_value = instance_slot_at(instance, descriptor.index);
  if (slot_value.tag == ValueTag::Invalid) {
    return raise_runtime_error("object has no attribute '" + descriptor.name + "'")
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(out, slot_value);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow slot_descriptor_set(
    const SlotDescriptorObject& descriptor,
    const Value& receiver,
    const Value& value,
    RaiseRuntimeError&& raise_runtime_error) {
  auto* instance = value_as_instance(receiver);
  if (instance == nullptr || descriptor.index >= instance_slot_count(instance)) {
    return raise_runtime_error("descriptor does not apply to this object")
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(instance_slot_at(instance, descriptor.index), value);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow slot_descriptor_delete(
    const SlotDescriptorObject& descriptor,
    const Value& receiver,
    RaiseRuntimeError&& raise_runtime_error) {
  auto* instance = value_as_instance(receiver);
  if (instance == nullptr || descriptor.index >= instance_slot_count(instance)) {
    return raise_runtime_error("descriptor does not apply to this object")
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  value_set_invalid(instance_slot_at(instance, descriptor.index));
  return XlangVMOpFlow::Next;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_attr_hook(
    const Value& hook,
    const Value* leading,
    uint32_t leading_count,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    std::vector<Value>& native_call_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    uint32_t return_dst,
    size_t& ip,
    RuntimeResult& result,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  CallArgsView hook_args;
  hook_args.leading = leading;
  hook_args.leading_count = leading_count;
  Value bound_values[4];
  const Value* function_value = &hook;
  if (auto* bound = value_as_bound_method(hook)) {
    if (leading_count + 1 > 4) {
      return raise_runtime_error("too many descriptor hook arguments")
          ? XlangVMOpFlow::ContinueLoop
          : XlangVMOpFlow::ReturnResult;
    }
    value_assign_fast(bound_values[0], bound->self);
    for (uint32_t i = 0; i < leading_count; ++i) {
      value_assign_fast(bound_values[i + 1], leading[i]);
    }
    hook_args.leading = bound_values;
    hook_args.leading_count = leading_count + 1;
    function_value = &bound->function;
  }
  bool pushed_frame = false;
  if (!call_callable_value(runtime, *function_value, hook_args, module, module_owner, return_dst, ip, native_call_args,
                           execution_lock, out, pushed_frame, make_generator_if_needed, push_frame,
                           raise_runtime_error, raise_exception_value)) {
    if (!result.errors.empty()) {
      return XlangVMOpFlow::ReturnResult;
    }
    return XlangVMOpFlow::ContinueLoop;
  }
  return pushed_frame ? XlangVMOpFlow::SwitchFrame : XlangVMOpFlow::Next;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_descriptor_get(
    const Value& descriptor,
    const Value& receiver,
    const Value& owner,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    std::vector<Value>& native_call_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    uint32_t return_dst,
    size_t& ip,
    RuntimeResult& result,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  Value get_method;
  if (!xlang_vm_descriptor_method(descriptor, "__get__", get_method)) {
    value_assign_fast(out, descriptor);
    return XlangVMOpFlow::Next;
  }
  Value args[2];
  value_assign_fast(args[0], receiver);
  value_assign_fast(args[1], owner);
  return call_attr_hook(get_method, args, 2, module, module_owner, runtime, native_call_args, execution_lock,
                        out, return_dst, ip, result, make_generator_if_needed, push_frame,
                        raise_runtime_error, raise_exception_value);
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_descriptor_set(
    const Value& descriptor,
    const Value& receiver,
    const Value& value,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    std::vector<Value>& native_call_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    uint32_t return_dst,
    size_t& ip,
    RuntimeResult& result,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  Value set_method;
  if (!xlang_vm_descriptor_method(descriptor, "__set__", set_method)) {
    return raise_runtime_error("can't set attribute") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  Value args[2];
  value_assign_fast(args[0], receiver);
  value_assign_fast(args[1], value);
  return call_attr_hook(set_method, args, 2, module, module_owner, runtime, native_call_args, execution_lock,
                        out, return_dst, ip, result, make_generator_if_needed, push_frame,
                        raise_runtime_error, raise_exception_value);
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow call_descriptor_delete(
    const Value& descriptor,
    const Value& receiver,
    const ir::Module& module,
    const std::shared_ptr<const ir::Module>& module_owner,
    Runtime& runtime,
    std::vector<Value>& native_call_args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    uint32_t return_dst,
    size_t& ip,
    RuntimeResult& result,
    MakeGeneratorIfNeeded&& make_generator_if_needed,
    PushFrame&& push_frame,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  Value delete_method;
  if (!xlang_vm_descriptor_method(descriptor, "__delete__", delete_method)) {
    return raise_runtime_error("can't delete attribute") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  Value args[1];
  value_assign_fast(args[0], receiver);
  return call_attr_hook(delete_method, args, 1, module, module_owner, runtime, native_call_args, execution_lock,
                        out, return_dst, ip, result, make_generator_if_needed, push_frame,
                        raise_runtime_error, raise_exception_value);
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow load_attr(
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
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::Attr);
  if (in.b >= fn.names.size()) {
    result.errors.push_back("invalid attribute name");
    return XlangVMOpFlow::ReturnResult;
  }
  if (auto* hook_instance = value_as_instance(regs[in.a])) {
    auto* hook_class = value_as_class(hook_instance->klass);
    Value hook;
    std::string hook_error;
    if (hook_class != nullptr && hook_class->has_getattribute_hook &&
        object_get_class_attr_for_instance(regs[in.a], "__getattribute__", hook, hook_error) &&
        !xlang_vm_is_default_object_hook(hook, "object.__getattribute__")) {
      Value hook_values[2];
      value_assign_fast(hook_values[0], regs[in.a]);
      hook_values[1] = Value::string(fn.names[in.b]);
      return call_attr_hook(hook, hook_values, 2, module, module_owner, runtime, native_call_args, execution_lock,
                            regs[in.dst], in.dst, ip, result, make_generator_if_needed, push_frame,
                            raise_runtime_error, raise_exception_value);
    }
  }
  if (auto* instance = value_as_instance(regs[in.a])) {
    if (auto* klass = value_as_class(instance->klass)) {
      auto& cache = instr_cache[ip].attr;
      if (cache.getter_inline && cache.owner == &klass->header && cache.version == klass->version) {
        std::string error;
        if (!execute_inline_property_getter(*instance, cache, regs[in.dst], error)) {
          return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
    }
  }
  std::string error;
  Value attr;
  if (!load_attr_cached(regs[in.a], fn.names[in.b], instr_cache[ip].attr, attr, error)) {
    if (auto* hook_instance = value_as_instance(regs[in.a])) {
      auto* hook_class = value_as_class(hook_instance->klass);
      Value hook;
      std::string hook_error;
      if (hook_class != nullptr && hook_class->has_getattr_hook &&
          object_get_class_attr_for_instance(regs[in.a], "__getattr__", hook, hook_error)) {
        Value hook_values[2];
        value_assign_fast(hook_values[0], regs[in.a]);
        hook_values[1] = Value::string(fn.names[in.b]);
        return call_attr_hook(hook, hook_values, 2, module, module_owner, runtime, native_call_args, execution_lock,
                              regs[in.dst], in.dst, ip, result, make_generator_if_needed, push_frame,
                              raise_runtime_error, raise_exception_value);
      }
    }
    return raise_exception_value(runtime.make_exception("AttributeError", error))
        ? XlangVMOpFlow::ContinueLoop
        : XlangVMOpFlow::ReturnResult;
  }
  if (auto* slot = value_as_slot_descriptor(attr)) {
    if (auto* instance = value_as_instance(regs[in.a])) {
      return slot_descriptor_get(*slot, regs[in.a], regs[in.dst], raise_runtime_error);
    }
    if (value_as_class(regs[in.a]) != nullptr) {
      Value none = Value::none();
      return slot_descriptor_get(*slot, none, regs[in.dst], raise_runtime_error);
    }
  }
  if (auto* property = value_as_instance(regs[in.a]) != nullptr ? value_as_property(attr) : nullptr) {
    if (property->fget.tag == ValueTag::None || property->fget.tag == ValueTag::Invalid) {
      return raise_runtime_error("unreadable attribute") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    Value self_arg[1];
    value_assign_fast(self_arg[0], regs[in.a]);
    CallArgsView property_args;
    property_args.leading = self_arg;
    property_args.leading_count = 1;
    if (auto* fn_obj = value_as_function(property->fget)) {
      auto* instance = value_as_instance(regs[in.a]);
      auto& cache = instr_cache[ip].attr;
      if (instance != nullptr) {
        if (!cache.getter_inline) {
          InlinePropertyAccess inline_spec;
          if (analyze_property_getter(module, *fn_obj, inline_spec)) {
            cache.getter_slot = inline_spec.slot;
            cache.getter_op = inline_spec.op;
            cache.getter_has_const = inline_spec.has_const;
            value_assign_fast(cache.getter_const, inline_spec.constant);
            if (auto* klass = value_as_class(instance->klass)) {
              cache.owner = &klass->header;
              cache.version = klass->version;
            }
            cache.getter_inline = true;
          }
        }
        if (cache.getter_inline) {
          if (!execute_inline_property_getter(*instance, cache, regs[in.dst], error)) {
            return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
          }
          return XlangVMOpFlow::Next;
        }
      }
      const ir::Module* call_module = &module;
      auto call_module_owner = module_owner;
      if (fn_obj->module != nullptr) {
        call_module = fn_obj->module.get();
        call_module_owner = fn_obj->module;
      }
      ++ip;
      if (!push_frame(*call_module, fn_obj->function_id, property_args, fn_obj->closure, fn_obj->defaults,
                      fn_obj->globals_module, std::move(call_module_owner), in.dst)) {
        return XlangVMOpFlow::ReturnResult;
      }
      return XlangVMOpFlow::SwitchFrame;
    }
    if (auto* native = value_as_native_function(property->fget)) {
      Value native_result;
      bool ok = false;
      if (native->fast_callback != nullptr && !native->fast_releases_vm_lock) {
        ok = native->fast_callback(runtime, property_args.leading, property_args.leading_count, nullptr, nullptr,
                                   0, native_result, error, native->user_data);
      } else {
        execution_lock.unlock();
        ok = native->fast_callback != nullptr
                 ? native->fast_callback(runtime, property_args.leading, property_args.leading_count, nullptr,
                                         nullptr, 0, native_result, error, native->user_data)
                 : native->callback != nullptr &&
                       native->callback(runtime, self_arg, 1, native_result, error, native->user_data);
        execution_lock.lock();
      }
      if (!ok) {
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return raise_runtime_error(error.empty() ? "property getter failed" : error)
            ? XlangVMOpFlow::ContinueLoop
            : XlangVMOpFlow::ReturnResult;
      }
      regs[in.dst] = std::move(native_result);
      return XlangVMOpFlow::Next;
    }
    return raise_runtime_error("property getter is not callable") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  if (object_value_has_descriptor_get(attr)) {
    if (auto* instance = value_as_instance(regs[in.a])) {
      return call_descriptor_get(attr, regs[in.a], instance->klass, module, module_owner, runtime, native_call_args,
                                 execution_lock, regs[in.dst], in.dst, ip, result, make_generator_if_needed,
                                 push_frame, raise_runtime_error, raise_exception_value);
    }
    if (value_as_class(regs[in.a]) != nullptr) {
      Value none = Value::none();
      return call_descriptor_get(attr, none, regs[in.a], module, module_owner, runtime, native_call_args,
                                 execution_lock, regs[in.dst], in.dst, ip, result, make_generator_if_needed,
                                 push_frame, raise_runtime_error, raise_exception_value);
    }
  }
  regs[in.dst] = std::move(attr);
  return XlangVMOpFlow::Next;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow store_attr(
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
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::Attr);
  if (in.a >= fn.names.size()) {
    result.errors.push_back("invalid attribute name");
    return XlangVMOpFlow::ReturnResult;
  }
  if (auto* hook_instance = value_as_instance(regs[in.dst])) {
    auto* hook_class = value_as_class(hook_instance->klass);
    Value hook;
    std::string hook_error;
    if (hook_class != nullptr && hook_class->has_setattr_hook &&
        object_get_class_attr_for_instance(regs[in.dst], "__setattr__", hook, hook_error) &&
        !xlang_vm_is_default_object_hook(hook, "object.__setattr__")) {
      Value hook_values[3];
      value_assign_fast(hook_values[0], regs[in.dst]);
      hook_values[1] = Value::string(fn.names[in.a]);
      value_assign_fast(hook_values[2], regs[in.b]);
      return call_attr_hook(hook, hook_values, 3, module, module_owner, runtime, native_call_args, execution_lock,
                            regs[in.b], in.b, ip, result, make_generator_if_needed, push_frame,
                            raise_runtime_error, raise_exception_value);
    }
  }
  if (auto* instance = value_as_instance(regs[in.dst])) {
    if (auto* klass = value_as_class(instance->klass)) {
      auto& cache = instr_cache[ip].attr;
      if (cache.setter_inline && cache.owner == &klass->header && cache.version == klass->version) {
        std::string error;
        if (!execute_inline_property_setter(*instance, cache, regs[in.b], error)) {
          return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
    }
  }
  std::string error;
  Value descriptor;
  bool has_descriptor = false;
  if (auto* instance = value_as_instance(regs[in.dst])) {
    if (auto* klass = value_as_class(instance->klass)) {
      auto& cache = instr_cache[ip].attr;
      if (cache.kind == AttrSiteKind::Descriptor &&
          cache.owner == &klass->header &&
          cache.version == klass->version &&
          object_value_is_data_descriptor(cache.value)) {
        value_assign_fast(descriptor, cache.value);
        has_descriptor = true;
      }
    }
  }
  if (!has_descriptor && object_get_class_attr_for_instance(regs[in.dst], fn.names[in.a], descriptor, error)) {
    if (object_value_is_data_descriptor(descriptor)) {
      if (auto* instance = value_as_instance(regs[in.dst])) {
        if (auto* klass = value_as_class(instance->klass)) {
          auto& cache = instr_cache[ip].attr;
          cache.kind = AttrSiteKind::Descriptor;
          cache.owner = &klass->header;
          cache.version = klass->version;
          value_assign_fast(cache.value, descriptor);
        }
      }
      has_descriptor = true;
    }
  }
  if (has_descriptor) {
    if (auto* slot = value_as_slot_descriptor(descriptor)) {
      return slot_descriptor_set(*slot, regs[in.dst], regs[in.b], raise_runtime_error);
    }
    if (auto* property = value_as_property(descriptor)) {
      if (property->fset.tag == ValueTag::None || property->fset.tag == ValueTag::Invalid) {
        return raise_runtime_error("can't set attribute") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      Value property_values[2];
      value_assign_fast(property_values[0], regs[in.dst]);
      value_assign_fast(property_values[1], regs[in.b]);
      CallArgsView property_args;
      property_args.leading = property_values;
      property_args.leading_count = 2;
      if (auto* fn_obj = value_as_function(property->fset)) {
        auto* instance = value_as_instance(regs[in.dst]);
        auto& cache = instr_cache[ip].attr;
        if (instance != nullptr) {
          if (!cache.setter_inline) {
            InlinePropertyAccess inline_spec;
            if (analyze_property_setter(module, *fn_obj, inline_spec)) {
              cache.setter_slot = inline_spec.slot;
              cache.setter_op = inline_spec.op;
              cache.setter_has_const = inline_spec.has_const;
              value_assign_fast(cache.setter_const, inline_spec.constant);
              if (auto* klass = value_as_class(instance->klass)) {
                cache.owner = &klass->header;
                cache.version = klass->version;
              }
              cache.setter_inline = true;
            }
          }
          if (cache.setter_inline) {
            if (!execute_inline_property_setter(*instance, cache, regs[in.b], error)) {
              return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
        }
        const ir::Module* call_module = &module;
        auto call_module_owner = module_owner;
        if (fn_obj->module != nullptr) {
          call_module = fn_obj->module.get();
          call_module_owner = fn_obj->module;
        }
        ++ip;
        if (!push_frame(*call_module, fn_obj->function_id, property_args, fn_obj->closure, fn_obj->defaults,
                        fn_obj->globals_module, std::move(call_module_owner), in.b)) {
          return XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::SwitchFrame;
      }
      if (auto* native = value_as_native_function(property->fset)) {
        Value native_result;
        bool ok = false;
        if (native->fast_callback != nullptr && !native->fast_releases_vm_lock) {
          ok = native->fast_callback(runtime, property_args.leading, property_args.leading_count, nullptr,
                                     nullptr, 0, native_result, error, native->user_data);
        } else {
          execution_lock.unlock();
          ok = native->fast_callback != nullptr
                   ? native->fast_callback(runtime, property_args.leading, property_args.leading_count, nullptr,
                                           nullptr, 0, native_result, error, native->user_data)
                   : native->callback != nullptr &&
                         native->callback(runtime, property_values, 2, native_result, error, native->user_data);
          execution_lock.lock();
        }
        if (!ok) {
          Value pending;
          if (runtime.take_pending_exception(pending)) {
            return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
          }
          return raise_runtime_error(error.empty() ? "property setter failed" : error)
              ? XlangVMOpFlow::ContinueLoop
              : XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
      return raise_runtime_error("property setter is not callable") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (object_value_has_descriptor_set(descriptor)) {
      return call_descriptor_set(descriptor, regs[in.dst], regs[in.b], module, module_owner, runtime, native_call_args,
                                 execution_lock, regs[in.b], in.b, ip, result, make_generator_if_needed,
                                 push_frame, raise_runtime_error, raise_exception_value);
    }
  }
  if (!store_attr_cached(regs[in.dst], fn.names[in.a], regs[in.b], instr_cache[ip].attr, error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename MakeGeneratorIfNeeded, typename PushFrame, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow delete_attr(
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
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  xlang_vm_cache_touch(instr_cache[ip], XlangVMCacheDomain::Attr);
  if (in.dst >= regs.size() || in.a >= fn.names.size()) {
    result.errors.push_back("invalid attr delete");
    return XlangVMOpFlow::ReturnResult;
  }
  std::string error;
  if (value_as_module(regs[in.dst]) != nullptr) {
    if (!module_set_attr(regs[in.dst], fn.names[in.a], Value::invalid(), error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return XlangVMOpFlow::Next;
  }
  if (auto* hook_instance = value_as_instance(regs[in.dst])) {
    auto* hook_class = value_as_class(hook_instance->klass);
    Value hook;
    std::string hook_error;
    if (hook_class != nullptr && hook_class->has_delattr_hook &&
        object_get_class_attr_for_instance(regs[in.dst], "__delattr__", hook, hook_error) &&
        !xlang_vm_is_default_object_hook(hook, "object.__delattr__")) {
      Value hook_values[2];
      value_assign_fast(hook_values[0], regs[in.dst]);
      hook_values[1] = Value::string(fn.names[in.a]);
      return call_attr_hook(hook, hook_values, 2, module, module_owner, runtime, native_call_args, execution_lock,
                            regs[in.dst], in.dst, ip, result, make_generator_if_needed, push_frame,
                            raise_runtime_error, raise_exception_value);
    }
  }

  Value descriptor;
  bool has_descriptor = false;
  if (auto* instance = value_as_instance(regs[in.dst])) {
    if (auto* klass = value_as_class(instance->klass)) {
      auto& cache = instr_cache[ip].attr;
      if (cache.kind == AttrSiteKind::Descriptor &&
          cache.owner == &klass->header &&
          cache.version == klass->version &&
          object_value_is_data_descriptor(cache.value)) {
        value_assign_fast(descriptor, cache.value);
        has_descriptor = true;
      }
    }
  }
  if (!has_descriptor && object_get_class_attr_for_instance(regs[in.dst], fn.names[in.a], descriptor, error)) {
    if (object_value_is_data_descriptor(descriptor)) {
      if (auto* instance = value_as_instance(regs[in.dst])) {
        if (auto* klass = value_as_class(instance->klass)) {
          auto& cache = instr_cache[ip].attr;
          cache.kind = AttrSiteKind::Descriptor;
          cache.owner = &klass->header;
          cache.version = klass->version;
          value_assign_fast(cache.value, descriptor);
        }
      }
      has_descriptor = true;
    }
  }
  if (has_descriptor) {
    if (auto* slot = value_as_slot_descriptor(descriptor)) {
      return slot_descriptor_delete(*slot, regs[in.dst], raise_runtime_error);
    }
    if (auto* property = value_as_property(descriptor)) {
      if (property->fdel.tag == ValueTag::None || property->fdel.tag == ValueTag::Invalid) {
        return raise_runtime_error("can't delete attribute") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      Value self_arg[1];
      value_assign_fast(self_arg[0], regs[in.dst]);
      CallArgsView property_args;
      property_args.leading = self_arg;
      property_args.leading_count = 1;
      if (auto* fn_obj = value_as_function(property->fdel)) {
        auto* instance = value_as_instance(regs[in.dst]);
        auto& cache = instr_cache[ip].attr;
        if (instance != nullptr) {
          if (!cache.deleter_inline) {
            InlinePropertyAccess inline_spec;
            if (analyze_property_deleter(module, *fn_obj, inline_spec)) {
              cache.deleter_slot = inline_spec.slot;
              value_assign_fast(cache.deleter_const, inline_spec.constant);
              cache.deleter_inline = true;
            }
          }
          if (cache.deleter_inline) {
            if (!execute_inline_property_deleter(*instance, cache, error)) {
              return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
            }
            return XlangVMOpFlow::Next;
          }
        }
        const ir::Module* call_module = &module;
        auto call_module_owner = module_owner;
        if (fn_obj->module != nullptr) {
          call_module = fn_obj->module.get();
          call_module_owner = fn_obj->module;
        }
        ++ip;
        if (!push_frame(*call_module, fn_obj->function_id, property_args, fn_obj->closure, fn_obj->defaults,
                        fn_obj->globals_module, std::move(call_module_owner), in.dst)) {
          return XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::SwitchFrame;
      }
      if (auto* native = value_as_native_function(property->fdel)) {
        Value native_result;
        bool ok = false;
        if (native->fast_callback != nullptr && !native->fast_releases_vm_lock) {
          ok = native->fast_callback(runtime, property_args.leading, property_args.leading_count, nullptr,
                                     nullptr, 0, native_result, error, native->user_data);
        } else {
          execution_lock.unlock();
          ok = native->fast_callback != nullptr
                   ? native->fast_callback(runtime, property_args.leading, property_args.leading_count,
                                           nullptr, nullptr, 0, native_result, error, native->user_data)
                   : native->callback != nullptr &&
                         native->callback(runtime, self_arg, 1, native_result, error, native->user_data);
          execution_lock.lock();
        }
        if (!ok) {
          Value pending;
          if (runtime.take_pending_exception(pending)) {
            return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
          }
          return raise_runtime_error(error.empty() ? "property deleter failed" : error)
              ? XlangVMOpFlow::ContinueLoop
              : XlangVMOpFlow::ReturnResult;
        }
        return XlangVMOpFlow::Next;
      }
      return raise_runtime_error("property deleter is not callable") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (object_value_has_descriptor_delete(descriptor)) {
      return call_descriptor_delete(descriptor, regs[in.dst], module, module_owner, runtime, native_call_args,
                                    execution_lock, regs[in.dst], in.dst, ip, result, make_generator_if_needed,
                                    push_frame, raise_runtime_error, raise_exception_value);
    }
  }
  if (!object_delete_attr(regs[in.dst], fn.names[in.a], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow load_instance_slot(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  auto* instance = value_as_instance(regs[in.a]);
  if (instance == nullptr || in.b >= instance_slot_count(instance)) {
    return raise_runtime_error("invalid instance slot load") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const auto& slot_value = instance_slot_at(instance, in.b);
  if (slot_value.tag == ValueTag::Invalid) {
    if (auto* klass = value_as_class(instance->klass)) {
      if (in.b < klass->instance_slot_names.size()) {
        std::string error;
        if (object_get_attr(regs[in.a], klass->instance_slot_names[in.b], regs[in.dst], error)) {
          return XlangVMOpFlow::Next;
        }
      }
    }
    return raise_runtime_error("object has no attribute") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(regs[in.dst], slot_value);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow store_instance_slot(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  auto* instance = value_as_instance(regs[in.dst]);
  if (instance == nullptr || in.a >= instance_slot_count(instance)) {
    return raise_runtime_error("invalid instance slot store") ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(instance_slot_at(instance, in.a), regs[in.b]);
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
