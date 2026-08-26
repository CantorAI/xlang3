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

#include "../thread/thread_objects.h"
#include "runtime/memory/x3_runtime_memory.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
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
Value g_profile_function = Value::none();
std::vector<Value> g_audit_hooks;
thread_local int64_t g_coroutine_origin_tracking_depth = 0;
std::string g_filesystem_encoding = "utf-8";
std::string g_filesystem_encode_errors = "surrogatepass";

std::vector<Value>& interned_strings() {
  static auto* table = new std::vector<Value>();
  return *table;
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

Value make_member_descriptor(const std::string& name) {
  Value descriptor = Value::instance(Value::class_object("member_descriptor", {}));
  std::string ignored;
  object_set_attr(descriptor, "__name__", Value::string(name), ignored);
  return descriptor;
}

Value make_structseq(
    const std::string& type_name,
    const std::vector<std::pair<std::string, Value>>& fields,
    const std::string& module_name = "sys",
    size_t sequence_fields = std::numeric_limits<size_t>::max()) {
  std::vector<std::pair<std::string, Value>> class_attrs;
  class_attrs.push_back({"__module__", Value::string(module_name)});
  if (sequence_fields == std::numeric_limits<size_t>::max() || sequence_fields > fields.size()) {
    sequence_fields = fields.size();
  }
  class_attrs.push_back({"n_sequence_fields", Value::int64(static_cast<int64_t>(sequence_fields))});
  class_attrs.push_back({"n_fields", Value::int64(static_cast<int64_t>(fields.size()))});
  class_attrs.push_back({"n_unnamed_fields", Value::int64(0)});
  for (const auto& field : fields) {
    class_attrs.push_back({field.first, make_member_descriptor(field.first)});
  }
  Value instance = Value::instance(Value::class_object(type_name, std::move(class_attrs)));
  std::vector<Value> tuple_items;
  tuple_items.reserve(sequence_fields);
  std::string ignored;
  for (size_t i = 0; i < fields.size(); ++i) {
    const auto& field = fields[i];
    object_set_attr(instance, field.first, field.second, ignored);
    if (i < sequence_fields) {
      tuple_items.push_back(field.second);
    }
  }
  object_set_attr(instance, "n_sequence_fields", Value::int64(static_cast<int64_t>(sequence_fields)), ignored);
  object_set_attr(instance, "n_fields", Value::int64(static_cast<int64_t>(fields.size())), ignored);
  object_set_attr(instance, "n_unnamed_fields", Value::int64(0), ignored);
  object_set_attr(instance, "_tuple", Value::tuple(std::move(tuple_items)), ignored);
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

Value make_version_info() {
  return make_structseq(
      "version_info",
      {
          {"major", Value::int64(3)},
          {"minor", Value::int64(14)},
          {"micro", Value::int64(7)},
          {"releaselevel", Value::string("final")},
          {"serial", Value::int64(0)},
      });
}

Value make_flags() {
  return make_structseq(
      "flags",
      {
          {"debug", Value::int64(0)},
          {"inspect", Value::int64(0)},
          {"interactive", Value::int64(0)},
          {"optimize", Value::int64(0)},
          {"dont_write_bytecode", Value::int64(1)},
          {"no_user_site", Value::int64(0)},
          {"no_site", Value::int64(0)},
          {"ignore_environment", Value::int64(0)},
          {"verbose", Value::int64(0)},
          {"bytes_warning", Value::int64(0)},
          {"quiet", Value::int64(0)},
          {"hash_randomization", Value::int64(0)},
          {"isolated", Value::int64(0)},
          {"dev_mode", Value::boolean(false)},
          {"utf8_mode", Value::int64(1)},
          {"warn_default_encoding", Value::int64(0)},
          {"safe_path", Value::boolean(false)},
          {"int_max_str_digits", Value::int64(0)},
          {"gil", Value::int64(1)},
          {"thread_inherit_context", Value::int64(0)},
          {"context_aware_warnings", Value::int64(0)},
      },
      "sys",
      18);
}

Value make_float_info() {
  return make_structseq(
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

Value make_hash_info() {
  return make_structseq(
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

Value make_thread_info() {
  return make_structseq(
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

#if defined(_WIN32)
Value make_windows_version() {
  OSVERSIONINFOW version_info{};
  version_info.dwOSVersionInfoSize = sizeof(version_info);
  GetVersionExW(&version_info);
  const int64_t major = static_cast<int64_t>(version_info.dwMajorVersion);
  const int64_t minor = static_cast<int64_t>(version_info.dwMinorVersion);
  const int64_t build = static_cast<int64_t>(version_info.dwBuildNumber);
  return make_structseq(
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
      });
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
  return Value::set({
      Value::string("__future__"),
      Value::string("_abc"),
      Value::string("_ast"),
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
      Value::string("ast"),
      Value::string("atexit"),
      Value::string("builtins"),
      Value::string("code"),
      Value::string("codecs"),
      Value::string("collections"),
      Value::string("contextlib"),
      Value::string("ctypes"),
      Value::string("dataclasses"),
      Value::string("dis"),
      Value::string("enum"),
      Value::string("fnmatch"),
      Value::string("functools"),
      Value::string("getpass"),
      Value::string("glob"),
      Value::string("imp"),
      Value::string("importlib"),
      Value::string("inspect"),
      Value::string("io"),
      Value::string("itertools"),
      Value::string("json"),
      Value::string("linecache"),
      Value::string("locale"),
      Value::string("logging"),
      Value::string("marshal"),
      Value::string("math"),
      Value::string("numbers"),
      Value::string("opcode"),
      Value::string("operator"),
      Value::string("os"),
      Value::string("pathlib"),
      Value::string("pickle"),
      Value::string("pkgutil"),
      Value::string("platform"),
      Value::string("queue"),
      Value::string("re"),
      Value::string("runpy"),
      Value::string("select"),
      Value::string("signal"),
      Value::string("site"),
      Value::string("socket"),
      Value::string("stat"),
      Value::string("string"),
      Value::string("struct"),
      Value::string("subprocess"),
      Value::string("sys"),
      Value::string("sysconfig"),
      Value::string("threading"),
      Value::string("time"),
      Value::string("tokenize"),
      Value::string("traceback"),
      Value::string("types"),
      Value::string("unicodedata"),
      Value::string("urllib"),
      Value::string("warnings"),
      Value::string("weakref"),
      Value::string("winreg"),
      Value::string("xmlrpc"),
      Value::string("zipfile"),
      Value::string("zipimport"),
      Value::string("zlib"),
  });
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

bool sys_stdio_isatty(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "stdio.isatty() expected no arguments";
    return false;
  }
  value_set_bool(out, false);
  return true;
}

bool sys_stdio_readable(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "stdio.readable() expected no arguments";
    return false;
  }
  const char* kind = static_cast<const char*>(user_data);
  value_set_bool(out, std::string(kind) == "stdin");
  return true;
}

bool sys_stdio_writable(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "stdio.writable() expected no arguments";
    return false;
  }
  const char* kind = static_cast<const char*>(user_data);
  value_set_bool(out, std::string(kind) != "stdin");
  return true;
}

bool sys_stdio_seekable(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "stdio.seekable() expected no arguments";
    return false;
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
  attrs.push_back({"write", runtime.make_native_function(std::string("sys.") + kind + ".write", sys_stdio_write, const_cast<char*>(kind))});
  attrs.push_back({"read", runtime.make_native_function(std::string("sys.") + kind + ".read", sys_stdio_read, const_cast<char*>(kind))});
  attrs.push_back({"readline", runtime.make_native_function(std::string("sys.") + kind + ".readline", sys_stdio_readline, const_cast<char*>(kind))});
  attrs.push_back({"flush", runtime.make_native_function(std::string("sys.") + kind + ".flush", sys_stdio_flush, const_cast<char*>(kind))});
  attrs.push_back({"close", runtime.make_native_function(std::string("sys.") + kind + ".close", sys_stdio_close, const_cast<char*>(kind))});
  attrs.push_back({"isatty", runtime.make_native_function(std::string("sys.") + kind + ".isatty", sys_stdio_isatty, const_cast<char*>(kind))});
  attrs.push_back({"readable", runtime.make_native_function(std::string("sys.") + kind + ".readable", sys_stdio_readable, const_cast<char*>(kind))});
  attrs.push_back({"writable", runtime.make_native_function(std::string("sys.") + kind + ".writable", sys_stdio_writable, const_cast<char*>(kind))});
  attrs.push_back({"seekable", runtime.make_native_function(std::string("sys.") + kind + ".seekable", sys_stdio_seekable, const_cast<char*>(kind))});
  attrs.push_back({"fileno", runtime.make_native_function(std::string("sys.") + kind + ".fileno", sys_stdio_fileno, const_cast<char*>(kind))});
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
    error = "sys.exc_info expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
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
    error = "sys.exception expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
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
    error = "sys.settrace expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.set_trace_function(args[0]);
  value_set_none(out);
  return true;
}

bool sys_gettrace(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.gettrace expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (runtime.trace_function().tag == ValueTag::Invalid) {
    value_set_none(out);
  } else {
    value_assign_fast(out, runtime.trace_function());
  }
  return true;
}

bool sys_frame_at_depth(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, const char* name) {
  if (argc > 1) {
    error = std::string(name) + " expected at most 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t depth = 0;
  if (argc == 1) {
    if (args[0].tag != ValueTag::Int64) {
      error = std::string(name) + " depth must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    depth = args[0].as.i64;
    if (depth < 0) {
      error = "call stack is not deep enough";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  }
  out = runtime.current_frame_snapshot();
  for (int64_t i = 0; i < depth; ++i) {
    if (out.tag == ValueTag::None) {
      error = "call stack is not deep enough";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    Value back;
    if (!object_get_attr(out, "f_back", back, error) || back.tag == ValueTag::None) {
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
  if (!sys_frame_at_depth(runtime, args, argc, frame, error, "sys._getframemodulename")) {
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

bool sys_current_frames(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._current_frames expected 0 arguments";
    return false;
  }
  out = runtime.current_frame_snapshots(xlang_thread_active_idents());
  return true;
}

bool sys_current_exceptions(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._current_exceptions expected 0 arguments";
    return false;
  }
  out = runtime.current_exception_snapshots(xlang_thread_active_idents());
  return true;
}

bool sys_clear_internal_caches(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._clear_internal_caches expected 0 arguments";
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_clear_type_cache(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return sys_clear_internal_caches(runtime, args, argc, out, error, user_data);
}

bool sys_set_coroutine_origin_tracking_depth(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "sys.set_coroutine_origin_tracking_depth expected integer depth";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].as.i64 < 0) {
    error = "depth must be non-negative";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  g_coroutine_origin_tracking_depth = args[0].as.i64;
  value_set_none(out);
  return true;
}

bool sys_get_coroutine_origin_tracking_depth(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.get_coroutine_origin_tracking_depth expected 0 arguments";
    return false;
  }
  value_set_int64(out, g_coroutine_origin_tracking_depth);
  return true;
}

bool sys_getdefaultencoding(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.getdefaultencoding expected 0 arguments";
    return false;
  }
  out = Value::string("utf-8");
  return true;
}

bool sys_getfilesystemencoding(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.getfilesystemencoding expected 0 arguments";
    return false;
  }
  out = Value::string(g_filesystem_encoding);
  return true;
}

bool sys_getfilesystemencodeerrors(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.getfilesystemencodeerrors expected 0 arguments";
    return false;
  }
  out = Value::string(g_filesystem_encode_errors);
  return true;
}

bool sys_getrecursionlimit(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.getrecursionlimit expected 0 arguments";
    return false;
  }
  value_set_int64(out, g_recursion_limit);
  return true;
}

bool sys_setrecursionlimit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "sys.setrecursionlimit expected integer limit";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].as.i64 < 1 || args[0].as.i64 > std::numeric_limits<int>::max()) {
    error = "recursion limit must be positive";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  g_recursion_limit = static_cast<int>(args[0].as.i64);
  value_set_none(out);
  return true;
}

bool sys_intern(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "sys.intern expected string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string text = string_object_to_string(*value_as_string(args[0]));
  auto& interned = interned_strings();
  for (const auto& item : interned) {
    auto* interned_string = value_as_string(item);
    if (interned_string != nullptr && string_object_to_string(*interned_string) == text) {
      value_assign_fast(out, item);
      return true;
    }
  }
  interned.push_back(args[0]);
  value_assign_fast(out, args[0]);
  return true;
}

bool sys_is_interned(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "sys._is_interned expected string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (const auto& interned : interned_strings()) {
    if (value_is(interned, args[0])) {
      value_set_bool(out, true);
      return true;
    }
  }
  value_set_bool(out, false);
  return true;
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
  if (argc < 1 || argc > 2) {
    error = "sys.getsizeof expected object and optional default";
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
      return false;
    }
    if (size.tag != ValueTag::Int64) {
      error = "__sizeof__() should return int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_assign_fast(out, size);
    return true;
  }
  value_set_int64(out, shallow_sizeof(args[0]));
  return true;
}

bool sys_getrefcount(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.getrefcount expected object";
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

uint64_t live_block_count(const memory::X3MemoryCounter& counter) {
  return counter.alloc_count >= counter.free_count ? counter.alloc_count - counter.free_count : 0;
}

bool sys_getallocatedblocks(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.getallocatedblocks expected 0 arguments";
    return false;
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

bool sys_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "sys.exit expected optional status";
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

bool sys_displayhook(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.displayhook expected one argument";
    return false;
  }
  if (args[0].tag != ValueTag::None) {
    if (!sys_set_builtin_underscore(runtime, Value::none(), error) ||
        !sys_write_stream(runtime, "stdout", value_to_string(args[0]) + "\n", error) ||
        !sys_set_builtin_underscore(runtime, args[0], error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool sys_excepthook(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "sys.excepthook expected type, value, traceback";
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

bool sys_unraisablehook(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.unraisablehook expected one argument";
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_breakpointhook(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool sys_addaudithook(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.addaudithook expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!is_callable_value(args[0])) {
    error = "sys.addaudithook expected callable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  g_audit_hooks.push_back(args[0]);
  value_set_none(out);
  return true;
}

bool sys_audit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || value_as_string(args[0]) == nullptr) {
    error = "sys.audit expected event name string";
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

bool sys_setprofile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.setprofile expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(g_profile_function, args[0]);
  value_set_none(out);
  return true;
}

bool sys_getprofile(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.getprofile expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, g_profile_function);
  return true;
}

double g_switch_interval = 0.005;

bool sys_getswitchinterval(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.getswitchinterval expected 0 arguments";
    return false;
  }
  out = Value::number(g_switch_interval);
  return true;
}

bool sys_setswitchinterval(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || (args[0].tag != ValueTag::Int64 && args[0].tag != ValueTag::Double)) {
    error = "sys.setswitchinterval expected number";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const double value = args[0].tag == ValueTag::Int64 ? static_cast<double>(args[0].as.i64) : args[0].as.f64;
  if (value <= 0.0) {
    error = "switch interval must be strictly positive";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  g_switch_interval = value;
  value_set_none(out);
  return true;
}

bool sys_get_int_max_str_digits(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.get_int_max_str_digits expected 0 arguments";
    return false;
  }
  value_set_int64(out, 0);
  return true;
}

bool sys_set_int_max_str_digits(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "sys.set_int_max_str_digits expected integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].as.i64 < 0) {
    error = "maxdigits must be non-negative";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_is_finalizing(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.is_finalizing expected 0 arguments";
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool sys_is_gil_enabled(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._is_gil_enabled expected 0 arguments";
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool sys_activate_stack_trampoline(Runtime& runtime, const Value* args, uint32_t argc, Value&, std::string& error, void*) {
  StringObject* backend = argc == 1 ? value_as_string(args[0]) : nullptr;
  if (backend == nullptr) {
    error = "sys.activate_stack_trampoline expected backend name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  error = string_object_to_string(*backend) + " trampoline not available";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool sys_deactivate_stack_trampoline(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.deactivate_stack_trampoline expected 0 arguments";
    return false;
  }
  value_set_none(out);
  return true;
}

bool sys_is_stack_trampoline_active(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.is_stack_trampoline_active expected 0 arguments";
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool sys_jit_is_available(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._jit.is_available expected 0 arguments";
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool sys_jit_is_enabled(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._jit.is_enabled expected 0 arguments";
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool sys_jit_is_active(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._jit.is_active expected 0 arguments";
    return false;
  }
  out = Value::boolean(false);
  return true;
}

#if defined(_WIN32)
bool sys_getwindowsversion(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.getwindowsversion expected 0 arguments";
    return false;
  }
  out = make_windows_version();
  return true;
}

bool sys_enablelegacywindowsfsencoding(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._enablelegacywindowsfsencoding expected 0 arguments";
    return false;
  }
  g_filesystem_encoding = "mbcs";
  g_filesystem_encode_errors = "replace";
  value_set_none(out);
  return true;
}
#endif

bool sys_debugmallocstats(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._debugmallocstats expected 0 arguments";
    return false;
  }
  const auto& object_stats = memory::x3_thread_object_pools().stats();
  const auto& bucket_stats = memory::x3_thread_buckets().bucket_stats();
  const auto& large_stats = memory::x3_thread_buckets().large_stats();
  std::cerr << "XLang3 allocator stats\n"
            << "object_blocks=" << live_block_count(object_stats) << "\n"
            << "bucket_blocks=" << live_block_count(bucket_stats) << "\n"
            << "large_blocks=" << live_block_count(large_stats) << "\n";
  value_set_none(out);
  return true;
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
  module_set_attr(sys, "argv", Value::list({}), error);
  module_set_attr(sys, "orig_argv", Value::list({Value::string(executable_path())}), error);
  module_set_attr(sys, "version_info", make_version_info(), error);
  module_set_attr(sys, "version", Value::string("3.14.7 (XLang3)"), error);
  module_set_attr(sys, "hexversion", Value::int64(0x030e07f0), error);
  module_set_attr(sys, "api_version", Value::int64(1013), error);
  module_set_attr(sys, "abiflags", Value::string(""), error);
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
  module_set_attr(sys, "dont_write_bytecode", Value::boolean(true), error);
  module_set_attr(sys, "flags", make_flags(), error);
  module_set_attr(sys, "float_info", make_float_info(), error);
  module_set_attr(sys, "hash_info", make_hash_info(), error);
  module_set_attr(sys, "thread_info", make_thread_info(), error);
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
  module_set_attr(sys, "implementation", Value::instance(Value::class_object("SimpleNamespace", {})), error);
  Value implementation;
  module_get_attr(sys, "implementation", implementation, error);
  object_set_attr(implementation, "name", Value::string("xlang3"), error);
  object_set_attr(implementation, "version", make_version_info(), error);
  object_set_attr(implementation, "cache_tag", Value::string("xlang3-314"), error);
  object_set_attr(implementation, "hexversion", Value::int64(0x030e07f0), error);
  object_set_attr(implementation, "_multiarch", Value::string(""), error);
  object_set_attr(implementation, "supports_isolated_interpreters", Value::boolean(false), error);
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
  module_set_attr(sys, "exc_info", runtime.make_native_function("sys.exc_info", sys_exc_info), error);
  module_set_attr(sys, "exception", runtime.make_native_function("sys.exception", sys_exception), error);
  module_set_attr(sys, "exit", runtime.make_native_function("sys.exit", sys_exit), error);
  module_set_attr(sys, "displayhook", runtime.make_native_function("sys.displayhook", sys_displayhook), error);
  module_set_attr(sys, "__displayhook__", runtime.make_native_function("sys.__displayhook__", sys_displayhook), error);
  module_set_attr(sys, "excepthook", runtime.make_native_function("sys.excepthook", sys_excepthook), error);
  module_set_attr(sys, "__excepthook__", runtime.make_native_function("sys.__excepthook__", sys_excepthook), error);
  module_set_attr(sys, "unraisablehook", runtime.make_native_function("sys.unraisablehook", sys_unraisablehook), error);
  module_set_attr(sys, "__unraisablehook__", runtime.make_native_function("sys.__unraisablehook__", sys_unraisablehook), error);
  module_set_attr(sys, "breakpointhook", runtime.make_native_function("sys.breakpointhook", sys_breakpointhook), error);
  module_set_attr(sys, "__breakpointhook__", runtime.make_native_function("sys.__breakpointhook__", sys_breakpointhook), error);
  module_set_attr(sys, "addaudithook", runtime.make_native_function("sys.addaudithook", sys_addaudithook), error);
  module_set_attr(sys, "audit", runtime.make_native_function("sys.audit", sys_audit), error);
  module_set_attr(sys, "getdefaultencoding", runtime.make_native_function("sys.getdefaultencoding", sys_getdefaultencoding), error);
  module_set_attr(sys, "getfilesystemencoding", runtime.make_native_function("sys.getfilesystemencoding", sys_getfilesystemencoding), error);
  module_set_attr(sys, "getfilesystemencodeerrors", runtime.make_native_function("sys.getfilesystemencodeerrors", sys_getfilesystemencodeerrors), error);
  module_set_attr(sys, "getrecursionlimit", runtime.make_native_function("sys.getrecursionlimit", sys_getrecursionlimit), error);
  module_set_attr(sys, "setrecursionlimit", runtime.make_native_function("sys.setrecursionlimit", sys_setrecursionlimit), error);
  module_set_attr(sys, "intern", runtime.make_native_function("sys.intern", sys_intern), error);
  module_set_attr(sys, "_is_interned", runtime.make_native_function("sys._is_interned", sys_is_interned), error);
  module_set_attr(sys, "getsizeof", runtime.make_native_function("sys.getsizeof", sys_getsizeof), error);
  module_set_attr(sys, "getrefcount", runtime.make_native_function("sys.getrefcount", sys_getrefcount), error);
  module_set_attr(sys, "getallocatedblocks", runtime.make_native_function("sys.getallocatedblocks", sys_getallocatedblocks), error);
  module_set_attr(sys, "settrace", runtime.make_native_function("sys.settrace", sys_settrace), error);
  module_set_attr(sys, "gettrace", runtime.make_native_function("sys.gettrace", sys_gettrace), error);
  module_set_attr(sys, "setprofile", runtime.make_native_function("sys.setprofile", sys_setprofile), error);
  module_set_attr(sys, "getprofile", runtime.make_native_function("sys.getprofile", sys_getprofile), error);
  module_set_attr(sys, "getswitchinterval", runtime.make_native_function("sys.getswitchinterval", sys_getswitchinterval), error);
  module_set_attr(sys, "setswitchinterval", runtime.make_native_function("sys.setswitchinterval", sys_setswitchinterval), error);
  module_set_attr(sys, "get_int_max_str_digits", runtime.make_native_function("sys.get_int_max_str_digits", sys_get_int_max_str_digits), error);
  module_set_attr(sys, "set_int_max_str_digits", runtime.make_native_function("sys.set_int_max_str_digits", sys_set_int_max_str_digits), error);
  module_set_attr(sys, "is_finalizing", runtime.make_native_function("sys.is_finalizing", sys_is_finalizing), error);
  module_set_attr(sys, "_is_gil_enabled", runtime.make_native_function("sys._is_gil_enabled", sys_is_gil_enabled), error);
  module_set_attr(sys, "activate_stack_trampoline", runtime.make_native_function("sys.activate_stack_trampoline", sys_activate_stack_trampoline), error);
  module_set_attr(sys, "deactivate_stack_trampoline", runtime.make_native_function("sys.deactivate_stack_trampoline", sys_deactivate_stack_trampoline), error);
  module_set_attr(sys, "is_stack_trampoline_active", runtime.make_native_function("sys.is_stack_trampoline_active", sys_is_stack_trampoline_active), error);
  module_set_attr(sys, "_debugmallocstats", runtime.make_native_function("sys._debugmallocstats", sys_debugmallocstats), error);
  NativeModuleBuilder jit_builder(runtime, "sys._jit");
  jit_builder.function("is_available", sys_jit_is_available)
      .function("is_enabled", sys_jit_is_enabled)
      .function("is_active", sys_jit_is_active);
  Value jit_module = jit_builder.finish();
  module_set_attr(sys, "_jit", jit_module, error);
  runtime.register_module("sys._jit", jit_module);
#if defined(_WIN32)
  module_set_attr(sys, "winver", Value::string("3.14"), error);
  module_set_attr(sys, "dllhandle", Value::int64(reinterpret_cast<int64_t>(GetModuleHandleW(nullptr))), error);
  module_set_attr(sys, "getwindowsversion", runtime.make_native_function("sys.getwindowsversion", sys_getwindowsversion), error);
  module_set_attr(sys, "_enablelegacywindowsfsencoding", runtime.make_native_function("sys._enablelegacywindowsfsencoding", sys_enablelegacywindowsfsencoding), error);
#endif
  module_set_attr(sys, "_getframe", runtime.make_native_function("sys._getframe", sys_getframe), error);
  module_set_attr(sys, "_getframemodulename", runtime.make_native_function("sys._getframemodulename", sys_getframemodulename), error);
  module_set_attr(sys, "_current_frames", runtime.make_native_function("sys._current_frames", sys_current_frames), error);
  module_set_attr(sys, "_current_exceptions", runtime.make_native_function("sys._current_exceptions", sys_current_exceptions), error);
  module_set_attr(sys, "_clear_internal_caches", runtime.make_native_function("sys._clear_internal_caches", sys_clear_internal_caches), error);
  module_set_attr(sys, "_clear_type_cache", runtime.make_native_function("sys._clear_type_cache", sys_clear_type_cache), error);
  module_set_attr(sys, "get_coroutine_origin_tracking_depth", runtime.make_native_function("sys.get_coroutine_origin_tracking_depth", sys_get_coroutine_origin_tracking_depth), error);
  module_set_attr(sys, "set_coroutine_origin_tracking_depth", runtime.make_native_function("sys.set_coroutine_origin_tracking_depth", sys_set_coroutine_origin_tracking_depth), error);
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
