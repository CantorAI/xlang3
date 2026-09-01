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

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xlang3 {

namespace {

Value winapi_native_function(
    Runtime& runtime,
    const std::string& qualified_name,
    const std::string& function_name,
    NativeFunctionCallback callback,
    const std::string& doc,
    NativeKeywordFunctionCallback keyword_callback = nullptr,
    const char* text_signature = nullptr) {
  Value function = runtime.make_native_function(qualified_name, callback, nullptr, nullptr, nullptr, false, keyword_callback);
  if (auto* native = value_as_native_function(function)) {
    std::vector<std::pair<Value, Value>> attrs = {
        {Value::string("__module__"), Value::string("_winapi")},
        {Value::string("__name__"), Value::string(function_name)},
        {Value::string("__qualname__"), Value::string(function_name)},
        {Value::string("__doc__"), doc.empty() ? Value::none() : Value::string(doc)},
    };
    if (text_signature != nullptr) {
      attrs.push_back({Value::string("__text_signature__"), Value::string(text_signature)});
    }
    native->attrs_dict = new Value(Value::dict(std::move(attrs)));
  }
  return function;
}

std::string winapi_type_name(const Value& value) {
  if (value.tag == ValueTag::Bool) {
    return "bool";
  }
  if (value.tag == ValueTag::Int64) {
    return "int";
  }
  if (value.tag == ValueTag::Double) {
    return "float";
  }
  if (value.tag == ValueTag::None) {
    return "NoneType";
  }
  if (value_as_string(value) != nullptr) {
    return "str";
  }
  if (value_as_bytes(value) != nullptr) {
    return "bytes";
  }
  return "object";
}

bool winapi_string_arg(Runtime& runtime, const Value& value, const char* function_name, std::string& out, std::string& error) {
  auto* string = value_as_string(value);
  if (string == nullptr) {
    error = std::string(function_name) + "() argument must be str, not " + winapi_type_name(value);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = string_object_to_string(*string);
  return true;
}

bool winapi_int_value(const Value& value, int64_t& out) {
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

bool winapi_int_arg(Runtime& runtime, const Value& value, int64_t& out, uint32_t position, const char* function_name, std::string& error) {
  if (winapi_int_value(value, out)) {
    return true;
  }
  Value stored;
  std::string ignored;
  if (object_get_attr(value, "__xlang3_int_value__", stored, ignored) && winapi_int_value(stored, out)) {
    return true;
  }
  if (object_get_attr(value, "_value_", stored, ignored) && winapi_int_value(stored, out)) {
    return true;
  }
  error = std::string(function_name) + "() argument " + std::to_string(position) + " must be int, not " + winapi_type_name(value);
  runtime.raise_class_error("TypeError", error);
  return false;
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string& text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(size), L'\0');
  if (size > 0) {
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  }
  return wide;
}

bool raise_win32_error(Runtime& runtime, const char* operation, DWORD code, std::string& error) {
  error = std::string(operation) + " failed with Win32 error " + std::to_string(code);
  runtime.raise_class_error("OSError", error);
  return false;
}

bool winapi_optional_string_arg(
    Runtime& runtime,
    const Value& value,
    const char* function_name,
    std::string& out,
    bool& has_value,
    std::string& error) {
  if (value.tag == ValueTag::None) {
    out.clear();
    has_value = false;
    return true;
  }
  has_value = true;
  return winapi_string_arg(runtime, value, function_name, out, error);
}

bool winapi_startup_attr_handle(const Value& startupinfo, const char* name, HANDLE& out) {
  Value attr;
  std::string ignored;
  if (!object_get_attr(startupinfo, name, attr, ignored) || attr.tag == ValueTag::None) {
    return false;
  }
  int64_t value = 0;
  if (!winapi_int_value(attr, value)) {
    Value stored;
    if (object_get_attr(attr, "__xlang3_int_value__", stored, ignored) && winapi_int_value(stored, value)) {
      out = reinterpret_cast<HANDLE>(static_cast<intptr_t>(value));
      return true;
    }
    if (object_get_attr(attr, "_value_", stored, ignored) && winapi_int_value(stored, value)) {
      out = reinterpret_cast<HANDLE>(static_cast<intptr_t>(value));
      return true;
    }
    return false;
  }
  out = reinterpret_cast<HANDLE>(static_cast<intptr_t>(value));
  return true;
}

std::mutex g_pipe_handle_mutex;
std::unordered_set<intptr_t> g_pipe_handles;
std::unordered_set<intptr_t> g_auto_closed_pipe_handles;

intptr_t handle_key(HANDLE handle) {
  return reinterpret_cast<intptr_t>(handle);
}

bool take_pipe_handle_for_duplicate(HANDLE handle) {
  std::lock_guard<std::mutex> lock(g_pipe_handle_mutex);
  const intptr_t key = handle_key(handle);
  auto it = g_pipe_handles.find(key);
  if (it == g_pipe_handles.end()) {
    return false;
  }
  g_pipe_handles.erase(it);
  g_auto_closed_pipe_handles.insert(key);
  return true;
}

void remember_pipe_handle(HANDLE handle) {
  std::lock_guard<std::mutex> lock(g_pipe_handle_mutex);
  g_pipe_handles.insert(handle_key(handle));
}

bool forget_pipe_handle(HANDLE handle) {
  std::lock_guard<std::mutex> lock(g_pipe_handle_mutex);
  const intptr_t key = handle_key(handle);
  g_pipe_handles.erase(key);
  const auto auto_closed = g_auto_closed_pipe_handles.find(key);
  if (auto_closed != g_auto_closed_pipe_handles.end()) {
    g_auto_closed_pipe_handles.erase(auto_closed);
    return true;
  }
  return false;
}
#endif

bool winapi_need_current_directory_kw(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t kwargc, Value&, std::string& error, void*) {
  if (kwargc == 0) {
    error = "_winapi.NeedCurrentDirectoryForExePath() takes exactly one argument (0 given)";
  } else {
    error = "_winapi.NeedCurrentDirectoryForExePath() takes no keyword arguments";
  }
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool winapi_need_current_directory(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_winapi.NeedCurrentDirectoryForExePath() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string exe_name;
  if (!winapi_string_arg(runtime, args[0], "NeedCurrentDirectoryForExePath", exe_name, error)) {
    return false;
  }
#if defined(_WIN32)
  out = Value::boolean(NeedCurrentDirectoryForExePathW(utf8_to_wide(exe_name).c_str()) != FALSE);
#else
  out = Value::boolean(false);
#endif
  return true;
}

bool winapi_copy_file2(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "CopyFile2() missing required argument 'existing_file_name' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc < 2) {
    error = "CopyFile2() missing required argument 'new_file_name' (pos 2)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc < 3) {
    error = "CopyFile2() missing required argument 'flags' (pos 3)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 4) {
    error = "CopyFile2() takes at most 4 arguments (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string existing_file_name;
  std::string new_file_name;
  int64_t flags = 0;
  if (!winapi_string_arg(runtime, args[0], "CopyFile2", existing_file_name, error) ||
      !winapi_string_arg(runtime, args[1], "CopyFile2", new_file_name, error) ||
      !winapi_int_arg(runtime, args[2], flags, 3, "CopyFile2", error)) {
    return false;
  }
#if defined(_WIN32)
  COPYFILE2_EXTENDED_PARAMETERS params{};
  params.dwSize = sizeof(params);
  params.dwCopyFlags = static_cast<DWORD>(flags);
  const HRESULT hr = CopyFile2(utf8_to_wide(existing_file_name).c_str(), utf8_to_wide(new_file_name).c_str(), &params);
  if (FAILED(hr)) {
    return raise_win32_error(runtime, "CopyFile2", HRESULT_CODE(hr), error);
  }
#endif
  value_set_none(out);
  return true;
}

bool winapi_copy_file2_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  Value bound[4] = {Value::invalid(), Value::invalid(), Value::invalid(), Value::none()};
  const char* names[4] = {"existing_file_name", "new_file_name", "flags", "progress_routine"};
  if (argc > 4) {
    return winapi_copy_file2(runtime, args, argc, out, error, nullptr);
  }
  for (uint32_t i = 0; i < argc; ++i) {
    value_assign_fast(bound[i], args[i]);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    int slot = -1;
    for (int j = 0; j < 4; ++j) {
      if (name == names[j]) {
        slot = j;
        break;
      }
    }
    if (slot < 0) {
      error = "CopyFile2() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (bound[slot].tag != ValueTag::Invalid && slot < static_cast<int>(argc)) {
      error = "CopyFile2() got multiple values for argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_assign_fast(bound[slot], *kwargs[i].value);
  }
  uint32_t required = 0;
  while (required < 3 && bound[required].tag != ValueTag::Invalid) {
    ++required;
  }
  if (required < 3) {
    const char* missing = names[required];
    error = std::string("CopyFile2() missing required argument '") + missing + "' (pos " + std::to_string(required + 1) + ")";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return winapi_copy_file2(runtime, bound, bound[3].tag == ValueTag::Invalid ? 3 : 4, out, error, nullptr);
}

bool winapi_close_handle(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "CloseHandle() expected handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "CloseHandle", error)) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value));
  if (handle_value != 0 && !CloseHandle(handle)) {
    if (forget_pipe_handle(handle)) {
      value_set_none(out);
      return true;
    }
    // Some native dependency shims, such as _overlapped during early asyncio
    // bootstrap, expose XLang3-owned pseudo handles instead of OS handles.
    if (handle_value >= 0x10000 && GetLastError() == ERROR_INVALID_HANDLE) {
      value_set_none(out);
      return true;
    }
    return raise_win32_error(runtime, "CloseHandle", GetLastError(), error);
  }
  forget_pipe_handle(handle);
#endif
  value_set_none(out);
  return true;
}

bool winapi_get_current_process(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "GetCurrentProcess() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  out = Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(GetCurrentProcess())));
#else
  out = Value::int64(-1);
#endif
  return true;
}

bool winapi_get_std_handle(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "GetStdHandle() expected std handle id";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t std_id = 0;
  if (!winapi_int_arg(runtime, args[0], std_id, 1, "GetStdHandle", error)) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = GetStdHandle(static_cast<DWORD>(std_id));
  if (handle == INVALID_HANDLE_VALUE) {
    return raise_win32_error(runtime, "GetStdHandle", GetLastError(), error);
  }
  out = Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)));
#else
  out = Value::int64(std_id);
#endif
  return true;
}

bool winapi_get_file_type(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "GetFileType() expected handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "GetFileType", error)) {
    return false;
  }
#if defined(_WIN32)
  out = Value::int64(GetFileType(reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value))));
#else
  out = Value::int64(0);
#endif
  return true;
}

bool winapi_create_pipe(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "CreatePipe() expected security attributes and optional size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t size_value = 0;
  if (argc == 2 && !winapi_int_arg(runtime, args[1], size_value, 2, "CreatePipe", error)) {
    return false;
  }
#if defined(_WIN32)
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  SECURITY_ATTRIBUTES* security_ptr = args[0].tag == ValueTag::None ? nullptr : &security;
  HANDLE read_handle = nullptr;
  HANDLE write_handle = nullptr;
  if (!CreatePipe(&read_handle, &write_handle, security_ptr, static_cast<DWORD>(size_value))) {
    return raise_win32_error(runtime, "CreatePipe", GetLastError(), error);
  }
  remember_pipe_handle(read_handle);
  remember_pipe_handle(write_handle);
  out = Value::tuple({
      Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(read_handle))),
      Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(write_handle))),
  });
#else
  out = Value::tuple({Value::int64(-1), Value::int64(-1)});
#endif
  return true;
}

bool winapi_duplicate_handle(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 6) {
    error = "DuplicateHandle() expected 6 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t source_process = 0;
  int64_t source_handle = 0;
  int64_t target_process = 0;
  int64_t desired_access = 0;
  int64_t inherit_handle = 0;
  int64_t options = 0;
  if (!winapi_int_arg(runtime, args[0], source_process, 1, "DuplicateHandle", error) ||
      !winapi_int_arg(runtime, args[1], source_handle, 2, "DuplicateHandle", error) ||
      !winapi_int_arg(runtime, args[2], target_process, 3, "DuplicateHandle", error) ||
      !winapi_int_arg(runtime, args[3], desired_access, 4, "DuplicateHandle", error) ||
      !winapi_int_arg(runtime, args[4], inherit_handle, 5, "DuplicateHandle", error) ||
      !winapi_int_arg(runtime, args[5], options, 6, "DuplicateHandle", error)) {
    return false;
  }
#if defined(_WIN32)
  HANDLE target_handle = nullptr;
  const bool close_source_after_duplicate =
      source_process == static_cast<int64_t>(reinterpret_cast<intptr_t>(GetCurrentProcess())) &&
      target_process == static_cast<int64_t>(reinterpret_cast<intptr_t>(GetCurrentProcess())) &&
      inherit_handle != 0 &&
      take_pipe_handle_for_duplicate(reinterpret_cast<HANDLE>(static_cast<intptr_t>(source_handle)));
  DWORD duplicate_options = static_cast<DWORD>(options);
  if (close_source_after_duplicate) {
    duplicate_options |= DUPLICATE_CLOSE_SOURCE;
  }
  if (!DuplicateHandle(
          reinterpret_cast<HANDLE>(static_cast<intptr_t>(source_process)),
          reinterpret_cast<HANDLE>(static_cast<intptr_t>(source_handle)),
          reinterpret_cast<HANDLE>(static_cast<intptr_t>(target_process)),
          &target_handle,
          static_cast<DWORD>(desired_access),
          inherit_handle != 0,
          duplicate_options)) {
    return raise_win32_error(runtime, "DuplicateHandle", GetLastError(), error);
  }
  out = Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(target_handle)));
#else
  out = Value::int64(source_handle);
#endif
  return true;
}

bool winapi_create_process(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 9) {
    error = "CreateProcess() expected 9 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string application_name;
  std::string command_line;
  std::string current_directory;
  bool has_application_name = false;
  bool has_current_directory = false;
  int64_t inherit_handles = 0;
  int64_t creation_flags = 0;
  if (!winapi_optional_string_arg(runtime, args[0], "CreateProcess", application_name, has_application_name, error) ||
      !winapi_string_arg(runtime, args[1], "CreateProcess", command_line, error) ||
      !winapi_int_arg(runtime, args[4], inherit_handles, 5, "CreateProcess", error) ||
      !winapi_int_arg(runtime, args[5], creation_flags, 6, "CreateProcess", error) ||
      !winapi_optional_string_arg(runtime, args[7], "CreateProcess", current_directory, has_current_directory, error)) {
    return false;
  }
  if (args[6].tag != ValueTag::None) {
    error = "CreateProcess() env mappings are not implemented yet";
    runtime.raise_class_error("NotImplementedError", error);
    return false;
  }
#if defined(_WIN32)
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (args[8].tag != ValueTag::None) {
    Value value;
    std::string ignored;
    if (object_get_attr(args[8], "dwFlags", value, ignored)) {
      int64_t flags = 0;
      if (winapi_int_value(value, flags)) {
        startup.dwFlags = static_cast<DWORD>(flags);
      }
    }
    if (object_get_attr(args[8], "wShowWindow", value, ignored)) {
      int64_t show = 0;
      if (winapi_int_value(value, show)) {
        startup.wShowWindow = static_cast<WORD>(show);
      }
    }
    winapi_startup_attr_handle(args[8], "hStdInput", startup.hStdInput);
    winapi_startup_attr_handle(args[8], "hStdOutput", startup.hStdOutput);
    winapi_startup_attr_handle(args[8], "hStdError", startup.hStdError);
  }
  std::wstring app = utf8_to_wide(application_name);
  std::wstring cmd = utf8_to_wide(command_line);
  std::wstring cwd = utf8_to_wide(current_directory);
  if (!CreateProcessW(
          has_application_name ? app.c_str() : nullptr,
          cmd.empty() ? nullptr : cmd.data(),
          nullptr,
          nullptr,
          inherit_handles != 0,
          static_cast<DWORD>(creation_flags),
          nullptr,
          has_current_directory ? cwd.c_str() : nullptr,
          &startup,
          &process)) {
    return raise_win32_error(runtime, "CreateProcess", GetLastError(), error);
  }
  out = Value::tuple({
      Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(process.hProcess))),
      Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(process.hThread))),
      Value::int64(static_cast<int64_t>(process.dwProcessId)),
      Value::int64(static_cast<int64_t>(process.dwThreadId)),
  });
#else
  out = Value::tuple({Value::int64(-1), Value::int64(-1), Value::int64(0), Value::int64(0)});
#endif
  return true;
}

bool winapi_wait_for_single_object(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "WaitForSingleObject() expected handle and timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  int64_t timeout_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "WaitForSingleObject", error) ||
      !winapi_int_arg(runtime, args[1], timeout_value, 2, "WaitForSingleObject", error)) {
    return false;
  }
#if defined(_WIN32)
  out = Value::int64(WaitForSingleObject(
      reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)),
      static_cast<DWORD>(timeout_value)));
#else
  out = Value::int64(0);
#endif
  return true;
}

bool winapi_get_exit_code_process(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "GetExitCodeProcess() expected process handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "GetExitCodeProcess", error)) {
    return false;
  }
#if defined(_WIN32)
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)), &exit_code)) {
    return raise_win32_error(runtime, "GetExitCodeProcess", GetLastError(), error);
  }
  out = Value::int64(exit_code);
#else
  out = Value::int64(0);
#endif
  return true;
}

bool winapi_terminate_process(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "TerminateProcess() expected process handle and exit code";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  int64_t exit_code = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "TerminateProcess", error) ||
      !winapi_int_arg(runtime, args[1], exit_code, 2, "TerminateProcess", error)) {
    return false;
  }
#if defined(_WIN32)
  if (!TerminateProcess(reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)), static_cast<UINT>(exit_code))) {
    return raise_win32_error(runtime, "TerminateProcess", GetLastError(), error);
  }
#endif
  value_set_none(out);
  return true;
}

} // namespace

void register_winapi_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_winapi");
  builder.value("__doc__", Value::none())
#if defined(_WIN32)
      .value("NULL", Value::int64(0))
      .value("CREATE_NEW_CONSOLE", Value::int64(CREATE_NEW_CONSOLE))
      .value("CREATE_NEW_PROCESS_GROUP", Value::int64(CREATE_NEW_PROCESS_GROUP))
      .value("CREATE_NO_WINDOW", Value::int64(CREATE_NO_WINDOW))
      .value("DETACHED_PROCESS", Value::int64(DETACHED_PROCESS))
      .value("CREATE_DEFAULT_ERROR_MODE", Value::int64(CREATE_DEFAULT_ERROR_MODE))
      .value("CREATE_BREAKAWAY_FROM_JOB", Value::int64(CREATE_BREAKAWAY_FROM_JOB))
      .value("STD_INPUT_HANDLE", Value::int64(STD_INPUT_HANDLE))
      .value("STD_OUTPUT_HANDLE", Value::int64(STD_OUTPUT_HANDLE))
      .value("STD_ERROR_HANDLE", Value::int64(STD_ERROR_HANDLE))
      .value("STARTF_USESTDHANDLES", Value::int64(STARTF_USESTDHANDLES))
      .value("SW_HIDE", Value::int64(SW_HIDE))
      .value("STARTF_USESHOWWINDOW", Value::int64(STARTF_USESHOWWINDOW))
      .value("STARTF_FORCEONFEEDBACK", Value::int64(STARTF_FORCEONFEEDBACK))
      .value("STARTF_FORCEOFFFEEDBACK", Value::int64(STARTF_FORCEOFFFEEDBACK))
      .value("ABOVE_NORMAL_PRIORITY_CLASS", Value::int64(ABOVE_NORMAL_PRIORITY_CLASS))
      .value("BELOW_NORMAL_PRIORITY_CLASS", Value::int64(BELOW_NORMAL_PRIORITY_CLASS))
      .value("HIGH_PRIORITY_CLASS", Value::int64(HIGH_PRIORITY_CLASS))
      .value("IDLE_PRIORITY_CLASS", Value::int64(IDLE_PRIORITY_CLASS))
      .value("NORMAL_PRIORITY_CLASS", Value::int64(NORMAL_PRIORITY_CLASS))
      .value("REALTIME_PRIORITY_CLASS", Value::int64(REALTIME_PRIORITY_CLASS))
      .value("DUPLICATE_SAME_ACCESS", Value::int64(DUPLICATE_SAME_ACCESS))
      .value("FILE_TYPE_CHAR", Value::int64(FILE_TYPE_CHAR))
      .value("WAIT_OBJECT_0", Value::int64(WAIT_OBJECT_0))
      .value("WAIT_TIMEOUT", Value::int64(WAIT_TIMEOUT))
      .value("INFINITE", Value::int64(INFINITE))
      .value("STILL_ACTIVE", Value::int64(STILL_ACTIVE))
      .value("COPY_FILE_ALLOW_DECRYPTED_DESTINATION", Value::int64(COPY_FILE_ALLOW_DECRYPTED_DESTINATION))
      .value("COPY_FILE_COPY_SYMLINK", Value::int64(COPY_FILE_COPY_SYMLINK))
      .value("ERROR_PRIVILEGE_NOT_HELD", Value::int64(ERROR_PRIVILEGE_NOT_HELD))
      .value("ERROR_ACCESS_DENIED", Value::int64(ERROR_ACCESS_DENIED))
#endif
      .value(
          "NeedCurrentDirectoryForExePath",
          winapi_native_function(
              runtime,
              "_winapi.NeedCurrentDirectoryForExePath",
              "NeedCurrentDirectoryForExePath",
              winapi_need_current_directory,
              "",
              winapi_need_current_directory_kw,
              "($module, exe_name, /)"))
      .value(
          "CopyFile2",
          winapi_native_function(
              runtime,
              "_winapi.CopyFile2",
              "CopyFile2",
              winapi_copy_file2,
              "Copies a file from one name to a new name.\n\n"
              "This is implemented using the CopyFile2 API, which preserves all stat\n"
              "and metadata information apart from security attributes.\n\n"
              "progress_routine is reserved for future use, but is currently not\n"
              "implemented. Its value is ignored.",
              winapi_copy_file2_kw,
              "($module, /, existing_file_name, new_file_name, flags,\n"
              "          progress_routine=None)"))
      .function("CloseHandle", winapi_close_handle)
      .function("GetCurrentProcess", winapi_get_current_process)
      .function("GetStdHandle", winapi_get_std_handle)
      .function("GetFileType", winapi_get_file_type)
      .function("CreatePipe", winapi_create_pipe)
      .function("DuplicateHandle", winapi_duplicate_handle)
      .function("CreateProcess", winapi_create_process)
      .function("WaitForSingleObject", winapi_wait_for_single_object)
      .function("GetExitCodeProcess", winapi_get_exit_code_process)
      .function("TerminateProcess", winapi_terminate_process);
  runtime.register_module("_winapi", builder.finish());
}

} // namespace xlang3
