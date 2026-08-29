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

#include <cstdint>
#include <string>
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

bool winapi_int_arg(Runtime& runtime, const Value& value, int64_t& out, uint32_t position, const char* function_name, std::string& error) {
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
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

} // namespace

void register_winapi_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_winapi");
  builder.value("__doc__", Value::none())
#if defined(_WIN32)
      .value("CREATE_NEW_CONSOLE", Value::int64(CREATE_NEW_CONSOLE))
      .value("CREATE_NEW_PROCESS_GROUP", Value::int64(CREATE_NEW_PROCESS_GROUP))
      .value("STARTF_USESTDHANDLES", Value::int64(STARTF_USESTDHANDLES))
      .value("SW_HIDE", Value::int64(SW_HIDE))
      .value("WAIT_OBJECT_0", Value::int64(WAIT_OBJECT_0))
      .value("INFINITE", Value::int64(INFINITE))
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
              "          progress_routine=None)"));
  runtime.register_module("_winapi", builder.finish());
}

} // namespace xlang3
