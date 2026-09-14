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
#include "xlang3/mapping.h"
#include "xlang3/value.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

struct ClassObject;

XLANG3_HOT_INLINE const Value* builtin_fast_arg_at(
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    uint32_t index) {
  if (index < leading_count) {
    return leading == nullptr ? nullptr : &leading[index];
  }
  const uint32_t register_index = index - leading_count;
  if (register_index >= register_arg_count || registers == nullptr || register_args == nullptr) {
    return nullptr;
  }
  return &registers[register_args[register_index]];
}

XLANG3_HOT_INLINE bool method_check_argc(uint32_t argc, uint32_t expected, const char* name, std::string& error) {
  if (argc == expected) {
    return true;
  }
  error = std::string(name) + " expected " + std::to_string(expected) + " arguments, got " + std::to_string(argc);
  return false;
}

template <NativeFunctionCallback Callback, uint32_t MaxArgc>
XLANG3_HOT_INLINE bool builtin_method_fast_adapter(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void* user_data) {
  if (leading_count != 1 || leading == nullptr ||
      (register_arg_count != 0 && (registers == nullptr || register_args == nullptr))) {
    error = "invalid builtin method fast call";
    return false;
  }
  if (register_arg_count + 1 > MaxArgc) {
    std::vector<Value> args;
    args.reserve(static_cast<size_t>(register_arg_count) + 1);
    args.push_back(leading[0]);
    for (uint32_t i = 0; i < register_arg_count; ++i) args.push_back(registers[register_args[i]]);
    return Callback(runtime, args.data(), static_cast<uint32_t>(args.size()), out, error, user_data);
  }
  Value args[MaxArgc];
  value_borrow_assign_fast(args[0], leading[0]);
  for (uint32_t i = 0; i < register_arg_count; ++i) {
    value_borrow_assign_fast(args[i + 1], registers[register_args[i]]);
  }
  return Callback(runtime, args, register_arg_count + 1, out, error, user_data);
}

// Stack-backed positional-call adapter for ordinary native builtins.  This is
// the runtime equivalent of CPython's vectorcall convention: arguments stay in
// the VM register array instead of being copied through a heap vector first.
template <NativeFunctionCallback Callback, uint32_t MaxArgc>
XLANG3_HOT_INLINE bool builtin_fast_adapter(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void* user_data) {
  const uint32_t argc = leading_count + register_arg_count;
  if ((leading_count != 0 && leading == nullptr) ||
      (register_arg_count != 0 && (registers == nullptr || register_args == nullptr))) {
    error = "invalid builtin fast call";
    return false;
  }
  if (argc > MaxArgc) {
    std::vector<Value> args;
    args.reserve(argc);
    for (uint32_t i = 0; i < leading_count; ++i) args.push_back(leading[i]);
    for (uint32_t i = 0; i < register_arg_count; ++i) args.push_back(registers[register_args[i]]);
    return Callback(runtime, args.data(), argc, out, error, user_data);
  }
  Value args[MaxArgc];
  uint32_t next = 0;
  for (uint32_t i = 0; i < leading_count; ++i) {
    value_borrow_assign_fast(args[next++], leading[i]);
  }
  for (uint32_t i = 0; i < register_arg_count; ++i) {
    value_borrow_assign_fast(args[next++], registers[register_args[i]]);
  }
  return Callback(runtime, args, argc, out, error, user_data);
}

// Vectorcall adapter for variadic builtins. Common calls stay in a small stack
// buffer, while larger valid argument lists retain their ordinary semantics.
template <NativeFunctionCallback Callback, uint32_t InlineArgc>
XLANG3_HOT_INLINE bool builtin_variadic_fast_adapter(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void* user_data) {
  const uint32_t argc = leading_count + register_arg_count;
  if ((leading_count != 0 && leading == nullptr) ||
      (register_arg_count != 0 && (registers == nullptr || register_args == nullptr))) {
    error = "invalid variadic builtin fast call";
    return false;
  }
  Value inline_args[InlineArgc];
  std::vector<Value> overflow_args;
  Value* args = inline_args;
  if (argc > InlineArgc) {
    overflow_args.resize(argc);
    args = overflow_args.data();
  }
  uint32_t next = 0;
  for (uint32_t i = 0; i < leading_count; ++i) {
    value_borrow_assign_fast(args[next++], leading[i]);
  }
  for (uint32_t i = 0; i < register_arg_count; ++i) {
    value_borrow_assign_fast(args[next++], registers[register_args[i]]);
  }
  return Callback(runtime, args, argc, out, error, user_data);
}

struct BuiltinMethodSpec {
  const char* name;
  const char* full_name;
  NativeFunctionCallback callback;
  NativeFastCallCallback fast_callback = nullptr;
  bool fast_releases_vm_lock = false;
  NativeKeywordFunctionCallback keyword_callback = nullptr;
  const char* text_signature = nullptr;
  mutable std::once_flag function_once;
  mutable Value function = Value::invalid();
};

XLANG3_HOT_INLINE void builtin_method_set_text_signature(Value& function, const char* text_signature) {
  if (text_signature == nullptr) {
    return;
  }
  auto* native = value_as_native_function(function);
  if (native == nullptr) {
    return;
  }
  if (native->attrs_dict == nullptr) {
    native->attrs_dict = new Value(Value::dict({}));
  }
  std::string ignored;
  mapping_set_item(
      *native->attrs_dict,
      Value::string("__text_signature__"),
      Value::string(text_signature),
      ignored);
}

XLANG3_HOT_INLINE const Value& builtin_method_function(const BuiltinMethodSpec& spec) {
  std::call_once(spec.function_once, [&]() {
    spec.function = Value::native_function(
        0,
        spec.full_name,
        spec.callback,
        nullptr,
        nullptr,
        spec.fast_callback,
        spec.fast_releases_vm_lock,
        spec.keyword_callback);
    builtin_method_set_text_signature(spec.function, spec.text_signature);
  });
  return spec.function;
}

XLANG3_HOT_INLINE bool bind_builtin_method(
    const Value& object,
    std::string full_name,
    NativeFunctionCallback callback,
    NativeFastCallCallback fast_callback,
    bool fast_releases_vm_lock,
    const char* text_signature,
    Value& out) {
  Value function = Value::native_function(
      0,
      std::move(full_name),
      callback,
      nullptr,
      nullptr,
      fast_callback,
      fast_releases_vm_lock,
      nullptr);
  builtin_method_set_text_signature(function, text_signature);
  out = Value::bound_method(object, std::move(function));
  return true;
}

XLANG3_HOT_INLINE bool bind_builtin_method(
    const Value& object,
    std::string full_name,
    NativeFunctionCallback callback,
    NativeFastCallCallback fast_callback,
    bool fast_releases_vm_lock,
    NativeKeywordFunctionCallback keyword_callback,
    const char* text_signature,
    Value& out) {
  Value function = Value::native_function(
      0,
      std::move(full_name),
      callback,
      nullptr,
      nullptr,
      fast_callback,
      fast_releases_vm_lock,
      keyword_callback);
  builtin_method_set_text_signature(function, text_signature);
  out = Value::bound_method(object, std::move(function));
  return true;
}

XLANG3_HOT_INLINE bool bind_builtin_method_from_table(
    const Value& object,
    const std::string& name,
    const BuiltinMethodSpec* methods,
    size_t method_count,
    Value& out) {
  for (size_t i = 0; i < method_count; ++i) {
    if (name == methods[i].name) {
      out = Value::bound_method(object, builtin_method_function(methods[i]));
      return true;
    }
  }
  return false;
}

bool list_get_method(const Value& object, const std::string& name, Value& out);
bool list_install_class_methods(Runtime& runtime, ClassObject& list_class);
const BuiltinMethodSpec* list_find_method_spec(const Value& object, const std::string& name);
bool tuple_get_method(const Value& object, const std::string& name, Value& out);
bool tuple_install_class_methods(Runtime& runtime, ClassObject& tuple_class);
bool dict_get_method(const Value& object, const std::string& name, Value& out);
const BuiltinMethodSpec* dict_find_method_spec(const Value& object, const std::string& name);
bool dict_install_class_methods(Runtime& runtime, ClassObject& dict_class);
bool file_get_method(const Value& object, const std::string& name, Value& out);
bool int_get_method(const Value& object, const std::string& name, Value& out);
const BuiltinMethodSpec* int_find_method_spec(const Value& object, const std::string& name);
bool int_install_class_methods(Runtime& runtime, ClassObject& int_class);
bool set_get_method(const Value& object, const std::string& name, Value& out);
const BuiltinMethodSpec* set_find_method_spec(const Value& object, const std::string& name);
bool set_install_class_methods(Runtime& runtime, ClassObject& set_class);
bool string_get_method(const Value& object, const std::string& name, Value& out);
const BuiltinMethodSpec* string_find_method_spec(const Value& object, const std::string& name);
bool string_install_class_methods(Runtime& runtime, ClassObject& string_class);
bool bytes_get_method(const Value& object, const std::string& name, Value& out);
bool bytearray_get_method(const Value& object, const std::string& name, Value& out);
bool bytes_install_class_methods(Runtime& runtime, ClassObject& bytes_class);
bool memoryview_get_method(const Value& object, const std::string& name, Value& out);
bool iterator_get_method(const Value& object, const std::string& name, Value& out);
bool generator_get_method(const Value& object, const std::string& name, Value& out);
bool property_get_method(const Value& object, const std::string& name, Value& out);
bool property_install_class_methods(Runtime& runtime, ClassObject& property_class);

XLANG3_HOT_INLINE const BuiltinMethodSpec* builtin_method_find_spec_for_call(
    const Value& object,
    const std::string& name) {
  if (const auto* spec = string_find_method_spec(object, name)) {
    return spec;
  }
  if (const auto* spec = list_find_method_spec(object, name)) {
    return spec;
  }
  if (const auto* spec = dict_find_method_spec(object, name)) return spec;
  if (const auto* spec = set_find_method_spec(object, name)) return spec;
  if (const auto* spec = int_find_method_spec(object, name)) return spec;
  return nullptr;
}

} // namespace xlang3
