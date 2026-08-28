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
#include "xlang3/builtins.h"

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

#include "../thread/thread_objects.h"
#include "runtime/memory/x3_runtime_memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xlang3 {

namespace {

int g_recursion_limit = 1000;
constexpr int64_t kDefaultIntMaxStrDigits = 4300;
constexpr int64_t kIntStrDigitsCheckThreshold = 640;
int64_t g_int_max_str_digits = kDefaultIntMaxStrDigits;
std::vector<Value> g_audit_hooks;
thread_local int64_t g_coroutine_origin_tracking_depth = 0;
Value g_asyncgen_firstiter = Value::none();
Value g_asyncgen_finalizer = Value::none();
std::string g_filesystem_encoding = "utf-8";
std::string g_filesystem_encode_errors = "surrogatepass";
constexpr int64_t kMonitoringToolCount = 6;
constexpr int64_t kMonitoringEventPyStart = 1;
constexpr int64_t kMonitoringEventPyResume = 2;
constexpr int64_t kMonitoringEventPyReturn = 4;
constexpr int64_t kMonitoringEventPyYield = 8;
constexpr int64_t kMonitoringEventCall = 16;
constexpr int64_t kMonitoringEventLine = 32;
constexpr int64_t kMonitoringEventInstruction = 64;
constexpr int64_t kMonitoringEventJump = 128;
constexpr int64_t kMonitoringEventBranchLeft = 256;
constexpr int64_t kMonitoringEventBranchRight = 512;
constexpr int64_t kMonitoringEventStopIteration = 1024;
constexpr int64_t kMonitoringEventRaise = 2048;
constexpr int64_t kMonitoringEventExceptionHandled = 4096;
constexpr int64_t kMonitoringEventPyUnwind = 8192;
constexpr int64_t kMonitoringEventPyThrow = 16384;
constexpr int64_t kMonitoringEventReraise = 32768;
constexpr int64_t kMonitoringEventCReturn = 65536;
constexpr int64_t kMonitoringEventCRaise = 131072;
constexpr int64_t kMonitoringEventBranch = 262144;
constexpr int64_t kMonitoringEventMask =
    kMonitoringEventPyStart | kMonitoringEventPyResume | kMonitoringEventPyReturn |
    kMonitoringEventPyYield | kMonitoringEventCall | kMonitoringEventLine |
    kMonitoringEventInstruction | kMonitoringEventJump | kMonitoringEventBranchLeft |
    kMonitoringEventBranchRight | kMonitoringEventStopIteration | kMonitoringEventRaise |
    kMonitoringEventExceptionHandled | kMonitoringEventPyUnwind | kMonitoringEventPyThrow |
    kMonitoringEventReraise | kMonitoringEventCReturn | kMonitoringEventCRaise |
    kMonitoringEventBranch;

struct MonitoringCodeKey {
  const ir::Module* module = nullptr;
  uint32_t function_id = 0;

  bool operator==(const MonitoringCodeKey& other) const {
    return module == other.module && function_id == other.function_id;
  }
};

struct MonitoringCodeKeyHash {
  size_t operator()(const MonitoringCodeKey& key) const {
    return std::hash<const ir::Module*>{}(key.module) ^
           (std::hash<uint32_t>{}(key.function_id) + 0x9e3779b9u);
  }
};

struct MonitoringToolState {
  Value name = Value::none();
  int64_t events = 0;
  std::unordered_map<MonitoringCodeKey, int64_t, MonitoringCodeKeyHash> local_events;
  std::unordered_map<int64_t, Value> callbacks;
};

std::array<MonitoringToolState, kMonitoringToolCount> g_monitoring_tools;
thread_local bool g_monitoring_dispatch_active = false;

MonitoringCodeKey monitoring_code_key(const CodeObject& code) {
  return MonitoringCodeKey{code.module.get(), code.function_id};
}

bool is_callable_value(const Value& value) {
  return value_as_function(value) != nullptr ||
         value_as_native_function(value) != nullptr ||
         value_as_bound_method(value) != nullptr ||
         value_as_class(value) != nullptr ||
         [&]() {
           Value call_attr;
           std::string ignored;
           return value_as_instance(value) != nullptr && attribute_get(value, "__call__", call_attr, ignored);
         }();
}

bool raise_sys_no_args_type_error(Runtime& runtime, std::string& error, const char* name, uint32_t argc) {
  error = std::string(name) + "() takes no arguments (" + std::to_string(argc) + " given)";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_no_keyword_args(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t, Value&, std::string& error, void* user_data) {
  const char* name = static_cast<const char*>(user_data);
  error = std::string(name) + "() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool raise_sys_one_arg_type_error(Runtime& runtime, std::string& error, const char* name, uint32_t argc) {
  error = std::string(name) + "() takes exactly one argument (" + std::to_string(argc) + " given)";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_bool_or_int_arg(const Value& value, int64_t& out) {
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
    return true;
  }
  return false;
}

std::string sys_type_name(Runtime& runtime, const Value& value);

bool sys_noop_hook(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  const char* name = static_cast<const char*>(user_data);
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, name, argc);
  }
  value_set_none(out);
  return true;
}

bool sys_noop_unexpected_keyword(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void* user_data) {
  const char* name = static_cast<const char*>(user_data);
  const std::string keyword(kwargc > 0 && kwargs[0].name != nullptr ? kwargs[0].name : "");
  error = std::string(name) + "() got an unexpected keyword argument '" + keyword + "'";
  runtime.raise_class_error("TypeError", error);
  return false;
}

Value sys_metadata_native_function(
    Runtime& runtime,
    const std::string& module_name,
    const std::string& qualified_name,
    const std::string& function_name,
    NativeFunctionCallback callback,
    void* user_data,
    const std::string& doc,
    NativeKeywordFunctionCallback keyword_callback = nullptr) {
  Value function = runtime.make_native_function(qualified_name, callback, user_data, nullptr, nullptr, false, keyword_callback);
  if (auto* native = value_as_native_function(function)) {
    native->attrs_dict = new Value(Value::dict({
        {Value::string("__module__"), Value::string(module_name)},
        {Value::string("__name__"), Value::string(function_name)},
        {Value::string("__qualname__"), Value::string(function_name)},
        {Value::string("__doc__"), Value::string(doc)},
    }));
  }
  return function;
}

Value sys_metadata_native_function_no_doc(
    Runtime& runtime,
    const std::string& module_name,
    const std::string& qualified_name,
    const std::string& function_name,
    NativeFunctionCallback callback,
    void* user_data = nullptr,
    NativeKeywordFunctionCallback keyword_callback = nullptr) {
  Value function = runtime.make_native_function(qualified_name, callback, user_data, nullptr, nullptr, false, keyword_callback);
  if (auto* native = value_as_native_function(function)) {
    native->attrs_dict = new Value(Value::dict({
        {Value::string("__module__"), Value::string(module_name)},
        {Value::string("__name__"), Value::string(function_name)},
        {Value::string("__qualname__"), Value::string(function_name)},
    }));
  }
  return function;
}

Value make_member_descriptor(const std::string& owner_name, const std::string& name, uint32_t index) {
  return slot_descriptor(owner_name, name, index);
}

bool sys_structseq_tuple_storage(const Value& self, const char* method, TupleObject*& out, std::string& error) {
  Value tuple_value;
  std::string ignored;
  if (!object_get_attr(self, "_tuple", tuple_value, ignored) || (out = value_as_tuple(tuple_value)) == nullptr) {
    error = std::string(method) + " target has no tuple storage";
    return false;
  }
  return true;
}

bool sys_structseq_count(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "unbound method tuple.count() needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc != 2) {
    error = "tuple.count() takes exactly one argument (" + std::to_string(argc - 1) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  TupleObject* tuple = nullptr;
  if (!sys_structseq_tuple_storage(args[0], "sys structseq count", tuple, error)) {
    error = "descriptor 'count' for 'tuple' objects doesn't apply to a '" + sys_type_name(runtime, args[0]) + "' object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t count = 0;
  for (const auto& item : tuple->items) {
    if (value_key_equal(item, args[1])) {
      ++count;
    }
  }
  out = Value::int64(count);
  return true;
}

bool sys_structseq_count_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return true;
  }
  error = "tuple.count() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_structseq_bound(const Value& value, size_t size, size_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64 && value.tag != ValueTag::Bool) {
    error = "sys structseq index bounds must be int";
    return false;
  }
  int64_t index = value.tag == ValueTag::Bool ? (value.as.b ? 1 : 0) : value.as.i64;
  if (index < 0) {
    index += static_cast<int64_t>(size);
  }
  if (index < 0) {
    index = 0;
  }
  if (index > static_cast<int64_t>(size)) {
    index = static_cast<int64_t>(size);
  }
  out = static_cast<size_t>(index);
  return true;
}

bool sys_structseq_index(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "unbound method tuple.index() needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc < 2) {
    error = "index expected at least 1 argument, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 4) {
    error = "index expected at most 3 arguments, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  TupleObject* tuple = nullptr;
  if (!sys_structseq_tuple_storage(args[0], "sys structseq index", tuple, error)) {
    error = "descriptor 'index' for 'tuple' objects doesn't apply to a '" + sys_type_name(runtime, args[0]) + "' object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  size_t start = 0;
  size_t stop = tuple->items.size();
  if (argc >= 3 && !sys_structseq_bound(args[2], tuple->items.size(), start, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc >= 4 && !sys_structseq_bound(args[3], tuple->items.size(), stop, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (start > stop) {
    start = stop;
  }
  for (size_t i = start; i < stop; ++i) {
    if (value_key_equal(tuple->items[i], args[1])) {
      out = Value::int64(static_cast<int64_t>(i));
      return true;
    }
  }
  error = "tuple.index(x): x not in tuple";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool sys_structseq_index_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return true;
  }
  error = "tuple.index() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

std::string sys_structseq_field_repr(const Value& value) {
  if (auto* string = value_as_string(value)) {
    std::string text = "'";
    const std::string raw = string_object_to_string(*string);
    for (char ch : raw) {
      if (ch == '\'' || ch == '\\') {
        text.push_back('\\');
      }
      if (ch == '\n') {
        text += "\\n";
      } else if (ch == '\r') {
        text += "\\r";
      } else if (ch == '\t') {
        text += "\\t";
      } else {
        text.push_back(ch);
      }
    }
    text.push_back('\'');
    return text;
  }
  return value_to_string(value);
}

bool sys_structseq_repr_text(const Value& self, std::string& text, std::string& error) {
  TupleObject* tuple = nullptr;
  if (!sys_structseq_tuple_storage(self, "sys structseq __repr__", tuple, error)) {
    return false;
  }
  Value repr_name_value;
  if (!object_get_attr(self, "_repr_name", repr_name_value, error) || value_as_string(repr_name_value) == nullptr) {
    error = "sys structseq __repr__ target has no repr name";
    return false;
  }
  Value names_value;
  TupleObject* names = nullptr;
  if (!object_get_attr(self, "_field_names", names_value, error) ||
      (names = value_as_tuple(names_value)) == nullptr) {
    error = "sys structseq __repr__ target has no field names";
    return false;
  }

  text = string_object_to_string(*value_as_string(repr_name_value));
  text += "(";
  const size_t count = (std::min)(tuple->items.size(), names->items.size());
  for (size_t i = 0; i < count; ++i) {
    auto* name = value_as_string(names->items[i]);
    if (name == nullptr) {
      error = "sys structseq __repr__ field name must be string";
      return false;
    }
    if (i != 0) {
      text += ", ";
    }
    text += string_object_to_string(*name);
    text += "=";
    text += sys_structseq_field_repr(tuple->items[i]);
  }
  text += ")";
  return true;
}

bool sys_structseq_update_string_value(Value& self, std::string& error) {
  std::string text;
  if (!sys_structseq_repr_text(self, text, error)) {
    return false;
  }
  std::string ignored;
  object_set_attr(self, "__xlang3_string_value__", Value::string(std::move(text)), ignored);
  return true;
}

bool sys_structseq_repr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "descriptor '__repr__' of 'sys structseq' object needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc != 1) {
    error = "expected 0 arguments, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string text;
  if (!sys_structseq_repr_text(args[0], text, error)) {
    error = "descriptor '__repr__' requires a 'sys structseq' object but received a '" + sys_type_name(runtime, args[0]) + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::string(std::move(text));
  return true;
}

bool sys_structseq_repr_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return true;
  }
  error = "wrapper __repr__() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

Value make_structseq(
    Runtime& runtime,
    const std::string& type_name,
    const std::vector<std::pair<std::string, Value>>& fields,
    const std::string& module_name = "sys",
    size_t sequence_fields = std::numeric_limits<size_t>::max(),
    const std::string& repr_name = "") {
  std::vector<std::pair<std::string, Value>> class_attrs;
  class_attrs.push_back({"__module__", Value::string(module_name)});
  class_attrs.push_back({"count", runtime.make_native_function(type_name + ".count", sys_structseq_count, nullptr, nullptr, nullptr, false, sys_structseq_count_kw)});
  class_attrs.push_back({"index", runtime.make_native_function(type_name + ".index", sys_structseq_index, nullptr, nullptr, nullptr, false, sys_structseq_index_kw)});
  class_attrs.push_back({"__repr__", runtime.make_native_function(type_name + ".__repr__", sys_structseq_repr, nullptr, nullptr, nullptr, false, sys_structseq_repr_kw)});
  if (sequence_fields == std::numeric_limits<size_t>::max() || sequence_fields > fields.size()) {
    sequence_fields = fields.size();
  }
  class_attrs.push_back({"n_sequence_fields", Value::int64(static_cast<int64_t>(sequence_fields))});
  class_attrs.push_back({"n_fields", Value::int64(static_cast<int64_t>(fields.size()))});
  class_attrs.push_back({"n_unnamed_fields", Value::int64(0)});
  const std::string actual_repr_name =
      repr_name.empty() ? (module_name.empty() ? type_name : module_name + "." + type_name) : repr_name;
  std::vector<Value> match_args;
  match_args.reserve(sequence_fields);
  for (size_t i = 0; i < fields.size(); ++i) {
    const auto& field = fields[i];
    class_attrs.push_back({field.first, make_member_descriptor(actual_repr_name, field.first, static_cast<uint32_t>(i))});
    if (match_args.size() < sequence_fields) {
      match_args.push_back(Value::string(field.first));
    }
  }
  class_attrs.push_back({"__match_args__", Value::tuple(std::move(match_args))});
  const Value* tuple_base = runtime.find_builtin("tuple");
  Value instance = Value::instance(Value::class_object(
      type_name,
      std::move(class_attrs),
      tuple_base != nullptr ? *tuple_base : Value::invalid()));
  std::vector<Value> tuple_items;
  std::vector<Value> field_names;
  tuple_items.reserve(sequence_fields);
  field_names.reserve(sequence_fields);
  std::string ignored;
  for (size_t i = 0; i < fields.size(); ++i) {
    const auto& field = fields[i];
    object_set_attr(instance, field.first, field.second, ignored);
    if (i < sequence_fields) {
      tuple_items.push_back(field.second);
      field_names.push_back(Value::string(field.first));
    }
  }
  object_set_attr(instance, "n_sequence_fields", Value::int64(static_cast<int64_t>(sequence_fields)), ignored);
  object_set_attr(instance, "n_fields", Value::int64(static_cast<int64_t>(fields.size())), ignored);
  object_set_attr(instance, "n_unnamed_fields", Value::int64(0), ignored);
  object_set_attr(instance, "_tuple", Value::tuple(std::move(tuple_items)), ignored);
  object_set_attr(instance, "_field_names", Value::tuple(std::move(field_names)), ignored);
  object_set_attr(instance, "_repr_name", Value::string(actual_repr_name), ignored);
  sys_structseq_update_string_value(instance, ignored);
  return instance;
}

std::string executable_path() {
#if defined(_WIN32)
  std::wstring buffer(32768, L'\0');
  const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size > 0) {
    buffer.resize(size);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), text.data(), bytes, nullptr, nullptr);
    return text;
  }
#endif
  std::error_code ec;
  auto path = std::filesystem::current_path(ec) / "xlang3";
  return path.string();
}

std::string runtime_prefix(const Runtime& runtime) {
  const auto& roots = runtime.import_roots();
  if (!roots.empty()) {
    return roots.front().string();
  }
  std::error_code ec;
  return std::filesystem::current_path(ec).string();
}

std::string runtime_stdlib_dir(const Runtime& runtime) {
  std::error_code ec;
  const auto& roots = runtime.import_roots();
  for (const auto& root : roots) {
    const auto lib = root / "Lib";
    if (std::filesystem::is_directory(lib, ec)) {
      return lib.string();
    }
    ec.clear();
  }
  return (std::filesystem::path(runtime_prefix(runtime)) / "Lib").string();
}

Value make_version_info(Runtime& runtime) {
  return make_structseq(
      runtime,
      "version_info",
      {
          {"major", Value::int64(3)},
          {"minor", Value::int64(14)},
          {"micro", Value::int64(7)},
          {"releaselevel", Value::string("final")},
          {"serial", Value::int64(0)},
      });
}

Value make_flags(Runtime& runtime) {
  return make_structseq(
      runtime,
      "flags",
      {
          {"debug", Value::int64(0)},
          {"inspect", Value::int64(0)},
          {"interactive", Value::int64(0)},
          {"optimize", Value::int64(0)},
          {"dont_write_bytecode", Value::int64(0)},
          {"no_user_site", Value::int64(0)},
          {"no_site", Value::int64(0)},
          {"ignore_environment", Value::int64(0)},
          {"verbose", Value::int64(0)},
          {"bytes_warning", Value::int64(0)},
          {"quiet", Value::int64(0)},
          {"hash_randomization", Value::int64(1)},
          {"isolated", Value::int64(0)},
          {"dev_mode", Value::boolean(false)},
          {"utf8_mode", Value::int64(0)},
          {"warn_default_encoding", Value::int64(0)},
          {"safe_path", Value::boolean(false)},
          {"int_max_str_digits", Value::int64(g_int_max_str_digits)},
          {"gil", Value::int64(1)},
          {"thread_inherit_context", Value::int64(0)},
          {"context_aware_warnings", Value::int64(0)},
      },
      "sys",
      18);
}

Value make_int_info(Runtime& runtime) {
  return make_structseq(
      runtime,
      "int_info",
      {
          {"bits_per_digit", Value::int64(30)},
          {"sizeof_digit", Value::int64(4)},
          {"default_max_str_digits", Value::int64(kDefaultIntMaxStrDigits)},
          {"str_digits_check_threshold", Value::int64(kIntStrDigitsCheckThreshold)},
      });
}

Value make_float_info(Runtime& runtime) {
  return make_structseq(
      runtime,
      "float_info",
      {
          {"max", Value::number((std::numeric_limits<double>::max)())},
          {"max_exp", Value::int64(std::numeric_limits<double>::max_exponent)},
          {"max_10_exp", Value::int64(std::numeric_limits<double>::max_exponent10)},
          {"min", Value::number((std::numeric_limits<double>::min)())},
          {"min_exp", Value::int64(std::numeric_limits<double>::min_exponent)},
          {"min_10_exp", Value::int64(std::numeric_limits<double>::min_exponent10)},
          {"dig", Value::int64(std::numeric_limits<double>::digits10)},
          {"mant_dig", Value::int64(std::numeric_limits<double>::digits)},
          {"epsilon", Value::number(std::numeric_limits<double>::epsilon())},
          {"radix", Value::int64(std::numeric_limits<double>::radix)},
          {"rounds", Value::int64(1)},
      });
}

Value make_hash_info(Runtime& runtime) {
  return make_structseq(
      runtime,
      "hash_info",
      {
          {"width", Value::int64(64)},
          {"modulus", Value::int64(2305843009213693951LL)},
          {"inf", Value::int64(314159)},
          {"nan", Value::int64(0)},
          {"imag", Value::int64(1000003)},
          {"algorithm", Value::string("xlang3")},
          {"hash_bits", Value::int64(64)},
          {"seed_bits", Value::int64(0)},
          {"cutoff", Value::int64(0)},
      });
}

Value make_thread_info(Runtime& runtime) {
  return make_structseq(
      runtime,
      "thread_info",
      {
#if defined(_WIN32)
          {"name", Value::string("nt")},
#else
          {"name", Value::string("pthread")},
#endif
          {"lock", Value::none()},
          {"version", Value::none()},
      });
}

Value make_asyncgen_hooks(Runtime& runtime) {
  return make_structseq(
      runtime,
      "asyncgen_hooks",
      {
          {"firstiter", g_asyncgen_firstiter},
          {"finalizer", g_asyncgen_finalizer},
      },
      "builtins",
      std::numeric_limits<size_t>::max(),
      "asyncgen_hooks");
}

#if defined(_WIN32)
Value make_windows_version(Runtime& runtime) {
  OSVERSIONINFOW version_info{};
  version_info.dwOSVersionInfoSize = sizeof(version_info);
  GetVersionExW(&version_info);
  const int64_t major = static_cast<int64_t>(version_info.dwMajorVersion);
  const int64_t minor = static_cast<int64_t>(version_info.dwMinorVersion);
  const int64_t build = static_cast<int64_t>(version_info.dwBuildNumber);
  return make_structseq(
      runtime,
      "windows_version",
      {
          {"major", Value::int64(major)},
          {"minor", Value::int64(minor)},
          {"build", Value::int64(build)},
          {"platform", Value::int64(static_cast<int64_t>(version_info.dwPlatformId))},
          {"service_pack", Value::string("")},
          {"service_pack_major", Value::int64(0)},
          {"service_pack_minor", Value::int64(0)},
          {"suite_mask", Value::int64(0)},
          {"product_type", Value::int64(0)},
          {"platform_version", Value::tuple({Value::int64(major), Value::int64(minor), Value::int64(build)})},
      },
      "sys",
      5,
      "sys.getwindowsversion");
}
#endif

Value make_builtin_module_names() {
  return Value::tuple({
      Value::string("_abc"),
      Value::string("_ast"),
      Value::string("_builtins"),
      Value::string("_codecs"),
      Value::string("_collections"),
      Value::string("_imp"),
      Value::string("_io"),
      Value::string("_queue"),
      Value::string("_signal"),
      Value::string("_socket"),
      Value::string("_stat"),
      Value::string("_string"),
      Value::string("_thread"),
      Value::string("_warnings"),
      Value::string("_weakref"),
      Value::string("abc"),
      Value::string("argparse"),
      Value::string("atexit"),
      Value::string("builtins"),
      Value::string("codecs"),
      Value::string("collections"),
      Value::string("contextlib"),
      Value::string("functools"),
      Value::string("importlib"),
      Value::string("io"),
      Value::string("json"),
      Value::string("math"),
      Value::string("os"),
      Value::string("platform"),
      Value::string("subprocess"),
      Value::string("sys"),
      Value::string("time"),
      Value::string("types"),
      Value::string("zipfile"),
      Value::string("zlib"),
  });
}

Value make_stdlib_module_names() {
  static constexpr const char* kNames[] = {
      "__future__", "_abc", "_aix_support", "_android_support", "_apple_support", "_ast", "_ast_unparse",
      "_asyncio", "_bisect", "_blake2", "_bz2", "_codecs", "_codecs_cn", "_codecs_hk", "_codecs_iso2022",
      "_codecs_jp", "_codecs_kr", "_codecs_tw", "_collections", "_collections_abc", "_colorize",
      "_compat_pickle", "_contextvars", "_csv", "_ctypes", "_curses", "_curses_panel", "_datetime", "_dbm",
      "_decimal", "_elementtree", "_frozen_importlib", "_frozen_importlib_external", "_functools", "_gdbm",
      "_hashlib", "_heapq", "_hmac", "_imp", "_interpchannels", "_interpqueues", "_interpreters", "_io",
      "_ios_support", "_json", "_locale", "_lsprof", "_lzma", "_markupbase", "_md5", "_multibytecodec",
      "_multiprocessing", "_opcode", "_opcode_metadata", "_operator", "_osx_support", "_overlapped", "_pickle",
      "_posixshmem", "_posixsubprocess", "_py_abc", "_py_warnings", "_pydatetime", "_pydecimal", "_pyio",
      "_pylong", "_pyrepl", "_queue", "_random", "_remote_debugging", "_scproxy", "_sha1", "_sha2", "_sha3",
      "_signal", "_sitebuiltins", "_socket", "_sqlite3", "_sre", "_ssl", "_stat", "_statistics", "_string",
      "_strptime", "_struct", "_suggestions", "_symtable", "_sysconfig", "_thread", "_threading_local",
      "_tkinter", "_tokenize", "_tracemalloc", "_types", "_typing", "_uuid", "_warnings", "_weakref",
      "_weakrefset", "_winapi", "_wmi", "_zoneinfo", "_zstd", "abc", "annotationlib", "antigravity",
      "argparse", "array", "ast", "asyncio", "atexit", "base64", "bdb", "binascii", "bisect", "builtins",
      "bz2", "cProfile", "calendar", "cmath", "cmd", "code", "codecs", "codeop", "collections", "colorsys",
      "compileall", "compression", "concurrent", "configparser", "contextlib", "contextvars", "copy",
      "copyreg", "csv", "ctypes", "curses", "dataclasses", "datetime", "dbm", "decimal", "difflib", "dis",
      "doctest", "email", "encodings", "ensurepip", "enum", "errno", "faulthandler", "fcntl", "filecmp",
      "fileinput", "fnmatch", "fractions", "ftplib", "functools", "gc", "genericpath", "getopt", "getpass",
      "gettext", "glob", "graphlib", "grp", "gzip", "hashlib", "heapq", "hmac", "html", "http", "idlelib",
      "imaplib", "importlib", "inspect", "io", "ipaddress", "itertools", "json", "keyword", "linecache",
      "locale", "logging", "lzma", "mailbox", "marshal", "math", "mimetypes", "mmap", "modulefinder",
      "msvcrt", "multiprocessing", "netrc", "nt", "ntpath", "nturl2path", "numbers", "opcode", "operator",
      "optparse", "os", "pathlib", "pdb", "pickle", "pickletools", "pkgutil", "platform", "plistlib",
      "poplib", "posix", "posixpath", "pprint", "profile", "pstats", "pty", "pwd", "py_compile", "pyclbr",
      "pydoc", "pydoc_data", "pyexpat", "queue", "quopri", "random", "re", "readline", "reprlib",
      "resource", "rlcompleter", "runpy", "sched", "secrets", "select", "selectors", "shelve", "shlex",
      "shutil", "signal", "site", "smtplib", "socket", "socketserver", "sqlite3", "sre_compile",
      "sre_constants", "sre_parse", "ssl", "stat", "statistics", "string", "stringprep", "struct",
      "subprocess", "symtable", "sys", "sysconfig", "syslog", "tabnanny", "tarfile", "tempfile", "termios",
      "textwrap", "this", "threading", "time", "timeit", "tkinter", "token", "tokenize", "tomllib", "trace",
      "traceback", "tracemalloc", "tty", "turtle", "turtledemo", "types", "typing", "unicodedata",
      "unittest", "urllib", "uuid", "venv", "warnings", "wave", "weakref", "webbrowser", "winreg",
      "winsound", "wsgiref", "xml", "xmlrpc", "zipapp", "zipfile", "zipimport", "zlib", "zoneinfo",
  };
  std::vector<Value> names;
  names.reserve(sizeof(kNames) / sizeof(kNames[0]));
  for (const char* name : kNames) {
    names.push_back(Value::string(name));
  }
  return Value::frozenset(std::move(names));
}

bool bytes_or_string_text(const Value& value, std::string& out) {
  if (auto* text = value_as_string(value)) {
    out = string_object_to_string(*text);
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  return false;
}

bool sys_path_arg(Runtime& runtime, const Value& value, std::string& out, std::string& error) {
  if (auto* text = value_as_string(value)) {
    out = string_object_to_string(*text);
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }

  Value fspath;
  std::string ignored;
  if (object_get_attr(value, "__fspath__", fspath, ignored)) {
    Value result;
    std::string call_error;
    if (!runtime_call_callable(runtime, fspath, nullptr, 0, result, call_error)) {
      error = call_error;
      return false;
    }
    if (auto* text = value_as_string(result)) {
      out = string_object_to_string(*text);
      return true;
    }
    if (auto* bytes = value_as_bytes(result)) {
      const auto view = bytes_object_view(*bytes);
      out.assign(view.data(), view.size());
      return true;
    }
  }

  error = "expected str, bytes or os.PathLike object, not " + sys_type_name(runtime, value);
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_stdio_write(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 2) {
    error = "stdio.write() expected data";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string data;
  for (uint32_t i = 1; i < argc; ++i) {
    std::string part;
    if (!bytes_or_string_text(args[i], part)) {
      error = "stdio.write() data must be str or bytes";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    data += part;
  }
  const char* kind = static_cast<const char*>(user_data);
  if (std::string(kind) == "stderr") {
    std::cerr.write(data.data(), static_cast<std::streamsize>(data.size()));
  } else {
    std::cout.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  out = Value::int64(static_cast<int64_t>(data.size()));
  return true;
}

bool sys_stdio_read(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "stdio.read() expected optional size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t size = 1;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "stdio.read() size must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    size = args[1].as.i64;
  }
  if (size < 0) {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    out = Value::bytes(buffer.str());
    return true;
  }
  std::string data(static_cast<size_t>(size), '\0');
  std::cin.read(data.data(), static_cast<std::streamsize>(size));
  data.resize(static_cast<size_t>(std::cin.gcount()));
  out = Value::bytes(std::move(data));
  return true;
}

bool sys_stdio_readline(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "stdio.readline() expected optional size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t limit = -1;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "stdio.readline() size must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    limit = args[1].as.i64;
  }
  std::string data;
  while (limit < 0 || static_cast<int64_t>(data.size()) < limit) {
    char ch = '\0';
    if (!std::cin.get(ch)) {
      break;
    }
    data.push_back(ch);
    if (ch == '\n') {
      break;
    }
  }
  out = Value::bytes(std::move(data));
  return true;
}

bool sys_stdio_flush(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "stdio.flush() expected no arguments";
    return false;
  }
  const char* kind = static_cast<const char*>(user_data);
  if (std::string(kind) == "stderr") {
    std::cerr.flush();
  } else {
    std::cout.flush();
  }
  value_set_none(out);
  return true;
}

bool sys_stdio_close(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "stdio.close() expected no arguments";
    return false;
  }
  value_set_none(out);
  return true;
}

bool raise_stdio_no_args_type_error(Runtime& runtime, std::string& error, const char* method, uint32_t argc) {
  const uint32_t given = argc == 0 ? 0 : argc - 1;
  error = std::string("TextIOWrapper.") + method + "() takes no arguments (" + std::to_string(given) + " given)";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_stdio_isatty(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_stdio_no_args_type_error(runtime, error, "isatty", argc);
  }
  value_set_bool(out, false);
  return true;
}

bool sys_stdio_readable(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    return raise_stdio_no_args_type_error(runtime, error, "readable", argc);
  }
  const char* kind = static_cast<const char*>(user_data);
  value_set_bool(out, std::string(kind) == "stdin");
  return true;
}

bool sys_stdio_writable(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    return raise_stdio_no_args_type_error(runtime, error, "writable", argc);
  }
  const char* kind = static_cast<const char*>(user_data);
  value_set_bool(out, std::string(kind) != "stdin");
  return true;
}

bool sys_stdio_seekable(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_stdio_no_args_type_error(runtime, error, "seekable", argc);
  }
  value_set_bool(out, false);
  return true;
}

bool sys_stdio_fileno(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "stdio.fileno() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string kind = static_cast<const char*>(user_data);
  if (kind == "stdin") {
    value_set_int64(out, 0);
  } else if (kind == "stdout") {
    value_set_int64(out, 1);
  } else {
    value_set_int64(out, 2);
  }
  return true;
}

Value make_sys_stdio(Runtime& runtime, const char* class_name, const char* kind) {
  std::vector<std::pair<std::string, Value>> attrs;
  auto stdio_method = [&](const char* method, NativeFunctionCallback callback) {
    Value function = runtime.make_native_function(std::string("sys.") + kind + "." + method, callback, const_cast<char*>(kind));
    if (auto* native = value_as_native_function(function)) {
      native->attrs_dict = new Value(Value::dict({
          {Value::string("__name__"), Value::string(method)},
          {Value::string("__qualname__"), Value::string(std::string("TextIOWrapper.") + method)},
      }));
    }
    return function;
  };
  attrs.push_back({"write", stdio_method("write", sys_stdio_write)});
  attrs.push_back({"read", stdio_method("read", sys_stdio_read)});
  attrs.push_back({"readline", stdio_method("readline", sys_stdio_readline)});
  attrs.push_back({"flush", stdio_method("flush", sys_stdio_flush)});
  attrs.push_back({"close", stdio_method("close", sys_stdio_close)});
  attrs.push_back({"isatty", stdio_method("isatty", sys_stdio_isatty)});
  attrs.push_back({"readable", stdio_method("readable", sys_stdio_readable)});
  attrs.push_back({"writable", stdio_method("writable", sys_stdio_writable)});
  attrs.push_back({"seekable", stdio_method("seekable", sys_stdio_seekable)});
  attrs.push_back({"fileno", stdio_method("fileno", sys_stdio_fileno)});
  Value klass = Value::class_object(class_name, std::move(attrs));
  Value stream = Value::instance(klass);
  std::string ignored;
  object_set_attr(stream, "encoding", Value::string("utf-8"), ignored);
  object_set_attr(stream, "errors", Value::string("strict"), ignored);
  object_set_attr(stream, "buffer", stream, ignored);
  object_set_attr(stream, "closed", Value::boolean(false), ignored);
  object_set_attr(stream, "line_buffering", Value::boolean(true), ignored);
  object_set_attr(stream, "_line_buffering", Value::boolean(true), ignored);
  return stream;
}

bool sys_exc_info(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.exc_info", argc);
  }
  const Value& exception = runtime.active_exception();
  if (exception.tag == ValueTag::Invalid) {
    out = Value::tuple({Value::none(), Value::none(), Value::none()});
    return true;
  }
  Value traceback = Value::none();
  std::string ignored;
  object_get_attr(exception, "__traceback__", traceback, ignored);
  out = Value::tuple({runtime.exception_type(exception), exception, traceback});
  return true;
}

bool sys_exception(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.exception", argc);
  }
  const Value& exception = runtime.active_exception();
  if (exception.tag == ValueTag::Invalid) {
    value_set_none(out);
  } else {
    value_assign_fast(out, exception);
  }
  return true;
}

bool sys_settrace(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.settrace", argc);
  }
  runtime.set_trace_function(args[0]);
  value_set_none(out);
  return true;
}

bool sys_gettrace(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.gettrace", argc);
  }
  if (runtime.trace_function().tag == ValueTag::Invalid) {
    value_set_none(out);
  } else {
    value_assign_fast(out, runtime.trace_function());
  }
  return true;
}

bool sys_settraceallthreads(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys._settraceallthreads", argc);
  }
  return sys_settrace(runtime, args, argc, out, error, user_data);
}

bool sys_call_tracing(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "call_tracing expected 2 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* tuple_args = value_as_tuple(args[1]);
  if (tuple_args == nullptr) {
    error = "sys.call_tracing argument list must be a tuple";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value previous_trace;
  if (runtime.trace_function().tag == ValueTag::Invalid) {
    value_set_invalid(previous_trace);
  } else {
    value_assign_fast(previous_trace, runtime.trace_function());
  }
  const bool ok = runtime_call_callable(
      runtime,
      args[0],
      tuple_args->items.empty() ? nullptr : tuple_args->items.begin(),
      static_cast<uint32_t>(tuple_args->items.size()),
      out,
      error);
  runtime.set_trace_function(previous_trace);
  return ok;
}

bool sys_frame_at_depth(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    const char* name,
    bool none_when_too_deep = false) {
  if (argc > 1) {
    const std::string function_name = std::string(name) == "sys._getframe" ? "_getframe" : "_getframemodulename";
    if (function_name == "_getframe") {
      error = function_name + " expected at most 1 argument, got " + std::to_string(argc);
    } else {
      error = function_name + "() takes at most 1 argument (" + std::to_string(argc) + " given)";
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t depth = 0;
  if (argc == 1) {
    if (args[0].tag == ValueTag::Bool) {
      depth = args[0].as.b ? 1 : 0;
    } else if (args[0].tag == ValueTag::Int64) {
      depth = args[0].as.i64;
    } else {
      error = "'" + sys_type_name(runtime, args[0]) + "' object cannot be interpreted as an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (depth < 0) {
      depth = 0;
    }
  }
  out = runtime.current_frame_snapshot();
  for (int64_t i = 0; i < depth; ++i) {
    if (out.tag == ValueTag::None) {
      if (none_when_too_deep) {
        value_set_none(out);
        return true;
      }
      error = "call stack is not deep enough";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    Value back;
    if (!object_get_attr(out, "f_back", back, error) || back.tag == ValueTag::None) {
      if (none_when_too_deep) {
        value_set_none(out);
        return true;
      }
      error = "call stack is not deep enough";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    value_assign_fast(out, back);
  }
  return true;
}

bool sys_getframe(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return sys_frame_at_depth(runtime, args, argc, out, error, "sys._getframe");
}

bool sys_getframemodulename(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value frame;
  if (!sys_frame_at_depth(runtime, args, argc, frame, error, "sys._getframemodulename", true)) {
    return false;
  }
  auto* frame_object = value_as_frame(frame);
  if (frame_object == nullptr) {
    value_set_none(out);
    return true;
  }
  auto* module = value_as_module(frame_object->globals_module);
  if (module == nullptr) {
    value_set_none(out);
  } else {
    out = Value::string(module->name);
  }
  return true;
}

bool sys_getframemodulename_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc + kwargc > 1) {
    if (argc > 0) {
      error = "_getframemodulename() takes at most 1 argument (" + std::to_string(argc + kwargc) + " given)";
    } else {
      error = "_getframemodulename() takes at most 1 keyword argument (" + std::to_string(kwargc) + " given)";
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (kwargc == 1) {
    const std::string name(kwargs[0].name == nullptr ? "" : kwargs[0].name);
    if (name != "depth") {
      error = "_getframemodulename() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (kwargs[0].value == nullptr) {
      error = "_getframemodulename() received invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    return sys_getframemodulename(runtime, kwargs[0].value, 1, out, error, nullptr);
  }
  return sys_getframemodulename(runtime, args, argc, out, error, nullptr);
}

bool sys_current_frames(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._current_frames", argc);
  }
  out = runtime.current_frame_snapshots(xlang_thread_active_idents());
  return true;
}

bool sys_current_exceptions(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._current_exceptions", argc);
  }
  out = runtime.current_exception_snapshots(xlang_thread_active_idents());
  return true;
}

bool sys_get_cpu_count_config(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._get_cpu_count_config", argc);
  }
  value_set_int64(out, -1);
  return true;
}

bool sys_clear_internal_caches(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._clear_internal_caches", argc);
  }
  value_set_none(out);
  return true;
}

bool sys_clear_type_cache(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return sys_clear_internal_caches(runtime, args, argc, out, error, user_data);
}

bool sys_clear_type_descriptors_immutable_type(const ClassObject& klass) {
  static constexpr const char* kImmutableTypeNames[] = {
      "BaseException",
      "BaseExceptionGroup",
      "Exception",
      "bool",
      "bytes",
      "complex",
      "dict",
      "float",
      "frozenset",
      "int",
      "list",
      "object",
      "set",
      "str",
      "tuple",
      "type",
  };
  for (const char* name : kImmutableTypeNames) {
    if (klass.name == name) {
      return true;
    }
  }
  return false;
}

bool sys_clear_type_descriptors(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)user_data;
  if (argc != 1) {
    error = "sys._clear_type_descriptors() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* klass = value_as_class(args[0]);
  if (klass == nullptr) {
    error = "_clear_type_descriptors() argument must be type, not " + sys_type_name(runtime, args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (sys_clear_type_descriptors_immutable_type(*klass)) {
    error = "argument is immutable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_clear_type_descriptors_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "sys._clear_type_descriptors() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_set_coroutine_origin_tracking_depth(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  int64_t depth = 0;
  if (argc < 1) {
    error = "set_coroutine_origin_tracking_depth() missing required argument 'depth' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 1) {
    error = "set_coroutine_origin_tracking_depth() takes at most 1 argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sys_bool_or_int_arg(args[0], depth)) {
    error = "'" + sys_type_name(runtime, args[0]) + "' object cannot be interpreted as an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (depth < 0) {
    error = "depth must be >= 0";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  g_coroutine_origin_tracking_depth = depth;
  value_set_none(out);
  return true;
}

bool sys_set_coroutine_origin_tracking_depth_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (kwargc > 1) {
    error = "set_coroutine_origin_tracking_depth() takes at most 1 keyword argument (" + std::to_string(kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc + kwargc > 1) {
    error = "set_coroutine_origin_tracking_depth() takes at most 1 argument (" + std::to_string(argc + kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (kwargc == 1) {
    const std::string name(kwargs[0].name == nullptr ? "" : kwargs[0].name);
    if (kwargs[0].value != nullptr && name == "depth") {
      return sys_set_coroutine_origin_tracking_depth(runtime, kwargs[0].value, 1, out, error, nullptr);
    }
  }
  return sys_set_coroutine_origin_tracking_depth(runtime, args, argc, out, error, nullptr);
}

bool sys_get_coroutine_origin_tracking_depth(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.get_coroutine_origin_tracking_depth", argc);
  }
  value_set_int64(out, g_coroutine_origin_tracking_depth);
  return true;
}

bool validate_asyncgen_hook(Runtime& runtime, const Value& hook, const char* name, std::string& error) {
  if (hook.tag == ValueTag::None || is_callable_value(hook)) {
    return true;
  }
  error = std::string("callable ") + name + " expected, got " + sys_type_name(runtime, hook);
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_get_asyncgen_hooks(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.get_asyncgen_hooks", argc);
  }
  out = make_asyncgen_hooks(runtime);
  return true;
}

bool sys_set_asyncgen_hooks_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (kwargc > 2) {
    error = "function takes at most 2 keyword arguments (" + std::to_string(kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 2 || argc + kwargc > 2) {
    error = "function takes at most 2 arguments (" + std::to_string(argc + kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* firstiter = argc >= 1 ? &args[0] : nullptr;
  const Value* finalizer = argc >= 2 ? &args[1] : nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      error = "sys.set_asyncgen_hooks received invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (name == "firstiter") {
      if (firstiter != nullptr) {
        error = "argument for function given by name ('firstiter') and position (1)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      firstiter = kwargs[i].value;
    } else if (name == "finalizer") {
      if (finalizer != nullptr) {
        error = "argument for function given by name ('finalizer') and position (2)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      finalizer = kwargs[i].value;
    } else {
      error = "this function got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (firstiter != nullptr) {
    if (!validate_asyncgen_hook(runtime, *firstiter, "firstiter", error)) {
      return false;
    }
  }
  if (finalizer != nullptr) {
    if (!validate_asyncgen_hook(runtime, *finalizer, "finalizer", error)) {
      return false;
    }
  }
  if (firstiter != nullptr) {
    value_assign_fast(g_asyncgen_firstiter, *firstiter);
  }
  if (finalizer != nullptr) {
    value_assign_fast(g_asyncgen_finalizer, *finalizer);
  }
  value_set_none(out);
  return true;
}

bool sys_set_asyncgen_hooks(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return sys_set_asyncgen_hooks_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool sys_set_asyncgen_hooks_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return sys_set_asyncgen_hooks_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

bool sys_getdefaultencoding(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.getdefaultencoding", argc);
  }
  out = Value::string("utf-8");
  return true;
}

bool sys_getfilesystemencoding(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.getfilesystemencoding", argc);
  }
  out = Value::string(g_filesystem_encoding);
  return true;
}

bool sys_getfilesystemencodeerrors(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.getfilesystemencodeerrors", argc);
  }
  out = Value::string(g_filesystem_encode_errors);
  return true;
}

bool sys_getrecursionlimit(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.getrecursionlimit", argc);
  }
  value_set_int64(out, g_recursion_limit);
  return true;
}

bool sys_setrecursionlimit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  int64_t limit = 0;
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.setrecursionlimit", argc);
  }
  if (!sys_bool_or_int_arg(args[0], limit)) {
    error = "'" + sys_type_name(runtime, args[0]) + "' object cannot be interpreted as an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (limit < 1 || limit > std::numeric_limits<int>::max()) {
    error = "recursion limit must be greater or equal than 1";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (limit <= 1) {
    error = "cannot set the recursion limit to 1 at the recursion depth 1: the limit is too low";
    runtime.raise_class_error("RecursionError", error);
    return false;
  }
  g_recursion_limit = static_cast<int>(limit);
  value_set_none(out);
  return true;
}

std::string sys_type_name(Runtime& runtime, const Value& value) {
  Value type;
  if (runtime_type_of_value(runtime, value, type)) {
    if (auto* klass = value_as_class(type)) {
      return klass->name;
    }
  }
  return value_to_string(type);
}

bool sys_intern(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "intern() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_string(args[0]) == nullptr) {
    error = "intern() argument must be str, not " + sys_type_name(runtime, args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = intern_string_value(args[0]);
  return true;
}

bool sys_intern_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "sys.intern() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_is_interned(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys._is_interned() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_string(args[0]) == nullptr) {
    error = "_is_interned() argument must be str, not " + sys_type_name(runtime, args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_bool(out, string_value_is_interned(args[0]));
  return true;
}

bool sys_is_interned_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "sys._is_interned() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_getunicodeinternedsize(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "getunicodeinternedsize() takes no positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_int64(out, interned_string_count());
  return true;
}

bool sys_getunicodeinternedsize_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  const std::string name(kwargc > 0 && kwargs[0].name != nullptr ? kwargs[0].name : "");
  error = "getunicodeinternedsize() got an unexpected keyword argument '" + name + "'";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_is_immortal(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys._is_immortal() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_bool(out, args[0].tag != ValueTag::Object);
  return true;
}

bool sys_is_immortal_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "sys._is_immortal() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

int64_t shallow_sizeof(const Value& value) {
  switch (value.tag) {
    case ValueTag::Invalid:
    case ValueTag::None:
      return 16;
    case ValueTag::Bool:
    case ValueTag::Int64:
      return 28;
    case ValueTag::Double:
      return 24;
    case ValueTag::Object:
      if (auto* string = value_as_string(value)) {
        return static_cast<int64_t>(sizeof(StringObject) + string->size);
      }
      if (auto* bytes = value_as_bytes(value)) {
        return static_cast<int64_t>(sizeof(BytesObject) + bytes->size);
      }
      if (auto* bytearray = value_as_bytearray(value)) {
        return static_cast<int64_t>(sizeof(ByteArrayObject) + bytearray->value.capacity());
      }
      if (auto* tuple = value_as_tuple(value)) {
        return static_cast<int64_t>(sizeof(TupleObject) + tuple->items.capacity() * sizeof(Value));
      }
      if (auto* list = value_as_list(value)) {
        return static_cast<int64_t>(sizeof(ListObject) + list->items.capacity() * sizeof(Value));
      }
      if (auto* dict = value_as_dict(value)) {
        return static_cast<int64_t>(sizeof(DictObject) + dict->entries.capacity() * sizeof(std::pair<Value, Value>));
      }
      return 64;
  }
  return 0;
}

bool sys_getsizeof(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "getsizeof() missing required argument 'object' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 2) {
    error = "getsizeof() takes at most 2 arguments (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value sizeof_method;
  std::string attr_error;
  if (object_get_attr(args[0], "__sizeof__", sizeof_method, attr_error)) {
    if (!is_callable_value(sizeof_method)) {
      if (argc == 2) {
        value_assign_fast(out, args[1]);
        return true;
      }
      error = "__sizeof__ must be callable";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    Value size;
    if (!runtime_call_callable(runtime, sizeof_method, nullptr, 0, size, error)) {
      if (argc == 2) {
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          Value exception_type = runtime.exception_type(pending);
          auto* klass = value_as_class(exception_type);
          if (klass != nullptr && klass->name == "TypeError") {
            value_assign_fast(out, args[1]);
            return true;
          }
          runtime.set_pending_exception(std::move(pending));
        }
      }
      return false;
    }
    if (size.tag == ValueTag::Bool) {
      value_set_int64(out, size.as.b ? 1 : 0);
      return true;
    }
    if (size.tag != ValueTag::Int64) {
      if (argc == 2) {
        value_assign_fast(out, args[1]);
        return true;
      }
      error = "__sizeof__() should return int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (size.as.i64 < 0) {
      error = "__sizeof__() should return >= 0";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    value_assign_fast(out, size);
    return true;
  }
  value_set_int64(out, shallow_sizeof(args[0]));
  return true;
}

bool sys_getsizeof_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (kwargc > 2) {
    error = "getsizeof() takes at most 2 keyword arguments (" + std::to_string(kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc + kwargc > 2) {
    error = "getsizeof() takes at most 2 arguments (" + std::to_string(argc + kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  Value bound_args[2];
  bool has_object = argc >= 1;
  bool has_default = argc >= 2;
  if (argc >= 1) {
    value_assign_fast(bound_args[0], args[0]);
  }
  if (argc >= 2) {
    value_assign_fast(bound_args[1], args[1]);
  }

  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      error = "getsizeof() got an invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (name == "object") {
      if (has_object) {
        error = "argument for getsizeof() given by name ('object') and position (1)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(bound_args[0], *kwargs[i].value);
      has_object = true;
    } else if (name == "default") {
      if (has_default) {
        error = "getsizeof() got multiple values for keyword argument 'default'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(bound_args[1], *kwargs[i].value);
      has_default = true;
    } else {
      if (!has_object) {
        error = "getsizeof() missing required argument 'object' (pos 1)";
      } else {
        error = "getsizeof() got an unexpected keyword argument '" + name + "'";
      }
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (!has_object) {
    error = "getsizeof() missing required argument 'object' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return sys_getsizeof(runtime, bound_args, has_default ? 2 : 1, out, error, nullptr);
}

bool sys_getrefcount(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.getrefcount() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].tag == ValueTag::Object && args[0].as.obj != nullptr) {
    const uint32_t refcount = args[0].as.obj->refcnt.load(std::memory_order_relaxed);
    value_set_int64(out, static_cast<int64_t>(refcount) + 1);
    return true;
  }
  value_set_int64(out, 1);
  return true;
}

bool sys_getrefcount_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "sys.getrefcount() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

uint64_t live_block_count(const memory::X3MemoryCounter& counter) {
  return counter.alloc_count >= counter.free_count ? counter.alloc_count - counter.free_count : 0;
}

bool sys_getallocatedblocks(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.getallocatedblocks", argc);
  }
  const auto& object_stats = memory::x3_thread_object_pools().stats();
  const auto& bucket_stats = memory::x3_thread_buckets().bucket_stats();
  const auto& large_stats = memory::x3_thread_buckets().large_stats();
  const uint64_t blocks =
      live_block_count(object_stats) + live_block_count(bucket_stats) + live_block_count(large_stats);
  value_set_int64(out, blocks > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
                           ? std::numeric_limits<int64_t>::max()
                           : static_cast<int64_t>(blocks));
  return true;
}

bool sys_getallocatedblocks_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "sys.getallocatedblocks() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "exit expected at most 1 argument, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value exception = runtime.make_exception("SystemExit", "");
  std::string ignored;
  object_set_attr(exception, "code", argc == 0 ? Value::none() : args[0], ignored);
  object_set_attr(exception, "args", argc == 0 ? Value::tuple({}) : Value::tuple({args[0]}), ignored);
  runtime.set_pending_exception(std::move(exception));
  value_set_none(out);
  return false;
}

bool sys_exit_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "sys.exit() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool sys_write_stream(Runtime& runtime, const char* stream_name, const std::string& text, std::string& error) {
  Value sys;
  if (runtime.import_module("sys", sys, error)) {
    Value stream;
    std::string attr_error;
    if (module_get_attr(sys, stream_name, stream, attr_error)) {
      Value write;
      if (object_get_attr(stream, "write", write, attr_error)) {
        Value write_arg = Value::string(text);
        Value ignored;
        return runtime_call_callable(runtime, write, &write_arg, 1, ignored, error);
      }
    }
  }
  if (std::string(stream_name) == "stderr") {
    std::cerr << text;
  } else {
    runtime.write_output(text.c_str(), text.size());
  }
  return true;
}

std::string sys_exception_type_name(const Value& type) {
  if (auto* klass = value_as_class(type)) {
    return klass->name;
  }
  Value name;
  std::string ignored;
  if (object_get_attr(type, "__name__", name, ignored) && value_as_string(name) != nullptr) {
    return string_object_to_string(*value_as_string(name));
  }
  return value_to_string(type);
}

bool sys_set_builtin_underscore(Runtime& runtime, const Value& value, std::string& error) {
  Value builtins;
  if (!runtime.import_module("builtins", builtins, error)) {
    return false;
  }
  return module_set_attr(builtins, "_", value, error);
}

std::string sys_displayhook_repr(const Value& value) {
  if (auto* string = value_as_string(value)) {
    std::string text = "'";
    for (const unsigned char ch : string_object_view(*string)) {
      if (ch == '\\' || ch == '\'') {
        text.push_back('\\');
        text.push_back(static_cast<char>(ch));
      } else if (ch == '\n') {
        text += "\\n";
      } else if (ch == '\r') {
        text += "\\r";
      } else if (ch == '\t') {
        text += "\\t";
      } else if (ch >= 32 && ch < 127) {
        text.push_back(static_cast<char>(ch));
      } else {
        constexpr char hex[] = "0123456789abcdef";
        text += "\\x";
        text.push_back(hex[ch >> 4]);
        text.push_back(hex[ch & 0xf]);
      }
    }
    text.push_back('\'');
    return text;
  }
  return value_to_string(value);
}

bool sys_displayhook(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.displayhook() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].tag != ValueTag::None) {
    if (!sys_set_builtin_underscore(runtime, Value::none(), error) ||
        !sys_write_stream(runtime, "stdout", sys_displayhook_repr(args[0]) + "\n", error) ||
        !sys_set_builtin_underscore(runtime, args[0], error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool sys_excepthook(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "excepthook expected 3 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string type_name = sys_exception_type_name(args[0]);
  const std::string value_text = value_to_string(args[1]);
  const std::string line = value_text.empty() ? type_name + "\n" : type_name + ": " + value_text + "\n";
  if (!sys_write_stream(runtime, "stderr", line, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_unraisablehook(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.unraisablehook() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_breakpointhook(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool sys_breakpointhook_kw(
    Runtime&,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string&,
    void*) {
  value_set_none(out);
  return true;
}

bool sys_addaudithook_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (kwargc > 1) {
    error = "addaudithook() takes at most 1 keyword argument (" + std::to_string(kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc + kwargc > 1) {
    error = "addaudithook() takes at most 1 argument (" + std::to_string(argc + kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc < 1) {
    if (kwargc == 1 && std::string(kwargs[0].name == nullptr ? "" : kwargs[0].name) == "hook" && kwargs[0].value != nullptr) {
      g_audit_hooks.push_back(*kwargs[0].value);
      value_set_none(out);
      return true;
    }
    error = "addaudithook() missing required argument 'hook' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  g_audit_hooks.push_back(args[0]);
  value_set_none(out);
  return true;
}

bool sys_addaudithook(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return sys_addaudithook_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool sys_addaudithook_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return sys_addaudithook_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

bool sys_audit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "audit expected at least 1 argument, got 0";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_string(args[0]) == nullptr) {
    const std::string type_name = args[0].tag == ValueTag::None ? "None" : sys_type_name(runtime, args[0]);
    error = "audit() argument 1 must be str, not " + type_name;
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> event_args;
  event_args.reserve(argc > 0 ? argc - 1 : 0);
  for (uint32_t i = 1; i < argc; ++i) {
    event_args.push_back(args[i]);
  }
  Value hook_args[] = {args[0], Value::tuple(std::move(event_args))};
  for (const auto& hook : g_audit_hooks) {
    Value ignored;
    if (!runtime_call_callable(runtime, hook, hook_args, 2, ignored, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool sys_audit_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (kwargc > 0) {
    error = "sys.audit() takes no keyword arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return sys_audit(runtime, args, argc, out, error, nullptr);
}

bool sys_setprofile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.setprofile", argc);
  }
  runtime.set_profile_function(args[0]);
  value_set_none(out);
  return true;
}

bool sys_getprofile(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.getprofile", argc);
  }
  if (runtime.profile_function().tag == ValueTag::Invalid) {
    value_set_none(out);
  } else {
    value_assign_fast(out, runtime.profile_function());
  }
  return true;
}

bool sys_setprofileallthreads(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys._setprofileallthreads", argc);
  }
  return sys_setprofile(runtime, args, argc, out, error, user_data);
}

double g_switch_interval = 0.005;

bool sys_getswitchinterval(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.getswitchinterval", argc);
  }
  out = Value::number(g_switch_interval);
  return true;
}

bool sys_setswitchinterval(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.setswitchinterval", argc);
  }
  if (args[0].tag != ValueTag::Bool && args[0].tag != ValueTag::Int64 && args[0].tag != ValueTag::Double) {
    error = "must be real number, not " + sys_type_name(runtime, args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const double value = args[0].tag == ValueTag::Bool ? (args[0].as.b ? 1.0 : 0.0) : args[0].tag == ValueTag::Int64 ? static_cast<double>(args[0].as.i64) : args[0].as.f64;
  if (value <= 0.0) {
    error = "switch interval must be strictly positive";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  g_switch_interval = value;
  value_set_none(out);
  return true;
}

bool sys_get_int_max_str_digits(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.get_int_max_str_digits", argc);
  }
  value_set_int64(out, g_int_max_str_digits);
  return true;
}

bool sys_set_int_max_str_digits(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  int64_t max_digits = 0;
  if (argc < 1) {
    error = "set_int_max_str_digits() missing required argument 'maxdigits' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 1) {
    error = "set_int_max_str_digits() takes at most 1 argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sys_bool_or_int_arg(args[0], max_digits)) {
    error = "'" + sys_type_name(runtime, args[0]) + "' object cannot be interpreted as an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (max_digits != 0 && max_digits < kIntStrDigitsCheckThreshold) {
    error = "maxdigits must be >= 640 or 0 for unlimited";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  g_int_max_str_digits = max_digits;
  Value sys;
  if (runtime.import_module("sys", sys, error)) {
    Value flags;
    std::string ignored;
    if (module_get_attr(sys, "flags", flags, ignored)) {
      object_set_attr(flags, "int_max_str_digits", Value::int64(g_int_max_str_digits), ignored);
      Value tuple_value;
      if (object_get_attr(flags, "_tuple", tuple_value, ignored)) {
        if (auto* tuple = value_as_tuple(tuple_value); tuple != nullptr && tuple->items.size() > 17) {
          tuple->items[17] = Value::int64(g_int_max_str_digits);
          sys_structseq_update_string_value(flags, ignored);
        }
      }
    }
  }
  value_set_none(out);
  return true;
}

bool sys_set_int_max_str_digits_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc + kwargc > 1) {
    if (argc > 0) {
      error = "set_int_max_str_digits() takes at most 1 argument (" + std::to_string(argc + kwargc) + " given)";
    } else {
      error = "set_int_max_str_digits() takes at most 1 keyword argument (" + std::to_string(kwargc) + " given)";
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 0 && kwargc == 0) {
    error = "set_int_max_str_digits() missing required argument 'maxdigits' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (kwargc == 1) {
    const std::string name(kwargs[0].name == nullptr ? "" : kwargs[0].name);
    if (name != "maxdigits") {
      error = "set_int_max_str_digits() missing required argument 'maxdigits' (pos 1)";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (kwargs[0].value == nullptr) {
      error = "set_int_max_str_digits() received invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    return sys_set_int_max_str_digits(runtime, kwargs[0].value, 1, out, error, nullptr);
  }
  return sys_set_int_max_str_digits(runtime, args, argc, out, error, nullptr);
}

bool sys_is_finalizing(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.is_finalizing", argc);
  }
  out = Value::boolean(false);
  return true;
}

bool sys_is_remote_debug_enabled(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.is_remote_debug_enabled", argc);
  }
  out = Value::boolean(true);
  return true;
}

bool sys_is_gil_enabled(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._is_gil_enabled", argc);
  }
  out = Value::boolean(true);
  return true;
}

bool sys_activate_stack_trampoline(Runtime& runtime, const Value* args, uint32_t argc, Value&, std::string& error, void*) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.activate_stack_trampoline", argc);
  }
  StringObject* backend = value_as_string(args[0]);
  if (backend == nullptr) {
    error = "activate_stack_trampoline() argument must be str, not " + sys_type_name(runtime, args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  error = string_object_to_string(*backend) + " trampoline not available";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool sys_deactivate_stack_trampoline(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.deactivate_stack_trampoline", argc);
  }
  value_set_none(out);
  return true;
}

bool sys_is_stack_trampoline_active(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.is_stack_trampoline_active", argc);
  }
  out = Value::boolean(false);
  return true;
}

bool sys_jit_is_available(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._jit.is_available", argc);
  }
  out = Value::boolean(false);
  return true;
}

bool sys_jit_is_enabled(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._jit.is_enabled", argc);
  }
  out = Value::boolean(false);
  return true;
}

bool sys_jit_is_active(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._jit.is_active", argc);
  }
  out = Value::boolean(false);
  return true;
}

bool monitoring_tool_id(Runtime& runtime, const Value& value, int64_t& out, std::string& error) {
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
  } else if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
  } else {
    error = "object cannot be interpreted as an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (out < 0 || out >= kMonitoringToolCount) {
    error = "invalid tool " + std::to_string(out) + " (must be between 0 and 5)";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool monitoring_event_value(Runtime& runtime, const Value& value, int64_t& out, std::string& error) {
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
  } else if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
  } else {
    error = "object cannot be interpreted as an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (out < 0 || (out & ~kMonitoringEventMask) != 0) {
    std::ostringstream stream;
    stream << "invalid event set 0x" << std::hex << static_cast<uint64_t>(out);
    error = stream.str();
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool monitoring_event_set(Runtime& runtime, const Value& value, int64_t& out, std::string& error) {
  if (!monitoring_event_value(runtime, value, out, error)) {
    return false;
  }
  const int64_t c_events = kMonitoringEventCReturn | kMonitoringEventCRaise;
  if ((out & c_events) != 0) {
    error = "cannot set C_RETURN or C_RAISE events independently";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool monitoring_single_event(Runtime& runtime, const Value& value, int64_t& out, std::string& error) {
  if (!monitoring_event_value(runtime, value, out, error)) {
    return false;
  }
  if (out == 0 || (out & (out - 1)) != 0) {
    error = "The callback can only be set for one event at a time";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool sys_monitoring_use_tool_id(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "use_tool_id expected 2 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t tool_id = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error)) {
    return false;
  }
  if (value_as_string(args[1]) == nullptr) {
    error = "tool name must be a str";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto& tool = g_monitoring_tools[static_cast<size_t>(tool_id)];
  if (tool.name.tag != ValueTag::None) {
    error = "tool " + std::to_string(tool_id) + " is already in use";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  tool.name = args[1];
  value_set_none(out);
  return true;
}

bool sys_monitoring_free_tool_id(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.monitoring.free_tool_id", argc);
  }
  int64_t tool_id = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error)) {
    return false;
  }
  auto& tool = g_monitoring_tools[static_cast<size_t>(tool_id)];
  tool.name = Value::none();
  tool.events = 0;
  tool.callbacks.clear();
  value_set_none(out);
  return true;
}

bool sys_monitoring_clear_tool_id(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.monitoring.clear_tool_id", argc);
  }
  int64_t tool_id = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error)) {
    return false;
  }
  auto& tool = g_monitoring_tools[static_cast<size_t>(tool_id)];
  tool.events = 0;
  tool.callbacks.clear();
  value_set_none(out);
  return true;
}

bool sys_monitoring_get_tool(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.monitoring.get_tool", argc);
  }
  int64_t tool_id = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error)) {
    return false;
  }
  out = g_monitoring_tools[static_cast<size_t>(tool_id)].name;
  return true;
}

bool sys_monitoring_set_events(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "set_events expected 2 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t tool_id = 0;
  int64_t events = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error) ||
      !monitoring_event_set(runtime, args[1], events, error)) {
    return false;
  }
  auto& tool = g_monitoring_tools[static_cast<size_t>(tool_id)];
  if (tool.name.tag == ValueTag::None) {
    error = "tool " + std::to_string(tool_id) + " is not in use";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  tool.events = events;
  value_set_none(out);
  return true;
}

bool sys_monitoring_get_events(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_sys_one_arg_type_error(runtime, error, "sys.monitoring.get_events", argc);
  }
  int64_t tool_id = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error)) {
    return false;
  }
  value_set_int64(out, g_monitoring_tools[static_cast<size_t>(tool_id)].events);
  return true;
}

bool sys_monitoring_set_local_events(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "set_local_events expected 3 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t tool_id = 0;
  int64_t events = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error)) {
    return false;
  }
  auto* code = value_as_code(args[1]);
  if (code == nullptr) {
    error = "code must be a code object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto& tool = g_monitoring_tools[static_cast<size_t>(tool_id)];
  if (tool.name.tag == ValueTag::None) {
    error = "tool " + std::to_string(tool_id) + " is not in use";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (!monitoring_event_set(runtime, args[2], events, error)) {
    return false;
  }
  auto& local_events = tool.local_events;
  const MonitoringCodeKey key = monitoring_code_key(*code);
  if (events == 0) {
    local_events.erase(key);
  } else {
    local_events[key] = events;
  }
  value_set_none(out);
  return true;
}

bool sys_monitoring_get_local_events(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "get_local_events expected 2 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t tool_id = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error)) {
    return false;
  }
  auto* code = value_as_code(args[1]);
  if (code == nullptr) {
    error = "code must be a code object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto& local_events = g_monitoring_tools[static_cast<size_t>(tool_id)].local_events;
  const auto found = local_events.find(monitoring_code_key(*code));
  value_set_int64(out, found == local_events.end() ? 0 : found->second);
  return true;
}

bool sys_monitoring_register_callback(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "register_callback expected 3 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t tool_id = 0;
  int64_t event = 0;
  if (!monitoring_tool_id(runtime, args[0], tool_id, error) ||
      !monitoring_single_event(runtime, args[1], event, error)) {
    return false;
  }
  auto& callbacks = g_monitoring_tools[static_cast<size_t>(tool_id)].callbacks;
  const auto found = callbacks.find(event);
  if (found == callbacks.end()) {
    value_set_none(out);
  } else {
    out = found->second;
  }
  if (args[2].tag == ValueTag::None) {
    callbacks.erase(event);
  } else {
    callbacks[event] = args[2];
  }
  return true;
}

bool sys_monitoring_restart_events(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.monitoring.restart_events", argc);
  }
  value_set_none(out);
  return true;
}

bool sys_monitoring_all_events(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.monitoring._all_events", argc);
  }
  out = Value::dict({});
  return true;
}

} // namespace

bool sys_monitoring_dispatch_event(
    Runtime& runtime,
    int64_t event,
    const Value& code,
    int64_t instruction_offset,
    const Value* arg,
    std::string& error) {
  if (g_monitoring_dispatch_active) {
    return true;
  }
  g_monitoring_dispatch_active = true;
  struct DispatchGuard {
    ~DispatchGuard() { g_monitoring_dispatch_active = false; }
  } guard;

  for (auto& tool : g_monitoring_tools) {
    int64_t enabled_events = tool.events;
    if (auto* code_object = value_as_code(code)) {
      const auto local_it = tool.local_events.find(monitoring_code_key(*code_object));
      if (local_it != tool.local_events.end()) {
        enabled_events |= local_it->second;
      }
    }
    const bool c_call_result_event = event == kMonitoringEventCReturn || event == kMonitoringEventCRaise;
    if (tool.name.tag == ValueTag::None || ((enabled_events & event) == 0 && !(c_call_result_event && (enabled_events & kMonitoringEventCall) != 0))) {
      continue;
    }
    auto callback_it = tool.callbacks.find(event);
    if (callback_it == tool.callbacks.end() || callback_it->second.tag == ValueTag::None) {
      continue;
    }
    Value callback_args_storage[3] = {
        code,
        Value::int64(instruction_offset),
        arg != nullptr ? *arg : Value::none(),
    };
    Value ignored;
    const uint32_t callback_argc = arg != nullptr ? 3u : 2u;
    if (!runtime_call_callable(runtime, callback_it->second, callback_args_storage, callback_argc, ignored, error)) {
      return false;
    }
  }
  return true;
}

namespace {

Value make_monitoring_events() {
  Value events = Value::instance(Value::class_object(
      "SimpleNamespace",
      {{"__module__", Value::string("types")}}));
  std::string ignored;
  object_set_attr(events, "PY_START", Value::int64(kMonitoringEventPyStart), ignored);
  object_set_attr(events, "PY_RESUME", Value::int64(kMonitoringEventPyResume), ignored);
  object_set_attr(events, "PY_RETURN", Value::int64(kMonitoringEventPyReturn), ignored);
  object_set_attr(events, "PY_YIELD", Value::int64(kMonitoringEventPyYield), ignored);
  object_set_attr(events, "CALL", Value::int64(kMonitoringEventCall), ignored);
  object_set_attr(events, "LINE", Value::int64(kMonitoringEventLine), ignored);
  object_set_attr(events, "INSTRUCTION", Value::int64(kMonitoringEventInstruction), ignored);
  object_set_attr(events, "JUMP", Value::int64(kMonitoringEventJump), ignored);
  object_set_attr(events, "BRANCH_LEFT", Value::int64(kMonitoringEventBranchLeft), ignored);
  object_set_attr(events, "BRANCH_RIGHT", Value::int64(kMonitoringEventBranchRight), ignored);
  object_set_attr(events, "STOP_ITERATION", Value::int64(kMonitoringEventStopIteration), ignored);
  object_set_attr(events, "RAISE", Value::int64(kMonitoringEventRaise), ignored);
  object_set_attr(events, "EXCEPTION_HANDLED", Value::int64(kMonitoringEventExceptionHandled), ignored);
  object_set_attr(events, "PY_UNWIND", Value::int64(kMonitoringEventPyUnwind), ignored);
  object_set_attr(events, "PY_THROW", Value::int64(kMonitoringEventPyThrow), ignored);
  object_set_attr(events, "RERAISE", Value::int64(kMonitoringEventReraise), ignored);
  object_set_attr(events, "C_RETURN", Value::int64(kMonitoringEventCReturn), ignored);
  object_set_attr(events, "C_RAISE", Value::int64(kMonitoringEventCRaise), ignored);
  object_set_attr(events, "BRANCH", Value::int64(kMonitoringEventBranch), ignored);
  object_set_attr(events, "NO_EVENTS", Value::int64(0), ignored);
  object_set_attr(
      events,
      "__xlang3_string_value__",
      Value::string(
          "namespace(PY_START=1, PY_RESUME=2, PY_RETURN=4, PY_YIELD=8, CALL=16, "
          "LINE=32, INSTRUCTION=64, JUMP=128, BRANCH_LEFT=256, BRANCH_RIGHT=512, "
          "STOP_ITERATION=1024, RAISE=2048, EXCEPTION_HANDLED=4096, PY_UNWIND=8192, "
          "PY_THROW=16384, RERAISE=32768, C_RETURN=65536, C_RAISE=131072, "
          "BRANCH=262144, NO_EVENTS=0)"),
      ignored);
  return events;
}

#if defined(_WIN32)
bool sys_getwindowsversion(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys.getwindowsversion", argc);
  }
  out = make_windows_version(runtime);
  return true;
}

bool sys_enablelegacywindowsfsencoding(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._enablelegacywindowsfsencoding", argc);
  }
  g_filesystem_encoding = "mbcs";
  g_filesystem_encode_errors = "replace";
  value_set_none(out);
  return true;
}
#endif

bool sys_debugmallocstats(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return raise_sys_no_args_type_error(runtime, error, "sys._debugmallocstats", argc);
  }
  const auto& object_stats = memory::x3_thread_object_pools().stats();
  const auto& bucket_stats = memory::x3_thread_buckets().bucket_stats();
  const auto& large_stats = memory::x3_thread_buckets().large_stats();
  std::ostringstream stats;
  stats << "XLang3 allocator stats\n"
        << "object_blocks=" << live_block_count(object_stats) << "\n"
        << "bucket_blocks=" << live_block_count(bucket_stats) << "\n"
        << "large_blocks=" << live_block_count(large_stats) << "\n";
  if (!sys_write_stream(runtime, "stderr", stats.str(), error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_dump_tracelets(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "_dump_tracelets() missing required argument 'outpath' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 1) {
    error = "_dump_tracelets() takes at most 1 argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string outpath;
  if (!sys_path_arg(runtime, args[0], outpath, error)) {
    return false;
  }
  std::ofstream file(outpath, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "could not open tracelet dump path";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  file << "digraph ideal {\n\n    rankdir = \"LR\"\n\n}\n\n";
  if (!file) {
    error = "could not write tracelet dump";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_dump_tracelets_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (kwargc > 1) {
    error = "_dump_tracelets() takes at most 1 keyword argument (" + std::to_string(kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc + kwargc > 1) {
    error = "_dump_tracelets() takes at most 1 argument (" + std::to_string(argc + kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (kwargc == 1) {
    const std::string name(kwargs[0].name == nullptr ? "" : kwargs[0].name);
    if (kwargs[0].value != nullptr && name == "outpath") {
      return sys_dump_tracelets(runtime, kwargs[0].value, 1, out, error, nullptr);
    }
  }
  return sys_dump_tracelets(runtime, args, argc, out, error, nullptr);
}

bool sys_xlang3_debug_set_hook(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys._xlang3_debug_set_hook expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].tag != ValueTag::None && value_as_function(args[0]) == nullptr) {
    error = "sys._xlang3_debug_set_hook expected function or None";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.set_debug_hook(args[0]);
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_add_breakpoint(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "sys._xlang3_debug_add_breakpoint expected 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* file = value_as_string(args[0]);
  if (file == nullptr || args[1].tag != ValueTag::Int64 || args[1].as.i64 <= 0) {
    error = "sys._xlang3_debug_add_breakpoint expected file and positive line";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_add_breakpoint(string_object_to_string(*file), static_cast<uint32_t>(args[1].as.i64));
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_clear_breakpoints(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_clear_breakpoints expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_clear_breakpoints();
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_step_into(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_step_into expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_step_into(0, 0);
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_step_over(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64 || args[0].as.i64 <= 0 ||
      args[1].tag != ValueTag::Int64 || args[1].as.i64 <= 0) {
    error = "sys._xlang3_debug_step_over expected frame_count and line";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_step_over(static_cast<size_t>(args[0].as.i64), static_cast<uint32_t>(args[1].as.i64));
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_step_out(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64 || args[0].as.i64 <= 0) {
    error = "sys._xlang3_debug_step_out expected frame_count";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_step_out(static_cast<size_t>(args[0].as.i64));
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_request_pause(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_request_pause expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_request_pause();
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_continue(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_continue expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_continue();
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_poll_needed(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_poll_needed expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::boolean(runtime.debug_poll_needed());
  return true;
}

} // namespace

void register_sys_module(Runtime& runtime) {
  NativeModuleBuilder sys_builder(runtime, "sys");
  auto sys = sys_builder.finish();
  std::string error;
  Value modules_ref;
  value_borrow_assign_fast(modules_ref, runtime.module_registry_dict());
  module_set_attr(sys, "modules", modules_ref, error);
  module_set_attr(
      sys,
      "__doc__",
      Value::string(
          "This module provides access to some objects used or maintained by the\n"
          "interpreter and to functions that interact strongly with the interpreter.\n"
          "\n"
          "Dynamic objects:\n"
          "\n"
          "argv -- command line arguments; argv[0] is the script pathname if known\n"
          "path -- module search path; path[0] is the script directory, else ''\n"
          "modules -- dictionary of loaded modules\n"
          "\n"
          "displayhook -- called to show results in an interactive session\n"
          "excepthook -- called to handle any uncaught exception other than SystemExit\n"
          "  To customize printing in an interactive session or to install a custom\n"
          "  top-level exception handler, assign other functions to replace these.\n"
          "\n"
          "stdin -- standard input file object; used by input()\n"
          "stdout -- standard output file object; used by print()\n"
          "stderr -- standard error object; used for error messages\n"
          "  By assigning other file objects (or objects that behave like files)\n"
          "  to these, it is possible to redirect all of the interpreter's I/O.\n"
          "\n"
          "last_exc - the last uncaught exception\n"
          "  Only available in an interactive session after a\n"
          "  traceback has been printed.\n"
          "last_type -- type of last uncaught exception\n"
          "last_value -- value of last uncaught exception\n"
          "last_traceback -- traceback of last uncaught exception\n"
          "  These three are the (deprecated) legacy representation of last_exc.\n"
          "\n"
          "Static objects:\n"
          "\n"
          "builtin_module_names -- tuple of module names built into this interpreter\n"
          "copyright -- copyright notice pertaining to this interpreter\n"
          "exec_prefix -- prefix used to find the machine-specific Python library\n"
          "executable -- absolute path of the executable binary of the Python interpreter\n"
          "float_info -- a named tuple with information about the float implementation.\n"
          "float_repr_style -- string indicating the style of repr() output for floats\n"
          "hash_info -- a named tuple with information about the hash algorithm.\n"
          "hexversion -- version information encoded as a single integer\n"
          "implementation -- Python implementation information.\n"
          "int_info -- a named tuple with information about the int implementation.\n"
          "maxsize -- the largest supported length of containers.\n"
          "maxunicode -- the value of the largest Unicode code point\n"
          "platform -- platform identifier\n"
          "prefix -- prefix used to find the Python library\n"
          "thread_info -- a named tuple with information about the thread implementation.\n"
          "version -- the version of this interpreter as a string\n"
          "version_info -- version information as a named tuple\n"
          "dllhandle -- [Windows only] integer handle of the Python DLL\n"
          "winver -- [Windows only] version number of the Python DLL\n"
          "_enablelegacywindowsfsencoding -- [Windows only]\n"
          "__stdin__ -- the original stdin; don't touch!\n"
          "__stdout__ -- the original stdout; don't touch!\n"
          "__stderr__ -- the original stderr; don't touch!\n"
          "__displayhook__ -- the original displayhook; don't touch!\n"
          "__excepthook__ -- the original excepthook; don't touch!\n"
          "\n"
          "Functions:\n"
          "\n"
          "displayhook() -- print an object to the screen, and save it in builtins._\n"
          "excepthook() -- print an exception and its traceback to sys.stderr\n"
          "exception() -- return the current thread's active exception\n"
          "exc_info() -- return information about the current thread's active exception\n"
          "exit() -- exit the interpreter by raising SystemExit\n"
          "getdlopenflags() -- returns flags to be used for dlopen() calls\n"
          "getprofile() -- get the global profiling function\n"
          "getrefcount() -- return the reference count for an object (plus one :-)\n"
          "getrecursionlimit() -- return the max recursion depth for the interpreter\n"
          "getsizeof() -- return the size of an object in bytes\n"
          "gettrace() -- get the global debug tracing function\n"
          "setdlopenflags() -- set the flags to be used for dlopen() calls\n"
          "setprofile() -- set the global profiling function\n"
          "setrecursionlimit() -- set the max recursion depth for the interpreter\n"
          "settrace() -- set the global debug tracing function\n"),
      error);
  module_set_attr(
      sys,
      "__interactivehook__",
      sys_metadata_native_function(
          runtime,
          "site",
          "site.register_readline",
          "register_readline",
          sys_noop_hook,
          const_cast<char*>("site.register_readline"),
          "Configure readline completion on interactive prompts.",
          sys_noop_unexpected_keyword),
      error);
  module_set_attr(
      sys,
      "_baserepl",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._baserepl",
          "_baserepl",
          sys_noop_hook,
          const_cast<char*>("sys._baserepl"),
          "Private function for getting the base REPL",
          sys_no_keyword_args),
      error);
  module_set_attr(sys, "argv", Value::list({}), error);
  module_set_attr(sys, "orig_argv", Value::list({Value::string(executable_path())}), error);
  module_set_attr(sys, "version_info", make_version_info(runtime), error);
  module_set_attr(sys, "version", Value::string("3.14.7 (XLang3)"), error);
  module_set_attr(sys, "hexversion", Value::int64(0x030e07f0), error);
  module_set_attr(sys, "api_version", Value::int64(1013), error);
#if !defined(_WIN32)
  module_set_attr(sys, "abiflags", Value::string(""), error);
#endif
  module_set_attr(sys, "_git", Value::tuple({Value::string("CPython"), Value::string("tags/v3.14.7"), Value::string("")}), error);
  module_set_attr(sys, "_vpath", Value::string(""), error);
  module_set_attr(sys, "_home", Value::none(), error);
  module_set_attr(sys, "float_repr_style", Value::string("short"), error);
  module_set_attr(sys, "platform", Value::string(
#if defined(_WIN32)
                                      "win32"
#elif defined(__APPLE__)
                                      "darwin"
#elif defined(__EMSCRIPTEN__)
                                      "emscripten"
#else
                                      "linux"
#endif
                                      ),
      error);
  module_set_attr(sys, "maxsize", Value::int64(std::numeric_limits<int64_t>::max()), error);
  module_set_attr(sys, "maxunicode", Value::int64(0x10ffff), error);
  module_set_attr(sys, "byteorder", Value::string("little"), error);
  module_set_attr(sys, "dont_write_bytecode", Value::boolean(false), error);
  module_set_attr(sys, "flags", make_flags(runtime), error);
  module_set_attr(sys, "int_info", make_int_info(runtime), error);
  module_set_attr(sys, "float_info", make_float_info(runtime), error);
  module_set_attr(sys, "hash_info", make_hash_info(runtime), error);
  module_set_attr(sys, "thread_info", make_thread_info(runtime), error);
  module_set_attr(sys, "warnoptions", Value::list({}), error);
  module_set_attr(sys, "_xoptions", Value::dict({}), error);
  module_set_attr(sys, "meta_path", Value::list({}), error);
  module_set_attr(sys, "path_hooks", Value::list({}), error);
  module_set_attr(sys, "path_importer_cache", Value::dict({}), error);
  module_set_attr(sys, "builtin_module_names", make_builtin_module_names(), error);
  module_set_attr(sys, "stdlib_module_names", make_stdlib_module_names(), error);
  module_set_attr(sys, "modules", modules_ref, error);
  Value stdin_stream = make_sys_stdio(runtime, "_XLang3Stdin", "stdin");
  Value stdout_stream = make_sys_stdio(runtime, "_XLang3Stdout", "stdout");
  Value stderr_stream = make_sys_stdio(runtime, "_XLang3Stderr", "stderr");
  module_set_attr(sys, "stdin", stdin_stream, error);
  module_set_attr(sys, "stdout", stdout_stream, error);
  module_set_attr(sys, "stderr", stderr_stream, error);
  module_set_attr(sys, "__stdin__", stdin_stream, error);
  module_set_attr(sys, "__stdout__", stdout_stream, error);
  module_set_attr(sys, "__stderr__", stderr_stream, error);
  module_set_attr(sys, "implementation",
                  Value::instance(Value::class_object("SimpleNamespace", {{"__module__", Value::string("types")}})),
                  error);
  Value implementation;
  module_get_attr(sys, "implementation", implementation, error);
  object_set_attr(implementation, "name", Value::string("xlang3"), error);
  object_set_attr(implementation, "version", make_version_info(runtime), error);
  object_set_attr(implementation, "cache_tag", Value::string("xlang3-314"), error);
  object_set_attr(implementation, "hexversion", Value::int64(0x030e07f0), error);
#if !defined(_WIN32)
  object_set_attr(implementation, "_multiarch", Value::string(""), error);
#endif
  object_set_attr(implementation, "supports_isolated_interpreters", Value::boolean(false), error);
  Value implementation_version;
  object_get_attr(implementation, "version", implementation_version, error);
  std::string implementation_repr =
      "namespace(name='xlang3', cache_tag='xlang3-314', version=" +
      value_to_string(implementation_version) +
      ", hexversion=51251184, supports_isolated_interpreters=False";
#if !defined(_WIN32)
  implementation_repr += ", _multiarch=''";
#endif
  implementation_repr += ")";
  object_set_attr(implementation, "__xlang3_string_value__", Value::string(std::move(implementation_repr)), error);
  const std::string exe = executable_path();
  const std::string prefix = runtime_prefix(runtime);
  const std::string stdlib_dir = runtime_stdlib_dir(runtime);
  module_set_attr(sys, "executable", Value::string(exe), error);
  module_set_attr(sys, "_base_executable", Value::string(exe), error);
  module_set_attr(sys, "prefix", Value::string(prefix), error);
  module_set_attr(sys, "base_prefix", Value::string(prefix), error);
  module_set_attr(sys, "exec_prefix", Value::string(prefix), error);
  module_set_attr(sys, "base_exec_prefix", Value::string(prefix), error);
  module_set_attr(sys, "_stdlib_dir", Value::string(stdlib_dir), error);
  module_set_attr(sys, "_framework", Value::string(""), error);
  module_set_attr(sys, "platlibdir", Value::string(
#if defined(_WIN32)
                                      "DLLs"
#else
                                      "lib"
#endif
                                      ),
      error);
  module_set_attr(sys, "pycache_prefix", Value::none(), error);
  module_set_attr(sys, "copyright", Value::string("Copyright (C) 2026 CantorAI Inc. and The XLang Foundation"), error);
  module_set_attr(
      sys,
      "exc_info",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.exc_info",
          "exc_info",
          sys_exc_info,
          const_cast<char*>("sys.exc_info"),
          "Return current exception information: (type, value, traceback).\n\n"
          "Return information about the most recent exception caught by an except\n"
          "clause in the current stack frame or in an older stack frame.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "exception",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.exception",
          "exception",
          sys_exception,
          const_cast<char*>("sys.exception"),
          "Return the current exception.\n\n"
          "Return the most recent exception caught by an except clause\n"
          "in the current stack frame or in an older stack frame, or None\n"
          "if no such exception exists.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "exit",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.exit",
          "exit",
          sys_exit,
          nullptr,
          "Exit the interpreter by raising SystemExit(status).\n\n"
          "If the status is omitted or None, it defaults to zero (i.e., success).\n"
          "If the status is an integer, it will be used as the system exit status.\n"
          "If it is another kind of object, it will be printed and the system\n"
          "exit status will be one (i.e., failure).",
          sys_exit_kw),
      error);
  const Value displayhook = sys_metadata_native_function(
      runtime,
      "sys",
      "sys.displayhook",
      "displayhook",
      sys_displayhook,
      const_cast<char*>("sys.displayhook"),
      "Print an object to sys.stdout and also save it in builtins._",
      sys_no_keyword_args);
  module_set_attr(sys, "displayhook", displayhook, error);
  module_set_attr(sys, "__displayhook__", displayhook, error);
  const Value excepthook = sys_metadata_native_function(
      runtime,
      "sys",
      "sys.excepthook",
      "excepthook",
      sys_excepthook,
      const_cast<char*>("sys.excepthook"),
      "Handle an exception by displaying it with a traceback on sys.stderr.",
      sys_no_keyword_args);
  module_set_attr(sys, "excepthook", excepthook, error);
  module_set_attr(sys, "__excepthook__", excepthook, error);
  const Value unraisablehook = sys_metadata_native_function(
      runtime,
      "sys",
      "sys.unraisablehook",
      "unraisablehook",
      sys_unraisablehook,
      const_cast<char*>("sys.unraisablehook"),
      "Handle an unraisable exception.\n\n"
      "The unraisable argument has the following attributes:\n\n"
      "* exc_type: Exception type.\n"
      "* exc_value: Exception value, can be None.\n"
      "* exc_traceback: Exception traceback, can be None.\n"
      "* err_msg: Error message, can be None.\n"
      "* object: Object causing the exception, can be None.",
      sys_no_keyword_args);
  module_set_attr(sys, "unraisablehook", unraisablehook, error);
  module_set_attr(sys, "__unraisablehook__", unraisablehook, error);
  const Value breakpointhook = sys_metadata_native_function(
      runtime,
      "sys",
      "sys.breakpointhook",
      "breakpointhook",
      sys_breakpointhook,
      nullptr,
      "This hook function is called by built-in breakpoint().\n",
      sys_breakpointhook_kw);
  module_set_attr(sys, "breakpointhook", breakpointhook, error);
  module_set_attr(sys, "__breakpointhook__", breakpointhook, error);
  module_set_attr(
      sys,
      "addaudithook",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.addaudithook",
          "addaudithook",
          sys_addaudithook,
          nullptr,
          "Adds a new audit hook callback.",
          sys_addaudithook_kw),
      error);
  module_set_attr(
      sys,
      "audit",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.audit",
          "audit",
          sys_audit,
          nullptr,
          "Passes the event to any audit hooks that are attached.",
          sys_audit_kw),
      error);
  module_set_attr(
      sys,
      "getdefaultencoding",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getdefaultencoding",
          "getdefaultencoding",
          sys_getdefaultencoding,
          const_cast<char*>("sys.getdefaultencoding"),
          "Return the current default encoding used by the Unicode implementation.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "getfilesystemencoding",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getfilesystemencoding",
          "getfilesystemencoding",
          sys_getfilesystemencoding,
          const_cast<char*>("sys.getfilesystemencoding"),
          "Return the encoding used to convert Unicode filenames to OS filenames.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "getfilesystemencodeerrors",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getfilesystemencodeerrors",
          "getfilesystemencodeerrors",
          sys_getfilesystemencodeerrors,
          const_cast<char*>("sys.getfilesystemencodeerrors"),
          "Return the error mode used Unicode to OS filename conversion.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "getrecursionlimit",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getrecursionlimit",
          "getrecursionlimit",
          sys_getrecursionlimit,
          const_cast<char*>("sys.getrecursionlimit"),
          "Return the current value of the recursion limit.\n\n"
          "The recursion limit is the maximum depth of the Python interpreter\n"
          "stack.  This limit prevents infinite recursion from causing an overflow\n"
          "of the C stack and crashing Python.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "setrecursionlimit",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.setrecursionlimit",
          "setrecursionlimit",
          sys_setrecursionlimit,
          const_cast<char*>("sys.setrecursionlimit"),
          "Set the maximum depth of the Python interpreter stack to n.\n\n"
          "This limit prevents infinite recursion from causing an overflow of the C\n"
          "stack and crashing Python.  The highest possible limit is platform-\n"
          "dependent.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "intern",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.intern",
          "intern",
          sys_intern,
          nullptr,
          "``Intern'' the given string.\n\n"
          "This enters the string in the (global) table of interned strings whose\n"
          "purpose is to speed up dictionary lookups. Return the string itself or\n"
          "the previously interned string object with the same value.",
          sys_intern_kw),
      error);
  module_set_attr(
      sys,
      "_is_interned",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._is_interned",
          "_is_interned",
          sys_is_interned,
          nullptr,
          "Return True if the given string is \"interned\".",
          sys_is_interned_kw),
      error);
  module_set_attr(
      sys,
      "getunicodeinternedsize",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getunicodeinternedsize",
          "getunicodeinternedsize",
          sys_getunicodeinternedsize,
          nullptr,
          "Return the number of elements of the unicode interned dictionary",
          sys_getunicodeinternedsize_kw),
      error);
  module_set_attr(
      sys,
      "_is_immortal",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._is_immortal",
          "_is_immortal",
          sys_is_immortal,
          nullptr,
          "Return True if the given object is \"immortal\" per PEP 683.",
          sys_is_immortal_kw),
      error);
  module_set_attr(
      sys,
      "getsizeof",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getsizeof",
          "getsizeof",
          sys_getsizeof,
          nullptr,
          "getsizeof(object [, default]) -> int\n\n"
          "Return the size of object in bytes.",
          sys_getsizeof_kw),
      error);
  module_set_attr(
      sys,
      "getrefcount",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getrefcount",
          "getrefcount",
          sys_getrefcount,
          nullptr,
          "Return the reference count of object.\n\n"
          "The count returned is generally one higher than you might expect,\n"
          "because it includes the (temporary) reference as an argument to\n"
          "getrefcount().",
          sys_getrefcount_kw),
      error);
  module_set_attr(
      sys,
      "getallocatedblocks",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getallocatedblocks",
          "getallocatedblocks",
          sys_getallocatedblocks,
          nullptr,
          "Return the number of memory blocks currently allocated.",
          sys_getallocatedblocks_kw),
      error);
  module_set_attr(
      sys,
      "settrace",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.settrace",
          "settrace",
          sys_settrace,
          const_cast<char*>("sys.settrace"),
          "Set the global debug tracing function.\n\n"
          "It will be called on each function call.  See the debugger chapter\n"
          "in the library manual.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "gettrace",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.gettrace",
          "gettrace",
          sys_gettrace,
          const_cast<char*>("sys.gettrace"),
          "Return the global debug tracing function set with sys.settrace.\n\n"
          "See the debugger chapter in the library manual.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_settraceallthreads",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._settraceallthreads",
          "_settraceallthreads",
          sys_settraceallthreads,
          const_cast<char*>("sys._settraceallthreads"),
          "Set the global debug tracing function in all running threads belonging to the current interpreter.\n\n"
          "It will be called on each function call. See the debugger chapter\n"
          "in the library manual.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "call_tracing",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.call_tracing",
          "call_tracing",
          sys_call_tracing,
          const_cast<char*>("sys.call_tracing"),
          "Call func(*args), while tracing is enabled.\n\n"
          "The tracing state is saved, and restored afterwards.  This is intended\n"
          "to be called from a debugger from a checkpoint, to recursively debug\n"
          "some other code.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "setprofile",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.setprofile",
          "setprofile",
          sys_setprofile,
          const_cast<char*>("sys.setprofile"),
          "Set the profiling function.\n\n"
          "It will be called on each function call and return.  See the profiler\n"
          "chapter in the library manual.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "getprofile",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getprofile",
          "getprofile",
          sys_getprofile,
          const_cast<char*>("sys.getprofile"),
          "Return the profiling function set with sys.setprofile.\n\n"
          "See the profiler chapter in the library manual.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_setprofileallthreads",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._setprofileallthreads",
          "_setprofileallthreads",
          sys_setprofileallthreads,
          const_cast<char*>("sys._setprofileallthreads"),
          "Set the profiling function in all running threads belonging to the current interpreter.\n\n"
          "It will be called on each function call and return.  See the profiler\n"
          "chapter in the library manual.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "getswitchinterval",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getswitchinterval",
          "getswitchinterval",
          sys_getswitchinterval,
          const_cast<char*>("sys.getswitchinterval"),
          "Return the current thread switch interval; see sys.setswitchinterval().",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "setswitchinterval",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.setswitchinterval",
          "setswitchinterval",
          sys_setswitchinterval,
          const_cast<char*>("sys.setswitchinterval"),
          "Set the ideal thread switching delay inside the Python interpreter.\n\n"
          "The actual frequency of switching threads can be lower if the\n"
          "interpreter executes long sequences of uninterruptible code\n"
          "(this is implementation-specific and workload-dependent).\n\n"
          "The parameter must represent the desired switching delay in seconds\n"
          "A typical value is 0.005 (5 milliseconds).",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "get_int_max_str_digits",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.get_int_max_str_digits",
          "get_int_max_str_digits",
          sys_get_int_max_str_digits,
          const_cast<char*>("sys.get_int_max_str_digits"),
          "Return the maximum string digits limit for non-binary int<->str conversions.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "set_int_max_str_digits",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.set_int_max_str_digits",
          "set_int_max_str_digits",
          sys_set_int_max_str_digits,
          nullptr,
          "Set the maximum string digits limit for non-binary int<->str conversions.",
          sys_set_int_max_str_digits_kw),
      error);
  module_set_attr(
      sys,
      "is_finalizing",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.is_finalizing",
          "is_finalizing",
          sys_is_finalizing,
          const_cast<char*>("sys.is_finalizing"),
          "Return True if Python is exiting.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "is_remote_debug_enabled",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.is_remote_debug_enabled",
          "is_remote_debug_enabled",
          sys_is_remote_debug_enabled,
          const_cast<char*>("sys.is_remote_debug_enabled"),
          "Return True if remote debugging is enabled, False otherwise.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_is_gil_enabled",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._is_gil_enabled",
          "_is_gil_enabled",
          sys_is_gil_enabled,
          const_cast<char*>("sys._is_gil_enabled"),
          "Return True if the GIL is currently enabled and False otherwise.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "activate_stack_trampoline",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.activate_stack_trampoline",
          "activate_stack_trampoline",
          sys_activate_stack_trampoline,
          const_cast<char*>("sys.activate_stack_trampoline"),
          "Activate stack profiler trampoline *backend*.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "deactivate_stack_trampoline",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.deactivate_stack_trampoline",
          "deactivate_stack_trampoline",
          sys_deactivate_stack_trampoline,
          const_cast<char*>("sys.deactivate_stack_trampoline"),
          "Deactivate the current stack profiler trampoline backend.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "is_stack_trampoline_active",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.is_stack_trampoline_active",
          "is_stack_trampoline_active",
          sys_is_stack_trampoline_active,
          const_cast<char*>("sys.is_stack_trampoline_active"),
          "Return *True* if a stack profiler trampoline is active.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_debugmallocstats",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._debugmallocstats",
          "_debugmallocstats",
          sys_debugmallocstats,
          const_cast<char*>("sys._debugmallocstats"),
          "Print summary info to stderr about the state of pymalloc's structures.\n\n"
          "In Py_DEBUG mode, also perform some expensive internal consistency\n"
          "checks.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_dump_tracelets",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._dump_tracelets",
          "_dump_tracelets",
          sys_dump_tracelets,
          nullptr,
          "Dump the graph of tracelets in graphviz format",
          sys_dump_tracelets_kw),
      error);
  NativeModuleBuilder jit_builder(runtime, "sys._jit");
  jit_builder.value("__doc__", Value::string("Utilities for observing just-in-time compilation."))
      .value("is_available",
             sys_metadata_native_function(
                 runtime,
                 "sys._jit",
                 "sys._jit.is_available",
                 "is_available",
                 sys_jit_is_available,
                 const_cast<char*>("sys._jit.is_available"),
                 "Return True if the current Python executable supports JIT compilation, and False otherwise.",
                 sys_no_keyword_args))
      .value("is_enabled",
             sys_metadata_native_function(
                 runtime,
                 "sys._jit",
                 "sys._jit.is_enabled",
                 "is_enabled",
                 sys_jit_is_enabled,
                 const_cast<char*>("sys._jit.is_enabled"),
                 "Return True if JIT compilation is enabled for the current Python process (implies sys._jit.is_available()), and False otherwise.",
                 sys_no_keyword_args))
      .value("is_active",
             sys_metadata_native_function(
                 runtime,
                 "sys._jit",
                 "sys._jit.is_active",
                 "is_active",
                 sys_jit_is_active,
                 const_cast<char*>("sys._jit.is_active"),
                 "Return True if the topmost Python frame is currently executing JIT code (implies sys._jit.is_enabled()), and False otherwise.",
                 sys_no_keyword_args));
  Value jit_module = jit_builder.finish();
  module_set_attr(sys, "_jit", jit_module, error);
  runtime.register_module("sys._jit", jit_module);
  NativeModuleBuilder monitoring_builder(runtime, "sys.monitoring");
  monitoring_builder.value("DEBUGGER_ID", Value::int64(0))
      .value("COVERAGE_ID", Value::int64(1))
      .value("PROFILER_ID", Value::int64(2))
      .value("OPTIMIZER_ID", Value::int64(5))
      .value("MISSING", Value::instance(Value::class_object("object", {})))
      .value("DISABLE", Value::instance(Value::class_object("object", {})))
      .value("events", make_monitoring_events())
      .value("use_tool_id",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.use_tool_id", "use_tool_id", sys_monitoring_use_tool_id, const_cast<char*>("sys.monitoring.use_tool_id"), sys_no_keyword_args))
      .value("free_tool_id",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.free_tool_id", "free_tool_id", sys_monitoring_free_tool_id, const_cast<char*>("sys.monitoring.free_tool_id"), sys_no_keyword_args))
      .value("clear_tool_id",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.clear_tool_id", "clear_tool_id", sys_monitoring_clear_tool_id, const_cast<char*>("sys.monitoring.clear_tool_id"), sys_no_keyword_args))
      .value("get_tool",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.get_tool", "get_tool", sys_monitoring_get_tool, const_cast<char*>("sys.monitoring.get_tool"), sys_no_keyword_args))
      .value("set_events",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.set_events", "set_events", sys_monitoring_set_events, const_cast<char*>("sys.monitoring.set_events"), sys_no_keyword_args))
      .value("get_events",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.get_events", "get_events", sys_monitoring_get_events, const_cast<char*>("sys.monitoring.get_events"), sys_no_keyword_args))
      .value("set_local_events",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.set_local_events", "set_local_events", sys_monitoring_set_local_events, const_cast<char*>("sys.monitoring.set_local_events"), sys_no_keyword_args))
      .value("get_local_events",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.get_local_events", "get_local_events", sys_monitoring_get_local_events, const_cast<char*>("sys.monitoring.get_local_events"), sys_no_keyword_args))
      .value("register_callback",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.register_callback", "register_callback", sys_monitoring_register_callback, const_cast<char*>("sys.monitoring.register_callback"), sys_no_keyword_args))
      .value("restart_events",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring.restart_events", "restart_events", sys_monitoring_restart_events, const_cast<char*>("sys.monitoring.restart_events"), sys_no_keyword_args))
      .value("_all_events",
             sys_metadata_native_function_no_doc(
                 runtime, "sys.monitoring", "sys.monitoring._all_events", "_all_events", sys_monitoring_all_events, const_cast<char*>("sys.monitoring._all_events"), sys_no_keyword_args));
  Value monitoring_module = monitoring_builder.finish();
  module_set_attr(sys, "monitoring", monitoring_module, error);
  runtime.register_module("sys.monitoring", monitoring_module);
#if defined(_WIN32)
  module_set_attr(sys, "winver", Value::string("3.14"), error);
  module_set_attr(sys, "dllhandle", Value::int64(reinterpret_cast<int64_t>(GetModuleHandleW(nullptr))), error);
  module_set_attr(
      sys,
      "getwindowsversion",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.getwindowsversion",
          "getwindowsversion",
          sys_getwindowsversion,
          const_cast<char*>("sys.getwindowsversion"),
          "Return info about the running version of Windows as a named tuple.\n\n"
          "The members are named: major, minor, build, platform, service_pack,\n"
          "service_pack_major, service_pack_minor, suite_mask, product_type and\n"
          "platform_version. For backward compatibility, only the first 5 items\n"
          "are available by indexing. All elements are numbers, except\n"
          "service_pack and platform_type which are strings, and platform_version\n"
          "which is a 3-tuple. Platform is always 2. Product_type may be 1 for a\n"
          "workstation, 2 for a domain controller, 3 for a server.\n"
          "Platform_version is a 3-tuple containing a version number that is\n"
          "intended for identifying the OS rather than feature detection.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_enablelegacywindowsfsencoding",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._enablelegacywindowsfsencoding",
          "_enablelegacywindowsfsencoding",
          sys_enablelegacywindowsfsencoding,
          const_cast<char*>("sys._enablelegacywindowsfsencoding"),
          "Changes the default filesystem encoding to mbcs:replace.",
          sys_no_keyword_args),
      error);
#endif
  module_set_attr(
      sys,
      "_getframe",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._getframe",
          "_getframe",
          sys_getframe,
          const_cast<char*>("sys._getframe"),
          "Return a frame object from the call stack.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_getframemodulename",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._getframemodulename",
          "_getframemodulename",
          sys_getframemodulename,
          nullptr,
          "Return the name of the module for a calling frame.",
          sys_getframemodulename_kw),
      error);
  module_set_attr(
      sys,
      "_current_frames",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._current_frames",
          "_current_frames",
          sys_current_frames,
          const_cast<char*>("sys._current_frames"),
          "Return a dict mapping each thread's thread id to its current stack frame.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_current_exceptions",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._current_exceptions",
          "_current_exceptions",
          sys_current_exceptions,
          const_cast<char*>("sys._current_exceptions"),
          "Return a dict mapping each thread's identifier to its current raised exception.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_get_cpu_count_config",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._get_cpu_count_config",
          "_get_cpu_count_config",
          sys_get_cpu_count_config,
          const_cast<char*>("sys._get_cpu_count_config"),
          "Private function for getting PyConfig.cpu_count",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_clear_internal_caches",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._clear_internal_caches",
          "_clear_internal_caches",
          sys_clear_internal_caches,
          const_cast<char*>("sys._clear_internal_caches"),
          "Clear all internal performance-related caches.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_clear_type_cache",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._clear_type_cache",
          "_clear_type_cache",
          sys_clear_type_cache,
          const_cast<char*>("sys._clear_type_cache"),
          "Clear the internal type lookup cache.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "_clear_type_descriptors",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys._clear_type_descriptors",
          "_clear_type_descriptors",
          sys_clear_type_descriptors,
          nullptr,
          "Private function for clearing certain descriptors from a type's dictionary.",
          sys_clear_type_descriptors_kw),
      error);
  module_set_attr(
      sys,
      "get_coroutine_origin_tracking_depth",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.get_coroutine_origin_tracking_depth",
          "get_coroutine_origin_tracking_depth",
          sys_get_coroutine_origin_tracking_depth,
          const_cast<char*>("sys.get_coroutine_origin_tracking_depth"),
          "Check status of origin tracking for coroutine objects in this thread.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "set_coroutine_origin_tracking_depth",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.set_coroutine_origin_tracking_depth",
          "set_coroutine_origin_tracking_depth",
          sys_set_coroutine_origin_tracking_depth,
          nullptr,
          "Enable or disable origin tracking for coroutine objects in this thread.",
          sys_set_coroutine_origin_tracking_depth_kw),
      error);
  module_set_attr(
      sys,
      "get_asyncgen_hooks",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.get_asyncgen_hooks",
          "get_asyncgen_hooks",
          sys_get_asyncgen_hooks,
          const_cast<char*>("sys.get_asyncgen_hooks"),
          "Return the installed asynchronous generators hooks.",
          sys_no_keyword_args),
      error);
  module_set_attr(
      sys,
      "set_asyncgen_hooks",
      sys_metadata_native_function(
          runtime,
          "sys",
          "sys.set_asyncgen_hooks",
          "set_asyncgen_hooks",
          sys_set_asyncgen_hooks,
          nullptr,
          "set_asyncgen_hooks([firstiter] [, finalizer])",
          sys_set_asyncgen_hooks_kw),
      error);
  module_set_attr(sys, "_xlang3_debug_set_hook", runtime.make_native_function("sys._xlang3_debug_set_hook", sys_xlang3_debug_set_hook), error);
  module_set_attr(sys, "_xlang3_debug_add_breakpoint", runtime.make_native_function("sys._xlang3_debug_add_breakpoint", sys_xlang3_debug_add_breakpoint), error);
  module_set_attr(sys, "_xlang3_debug_clear_breakpoints", runtime.make_native_function("sys._xlang3_debug_clear_breakpoints", sys_xlang3_debug_clear_breakpoints), error);
  module_set_attr(sys, "_xlang3_debug_step_into", runtime.make_native_function("sys._xlang3_debug_step_into", sys_xlang3_debug_step_into), error);
  module_set_attr(sys, "_xlang3_debug_step_over", runtime.make_native_function("sys._xlang3_debug_step_over", sys_xlang3_debug_step_over), error);
  module_set_attr(sys, "_xlang3_debug_step_out", runtime.make_native_function("sys._xlang3_debug_step_out", sys_xlang3_debug_step_out), error);
  module_set_attr(sys, "_xlang3_debug_request_pause", runtime.make_native_function("sys._xlang3_debug_request_pause", sys_xlang3_debug_request_pause), error);
  module_set_attr(sys, "_xlang3_debug_continue", runtime.make_native_function("sys._xlang3_debug_continue", sys_xlang3_debug_continue), error);
  module_set_attr(sys, "_xlang3_debug_poll_needed", runtime.make_native_function("sys._xlang3_debug_poll_needed", sys_xlang3_debug_poll_needed), error);
  runtime.register_module("sys", std::move(sys));
}

} // namespace xlang3
