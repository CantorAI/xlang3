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

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include "../thread/runtime_lock.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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
#include <winioctl.h>
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

std::string wide_to_utf8(const std::wstring& text) {
  if (text.empty()) {
    return std::string();
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string utf8(static_cast<size_t>(size), '\0');
  if (size > 0) {
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
  }
  return utf8;
}

bool raise_win32_error(Runtime& runtime, const char* operation, DWORD code, std::string& error) {
  error = std::string(operation) + " failed with Win32 error " + std::to_string(code);
  Value exception = runtime.make_exception(
      code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
          ? "FileNotFoundError"
          : "OSError",
      error);
  std::string ignored;
  object_set_attr(exception, "winerror", Value::int64(static_cast<int64_t>(code)), ignored);
  object_set_attr(exception, "errno", Value::int64(static_cast<int64_t>(code)), ignored);
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool raise_win32_permission_error(Runtime& runtime, const char* operation, DWORD code, std::string& error) {
  error = std::string(operation) + " failed with Win32 error " + std::to_string(code);
  Value exception = runtime.make_exception("PermissionError", error);
  std::string ignored;
  object_set_attr(exception, "winerror", Value::int64(static_cast<int64_t>(code)), ignored);
  object_set_attr(exception, "errno", Value::int64(static_cast<int64_t>(code)), ignored);
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool winapi_env_text(Runtime& runtime, const Value& value, const char* field, std::wstring& out, std::string& error) {
  if (auto* text = value_as_string(value)) {
    out = utf8_to_wide(string_object_to_string(*text));
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out = utf8_to_wide(bytes_object_to_string(*bytes));
    return true;
  }
  error = std::string("CreateProcess() environment ") + field + " must be str or bytes, not " + winapi_type_name(value);
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool winapi_append_env_pair(
    Runtime& runtime,
    const Value& key,
    const Value& value,
    std::vector<std::wstring>& entries,
    std::string& error) {
  std::wstring key_text;
  std::wstring value_text;
  if (!winapi_env_text(runtime, key, "key", key_text, error) ||
      !winapi_env_text(runtime, value, "value", value_text, error)) {
    return false;
  }
  if (key_text.empty() || key_text.find(L'=') != std::wstring::npos || key_text.find(L'\0') != std::wstring::npos ||
      value_text.find(L'\0') != std::wstring::npos) {
    error = "CreateProcess() environment contains an invalid variable";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  entries.push_back(std::move(key_text) + L"=" + std::move(value_text));
  return true;
}

bool winapi_append_env_item(Runtime& runtime, const Value& item, std::vector<std::wstring>& entries, std::string& error) {
  if (auto* tuple = value_as_tuple(item)) {
    if (tuple->items.size() == 2) {
      return winapi_append_env_pair(runtime, tuple->items[0], tuple->items[1], entries, error);
    }
  } else if (auto* list = value_as_list(item)) {
    if (list->items.size() == 2) {
      return winapi_append_env_pair(runtime, list->items[0], list->items[1], entries, error);
    }
  }
  error = "CreateProcess() environment items must be key/value pairs";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool winapi_collect_env_entries(Runtime& runtime, const Value& env, std::vector<std::wstring>& entries, std::string& error) {
  if (const auto* dict = value_as_dict(env)) {
    entries.reserve(dict->entries.size());
    for (const auto& item : dict->entries) {
      if (!winapi_append_env_pair(runtime, item.first, item.second, entries, error)) {
        return false;
      }
    }
    return true;
  }

  Value items_method;
  std::string attr_error;
  if (!object_get_attr(env, "items", items_method, attr_error)) {
    error = "CreateProcess() environment must be a mapping";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value items_result;
  if (!runtime_call_callable(runtime, items_method, nullptr, 0, items_result, error)) {
    return false;
  }
  std::vector<Value> items;
  if (!runtime_collect_iterable(runtime, items_result, items, error)) {
    return false;
  }
  entries.reserve(items.size());
  for (const auto& item : items) {
    if (!winapi_append_env_item(runtime, item, entries, error)) {
      return false;
    }
  }
  return true;
}

std::wstring winapi_make_environment_block(std::vector<std::wstring> entries) {
  std::vector<std::wstring> deduplicated;
  std::unordered_map<std::wstring, size_t> positions;
  deduplicated.reserve(entries.size());
  for (auto& entry : entries) {
    const size_t equals = entry.find(L'=');
    std::wstring folded = entry.substr(0, equals);
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    auto found = positions.find(folded);
    if (found == positions.end()) {
      positions.emplace(std::move(folded), deduplicated.size());
      deduplicated.push_back(std::move(entry));
    } else {
      deduplicated[found->second] = std::move(entry);
    }
  }
  entries = std::move(deduplicated);
  std::sort(entries.begin(), entries.end(), [](const std::wstring& left, const std::wstring& right) {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](wchar_t a, wchar_t b) { return std::towlower(a) < std::towlower(b); });
  });
  std::wstring block;
  size_t total = 1;
  for (const auto& entry : entries) {
    total += entry.size() + 1;
  }
  block.reserve(total);
  for (const auto& entry : entries) {
    block.append(entry);
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
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

constexpr const char* kWinapiOverlappedNativeType = "_winapi.OverlappedResult";

struct WinapiOverlappedState {
  HANDLE event = nullptr;
  HANDLE handle = nullptr;
  OVERLAPPED overlapped{};
  DWORD transferred = 0;
  DWORD error = 0;
  std::string buffer;
  bool pending = false;
  bool is_read = false;
};

Value g_winapi_overlapped_class;

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

WinapiOverlappedState* winapi_overlapped_state(const Value& self, std::string& error) {
  auto* state = static_cast<WinapiOverlappedState*>(
      instance_get_native_data(self, kWinapiOverlappedNativeType));
  if (state == nullptr) {
    error = "invalid _winapi overlapped result";
  }
  return state;
}

void winapi_overlapped_cleanup(void* data) {
  auto* state = static_cast<WinapiOverlappedState*>(data);
  if (state != nullptr) {
    if (state->pending && state->handle != nullptr) {
      CancelIoEx(state->handle, &state->overlapped);
      DWORD transferred = 0;
      GetOverlappedResult(state->handle, &state->overlapped, &transferred, TRUE);
    }
    if (state->event != nullptr) {
      CloseHandle(state->event);
    }
  }
  delete state;
}

bool winapi_overlapped_get_result(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    error = "GetOverlappedResult() expected optional wait flag";
    return false;
  }
  auto* state = winapi_overlapped_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->pending) {
    const bool wait = argc < 2 || value_truthy(args[1]);
    DWORD transferred = 0;
    BOOL complete = FALSE;
    {
      XlangRuntimeExecutionSuspension suspension;
      complete = GetOverlappedResult(
          state->handle,
          &state->overlapped,
          &transferred,
          wait ? TRUE : FALSE);
    }
    state->transferred = transferred;
    state->error = complete ? ERROR_SUCCESS : GetLastError();
    if (state->error == ERROR_MORE_DATA && state->transferred == 0 &&
        state->is_read && !state->buffer.empty()) {
      const size_t internal_count = static_cast<size_t>(state->overlapped.InternalHigh);
      state->transferred = static_cast<DWORD>(
          internal_count == 0
              ? state->buffer.size()
              : std::min(internal_count, state->buffer.size()));
    }
    if (complete || state->error != ERROR_IO_INCOMPLETE) {
      state->pending = false;
      if (state->is_read && state->buffer.size() > state->transferred) {
        state->buffer.resize(static_cast<size_t>(state->transferred));
      }
    }
  }
  out = Value::tuple({
      Value::int64(static_cast<int64_t>(state->transferred)),
      Value::int64(static_cast<int64_t>(state->error)),
  });
  return true;
}

bool winapi_overlapped_get_buffer(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "getbuffer() expected no arguments";
    return false;
  }
  auto* state = winapi_overlapped_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::bytes(state->buffer);
  return true;
}

bool winapi_overlapped_cancel(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "cancel() expected no arguments";
    return false;
  }
  auto* state = winapi_overlapped_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  bool cancelled = false;
  if (state->pending && state->handle != nullptr) {
    BOOL result = FALSE;
    {
      XlangRuntimeExecutionSuspension suspension;
      result = CancelIoEx(state->handle, &state->overlapped);
    }
    cancelled = result != FALSE;
    if (!cancelled && GetLastError() != ERROR_NOT_FOUND) {
      return raise_win32_error(runtime, "CancelIoEx", GetLastError(), error);
    }
  }
  out = Value::boolean(cancelled);
  return true;
}

Value make_winapi_overlapped_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_winapi")});
  attrs.push_back({"GetOverlappedResult", runtime.make_native_function(
      "_winapi.OverlappedResult.GetOverlappedResult", winapi_overlapped_get_result)});
  attrs.push_back({"getbuffer", runtime.make_native_function(
      "_winapi.OverlappedResult.getbuffer", winapi_overlapped_get_buffer)});
  attrs.push_back({"cancel", runtime.make_native_function(
      "_winapi.OverlappedResult.cancel", winapi_overlapped_cancel)});
  return Value::class_object("OverlappedResult", std::move(attrs));
}

bool make_winapi_overlapped_result(
    DWORD transferred,
    DWORD code,
    std::string buffer,
    Value& out,
    std::string& error) {
  HANDLE event = CreateEventW(nullptr, TRUE, TRUE, nullptr);
  if (event == nullptr) {
    error = "CreateEvent failed with Win32 error " + std::to_string(GetLastError());
    return false;
  }
  Value result = Value::instance(g_winapi_overlapped_class);
  auto* state = new WinapiOverlappedState();
  state->event = event;
  state->overlapped.hEvent = event;
  state->transferred = transferred;
  state->error = code;
  state->buffer = std::move(buffer);
  if (!instance_set_native_data(
          result, kWinapiOverlappedNativeType, state, winapi_overlapped_cleanup, error)) {
    winapi_overlapped_cleanup(state);
    return false;
  }
  std::string ignored;
  object_set_attr(
      result,
      "event",
      Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(event))),
      ignored);
  value_assign_fast(out, result);
  return true;
}

bool make_pending_winapi_overlapped_result(
    std::unique_ptr<WinapiOverlappedState> state,
    Value& out,
    std::string& error) {
  Value result = Value::instance(g_winapi_overlapped_class);
  WinapiOverlappedState* raw_state = state.release();
  if (!instance_set_native_data(
          result,
          kWinapiOverlappedNativeType,
          raw_state,
          winapi_overlapped_cleanup,
          error)) {
    winapi_overlapped_cleanup(raw_state);
    return false;
  }
  std::string ignored;
  object_set_attr(
      result,
      "event",
      Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(raw_state->event))),
      ignored);
  value_assign_fast(out, result);
  return true;
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
    const DWORD code = HRESULT_CODE(hr);
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
      std::vector<uint8_t> contents;
      std::string vfs_error;
      if (runtime.vfs().read_file(existing_file_name, contents, vfs_error) &&
          runtime.vfs().write_file(
              new_file_name,
              contents.empty() ? nullptr : contents.data(),
              contents.size(),
              vfs_error)) {
        value_set_none(out);
        return true;
      }
    }
    return raise_win32_error(runtime, "CopyFile2", code, error);
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

bool winapi_create_junction(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "CreateJunction() expected source and destination paths";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string source_path;
  std::string destination_path;
  if (!winapi_string_arg(runtime, args[0], "CreateJunction", source_path, error) ||
      !winapi_string_arg(runtime, args[1], "CreateJunction", destination_path, error)) {
    return false;
  }
#if defined(_WIN32)
  // Python strings arrive here as UTF-8. Match the VFS conversion so junctions
  // work when the source or destination contains non-ASCII characters.
  const std::wstring source = std::filesystem::u8path(source_path).wstring();
  const std::wstring destination = std::filesystem::u8path(destination_path).wstring();
  if (source.rfind(L"\\??\\", 0) == 0) {
    return raise_win32_error(runtime, "CreateJunction", ERROR_INVALID_PARAMETER, error);
  }
  if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return raise_win32_error(runtime, "CreateJunction", GetLastError(), error);
  }

  const DWORD required = GetFullPathNameW(source.c_str(), 0, nullptr, nullptr);
  if (required == 0) {
    return raise_win32_error(runtime, "CreateJunction", GetLastError(), error);
  }
  std::vector<wchar_t> absolute_buffer(static_cast<size_t>(required));
  const DWORD written = GetFullPathNameW(
      source.c_str(), required, absolute_buffer.data(), nullptr);
  if (written == 0 || written >= required) {
    const DWORD code = written == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
    return raise_win32_error(runtime, "CreateJunction", code, error);
  }
  const std::wstring print_name(absolute_buffer.data(), static_cast<size_t>(written));
  const std::wstring substitute_name = L"\\??\\" + print_name;

  struct JunctionReparseData {
    ULONG reparse_tag;
    USHORT reparse_data_length;
    USHORT reserved;
    USHORT substitute_name_offset;
    USHORT substitute_name_length;
    USHORT print_name_offset;
    USHORT print_name_length;
    WCHAR path_buffer[1];
  };
  constexpr size_t reparse_header_size = 8;
  constexpr size_t path_buffer_offset = 16;
  const size_t character_count =
      substitute_name.size() + 1 + print_name.size() + 1;
  const size_t reparse_size =
      path_buffer_offset + character_count * sizeof(wchar_t);
  if (reparse_size - reparse_header_size > USHRT_MAX) {
    return raise_win32_error(runtime, "CreateJunction", ERROR_BUFFER_OVERFLOW, error);
  }
  std::vector<uint64_t> storage((reparse_size + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
  auto* reparse = reinterpret_cast<JunctionReparseData*>(storage.data());
  reparse->reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
  reparse->reparse_data_length =
      static_cast<USHORT>(reparse_size - reparse_header_size);
  reparse->substitute_name_offset = 0;
  reparse->substitute_name_length =
      static_cast<USHORT>(substitute_name.size() * sizeof(wchar_t));
  reparse->print_name_offset = static_cast<USHORT>(
      (substitute_name.size() + 1) * sizeof(wchar_t));
  reparse->print_name_length =
      static_cast<USHORT>(print_name.size() * sizeof(wchar_t));
  std::copy(substitute_name.begin(), substitute_name.end(), reparse->path_buffer);
  wchar_t* print_buffer = reparse->path_buffer + substitute_name.size() + 1;
  std::copy(print_name.begin(), print_name.end(), print_buffer);

  if (!CreateDirectoryW(destination.c_str(), nullptr)) {
    return raise_win32_error(runtime, "CreateJunction", GetLastError(), error);
  }
  HANDLE junction = CreateFileW(
      destination.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr);
  if (junction == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    RemoveDirectoryW(destination.c_str());
    return raise_win32_error(runtime, "CreateJunction", code, error);
  }
  DWORD returned = 0;
  const BOOL success = DeviceIoControl(
      junction,
      FSCTL_SET_REPARSE_POINT,
      reparse,
      static_cast<DWORD>(reparse_size),
      nullptr,
      0,
      &returned,
      nullptr);
  const DWORD code = success ? ERROR_SUCCESS : GetLastError();
  CloseHandle(junction);
  if (!success) {
    RemoveDirectoryW(destination.c_str());
    return raise_win32_error(runtime, "CreateJunction", code, error);
  }
#endif
  value_set_none(out);
  return true;
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

bool winapi_create_event(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "CreateEventW() expected security attributes, manual reset, initial state, and name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string name;
  bool has_name = false;
  if (!winapi_optional_string_arg(runtime, args[3], "CreateEventW", name, has_name, error)) {
    return false;
  }
#if defined(_WIN32)
  const std::wstring wide_name = has_name ? utf8_to_wide(name) : std::wstring();
  HANDLE event = CreateEventW(
      nullptr,
      value_truthy(args[1]) ? TRUE : FALSE,
      value_truthy(args[2]) ? TRUE : FALSE,
      has_name ? wide_name.c_str() : nullptr);
  if (event == nullptr) {
    return raise_win32_error(runtime, "CreateEventW", GetLastError(), error);
  }
  out = Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(event)));
#else
  out = Value::int64(-1);
#endif
  return true;
}

bool winapi_set_event(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SetEvent() expected handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "SetEvent", error)) {
    return false;
  }
#if defined(_WIN32)
  if (!SetEvent(reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)))) {
    return raise_win32_error(runtime, "SetEvent", GetLastError(), error);
  }
#endif
  value_set_none(out);
  return true;
}

bool winapi_reset_event(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ResetEvent() expected handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "ResetEvent", error)) {
    return false;
  }
#if defined(_WIN32)
  if (!ResetEvent(reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)))) {
    return raise_win32_error(runtime, "ResetEvent", GetLastError(), error);
  }
#endif
  value_set_none(out);
  return true;
}

bool winapi_path_name_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    bool long_name) {
  const char* function_name = long_name ? "GetLongPathName" : "GetShortPathName";
  if (argc != 1) {
    error = std::string(function_name) + "() expected path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string path;
  if (!winapi_string_arg(runtime, args[0], function_name, path, error)) {
    return false;
  }
#if defined(_WIN32)
  const std::wstring wide_path = utf8_to_wide(path);
  const auto path_function = long_name ? GetLongPathNameW : GetShortPathNameW;
  DWORD capacity = path_function(wide_path.c_str(), nullptr, 0);
  if (capacity == 0) {
    return raise_win32_error(runtime, function_name, GetLastError(), error);
  }
  std::vector<wchar_t> buffer(static_cast<size_t>(capacity) + 1, L'\0');
  const DWORD size = path_function(wide_path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size == 0) {
    return raise_win32_error(runtime, function_name, GetLastError(), error);
  }
  out = Value::string(wide_to_utf8(std::wstring(buffer.data(), static_cast<size_t>(size))));
#else
  out = Value::string(path);
#endif
  return true;
}

bool winapi_get_long_path_name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return winapi_path_name_impl(runtime, args, argc, out, error, true);
}

bool winapi_get_short_path_name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return winapi_path_name_impl(runtime, args, argc, out, error, false);
}

bool winapi_open_process(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "OpenProcess() expected access, inherit_handle, and process_id";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t access = 0;
  int64_t process_id = 0;
  if (!winapi_int_arg(runtime, args[0], access, 1, "OpenProcess", error) ||
      !winapi_int_arg(runtime, args[2], process_id, 3, "OpenProcess", error)) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = OpenProcess(
      static_cast<DWORD>(access), value_truthy(args[1]) ? TRUE : FALSE, static_cast<DWORD>(process_id));
  if (handle == nullptr) {
    return raise_win32_error(runtime, "OpenProcess", GetLastError(), error);
  }
  out = Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)));
#else
  out = Value::int64(-1);
#endif
  return true;
}

bool winapi_create_named_pipe(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 8) {
    error = "CreateNamedPipe() expected 8 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string name;
  int64_t open_mode = 0;
  int64_t pipe_mode = 0;
  int64_t max_instances = 0;
  int64_t out_size = 0;
  int64_t in_size = 0;
  int64_t timeout = 0;
  if (!winapi_string_arg(runtime, args[0], "CreateNamedPipe", name, error) ||
      !winapi_int_arg(runtime, args[1], open_mode, 2, "CreateNamedPipe", error) ||
      !winapi_int_arg(runtime, args[2], pipe_mode, 3, "CreateNamedPipe", error) ||
      !winapi_int_arg(runtime, args[3], max_instances, 4, "CreateNamedPipe", error) ||
      !winapi_int_arg(runtime, args[4], out_size, 5, "CreateNamedPipe", error) ||
      !winapi_int_arg(runtime, args[5], in_size, 6, "CreateNamedPipe", error) ||
      !winapi_int_arg(runtime, args[6], timeout, 7, "CreateNamedPipe", error)) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = CreateNamedPipeW(
      utf8_to_wide(name).c_str(),
      static_cast<DWORD>(open_mode),
      static_cast<DWORD>(pipe_mode),
      static_cast<DWORD>(max_instances),
      static_cast<DWORD>(out_size),
      static_cast<DWORD>(in_size),
      static_cast<DWORD>(timeout),
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return raise_win32_error(runtime, "CreateNamedPipe", GetLastError(), error);
  }
  out = Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)));
#else
  out = Value::int64(-1);
#endif
  return true;
}

bool winapi_create_file(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 7) {
    error = "CreateFile() expected 7 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string name;
  int64_t access = 0;
  int64_t share_mode = 0;
  int64_t disposition = 0;
  int64_t flags = 0;
  if (!winapi_string_arg(runtime, args[0], "CreateFile", name, error) ||
      !winapi_int_arg(runtime, args[1], access, 2, "CreateFile", error) ||
      !winapi_int_arg(runtime, args[2], share_mode, 3, "CreateFile", error) ||
      !winapi_int_arg(runtime, args[4], disposition, 5, "CreateFile", error) ||
      !winapi_int_arg(runtime, args[5], flags, 6, "CreateFile", error)) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = CreateFileW(
      utf8_to_wide(name).c_str(),
      static_cast<DWORD>(access),
      static_cast<DWORD>(share_mode),
      nullptr,
      static_cast<DWORD>(disposition),
      static_cast<DWORD>(flags),
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return raise_win32_error(runtime, "CreateFile", GetLastError(), error);
  }
  out = Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)));
#else
  out = Value::int64(-1);
#endif
  return true;
}

bool winapi_set_named_pipe_handle_state(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "SetNamedPipeHandleState() expected 4 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  int64_t mode_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "SetNamedPipeHandleState", error) ||
      !winapi_int_arg(runtime, args[1], mode_value, 2, "SetNamedPipeHandleState", error)) {
    return false;
  }
#if defined(_WIN32)
  DWORD mode = static_cast<DWORD>(mode_value);
  if (!SetNamedPipeHandleState(
          reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)),
          &mode,
          nullptr,
          nullptr)) {
    return raise_win32_error(runtime, "SetNamedPipeHandleState", GetLastError(), error);
  }
#endif
  value_set_none(out);
  return true;
}

bool winapi_connect_named_pipe_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    error = "ConnectNamedPipe() expected handle and optional overlapped flag";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "ConnectNamedPipe", error)) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value));
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (event == nullptr) {
    return raise_win32_error(runtime, "CreateEvent", GetLastError(), error);
  }
  OVERLAPPED overlapped{};
  overlapped.hEvent = event;
  DWORD transferred = 0;
  DWORD code = 0;
  BOOL connected = ConnectNamedPipe(handle, &overlapped);
  if (!connected) {
    code = GetLastError();
    if (code == ERROR_IO_PENDING) {
      BOOL complete = FALSE;
      {
        XlangRuntimeExecutionSuspension suspension;
        complete = GetOverlappedResult(handle, &overlapped, &transferred, TRUE);
      }
      code = complete ? ERROR_SUCCESS : GetLastError();
    } else if (code == ERROR_PIPE_CONNECTED) {
      code = ERROR_SUCCESS;
    }
  }
  CloseHandle(event);
  if (code != ERROR_SUCCESS) {
    return raise_win32_error(runtime, "ConnectNamedPipe", code, error);
  }
  return make_winapi_overlapped_result(0, 0, {}, out, error);
#else
  value_set_none(out);
  return true;
#endif
}

bool winapi_connect_named_pipe(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return winapi_connect_named_pipe_impl(runtime, args, argc, out, error);
}

bool winapi_connect_named_pipe_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return winapi_connect_named_pipe_impl(runtime, args, argc, out, error);
  }
  if (kwargc != 1 || kwargs[0].name == nullptr || std::string_view(kwargs[0].name) != "overlapped") {
    error = "ConnectNamedPipe() got an unexpected keyword argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc >= 2) {
    error = "ConnectNamedPipe() got multiple values for argument 'overlapped'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value bound[2] = {Value::invalid(), Value::invalid()};
  if (argc == 1) value_assign_fast(bound[0], args[0]);
  value_assign_fast(bound[1], *kwargs[0].value);
  return winapi_connect_named_pipe_impl(runtime, bound, argc + 1, out, error);
}

bool winapi_bytes_view(const Value& value, std::string_view& data, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    data = bytes_object_view(*bytes);
    return true;
  }
  if (auto* array = value_as_bytearray(value)) {
    data = array->value;
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    data = memoryview_object_view(*view);
    if (data.data() != nullptr) {
      return true;
    }
  }
  error = "WriteFile() data must be a bytes-like object";
  return false;
}

bool winapi_read_file_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error) {
  if (argc < 2 || argc > 3) {
    error = "ReadFile() expected handle, size, and optional overlapped flag";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  int64_t size_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "ReadFile", error) ||
      !winapi_int_arg(runtime, args[1], size_value, 2, "ReadFile", error)) {
    return false;
  }
  if (size_value < 0 || static_cast<uint64_t>(size_value) > static_cast<uint64_t>(UINT32_MAX)) {
    error = "ReadFile() size is out of range";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value));
  auto state = std::make_unique<WinapiOverlappedState>();
  state->handle = handle;
  state->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (state->event == nullptr) {
    return raise_win32_error(runtime, "CreateEvent", GetLastError(), error);
  }
  state->overlapped.hEvent = state->event;
  state->buffer.assign(static_cast<size_t>(size_value), '\0');
  state->is_read = true;
  // A zero-byte overlapped read is the readiness probe used by
  // multiprocessing.connection.wait().  Return that operation immediately so
  // the Python wait loop can cancel it after a zero-timeout poll.  Complete
  // data-bearing reads here: the compact runtime overlapped object exposes a
  // completed buffer, and keeping message-mode ERROR_MORE_DATA reads together
  // prevents the initial chunk from being discarded.
  const bool return_pending =
      size_value == 0 && argc == 3 && value_truthy(args[2]);
  {
    XlangRuntimeExecutionSuspension suspension;
    const BOOL complete = ReadFile(
        handle,
        state->buffer.empty() ? nullptr : state->buffer.data(),
        static_cast<DWORD>(size_value),
        &state->transferred,
        &state->overlapped);
    if (!complete) {
      state->error = GetLastError();
      if (state->error == ERROR_IO_PENDING && !return_pending) {
        const BOOL finished = GetOverlappedResult(
            handle,
            &state->overlapped,
            &state->transferred,
            TRUE);
        state->error = finished ? ERROR_SUCCESS : GetLastError();
      } else if (state->error == ERROR_IO_PENDING) {
        state->pending = true;
      }
    }
  }
  if (state->error == ERROR_MORE_DATA && state->transferred == 0 &&
      !state->buffer.empty()) {
    const size_t internal_count = static_cast<size_t>(state->overlapped.InternalHigh);
    state->transferred = static_cast<DWORD>(
        internal_count == 0
            ? state->buffer.size()
            : std::min(internal_count, state->buffer.size()));
  }
  if (state->error != ERROR_SUCCESS &&
      state->error != ERROR_MORE_DATA &&
      state->error != ERROR_IO_PENDING) {
    const DWORD code = state->error;
    CloseHandle(state->event);
    state->event = nullptr;
    return raise_win32_error(runtime, "ReadFile", code, error);
  }
  if (!state->pending && state->buffer.size() > state->transferred) {
    state->buffer.resize(static_cast<size_t>(state->transferred));
  }
  Value result;
  const DWORD code = state->error;
  if (!make_pending_winapi_overlapped_result(std::move(state), result, error)) {
    return false;
  }
  out = Value::tuple({result, Value::int64(static_cast<int64_t>(code))});
#else
  out = Value::tuple({Value::none(), Value::int64(0)});
#endif
  return true;
}

bool winapi_read_file(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return winapi_read_file_impl(runtime, args, argc, out, error);
}

bool winapi_write_file_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error) {
  if (argc < 2 || argc > 3) {
    error = "WriteFile() expected handle, data, and optional overlapped flag";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  std::string_view data;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "WriteFile", error) ||
      !winapi_bytes_view(args[1], data, error)) {
    return false;
  }
  if (data.size() > static_cast<size_t>(UINT32_MAX)) {
    error = "WriteFile() data is too large";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value));
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (event == nullptr) {
    return raise_win32_error(runtime, "CreateEvent", GetLastError(), error);
  }
  OVERLAPPED overlapped{};
  overlapped.hEvent = event;
  DWORD transferred = 0;
  DWORD code = ERROR_SUCCESS;
  BOOL complete = FALSE;
  {
    XlangRuntimeExecutionSuspension suspension;
    complete = WriteFile(
        handle,
        data.empty() ? nullptr : data.data(),
        static_cast<DWORD>(data.size()),
        &transferred,
        &overlapped);
    if (!complete) {
      code = GetLastError();
      if (code == ERROR_IO_PENDING) {
        complete = GetOverlappedResult(handle, &overlapped, &transferred, TRUE);
        code = complete ? ERROR_SUCCESS : GetLastError();
      }
    }
  }
  CloseHandle(event);
  if (code != ERROR_SUCCESS) {
    return raise_win32_error(runtime, "WriteFile", code, error);
  }
  Value result;
  if (!make_winapi_overlapped_result(transferred, code, {}, result, error)) {
    return false;
  }
  out = Value::tuple({result, Value::int64(static_cast<int64_t>(code))});
#else
  out = Value::tuple({Value::none(), Value::int64(0)});
#endif
  return true;
}

bool winapi_write_file(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return winapi_write_file_impl(runtime, args, argc, out, error);
}

using WinapiIoImpl = bool (*)(Runtime&, const Value*, uint32_t, Value&, std::string&);

bool winapi_io_kw_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    WinapiIoImpl impl) {
  if (kwargc == 0) {
    return impl(runtime, args, argc, out, error);
  }
  if (kwargc != 1 || kwargs[0].name == nullptr || std::string_view(kwargs[0].name) != "overlapped") {
    error = "I/O function got an unexpected keyword argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc >= 3) {
    error = "I/O function got multiple values for argument 'overlapped'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value bound[3] = {Value::invalid(), Value::invalid(), Value::invalid()};
  for (uint32_t i = 0; i < argc; ++i) value_assign_fast(bound[i], args[i]);
  value_assign_fast(bound[2], *kwargs[0].value);
  return impl(runtime, bound, 3, out, error);
}

bool winapi_read_file_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return winapi_io_kw_impl(
      runtime, args, argc, kwargs, kwargc, out, error, winapi_read_file_impl);
}

bool winapi_write_file_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return winapi_io_kw_impl(
      runtime, args, argc, kwargs, kwargc, out, error, winapi_write_file_impl);
}

bool winapi_peek_named_pipe(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "PeekNamedPipe() expected handle and optional size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  int64_t size_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "PeekNamedPipe", error) ||
      (argc == 2 && !winapi_int_arg(runtime, args[1], size_value, 2, "PeekNamedPipe", error))) {
    return false;
  }
  if (size_value < 0 || static_cast<uint64_t>(size_value) > static_cast<uint64_t>(UINT32_MAX)) {
    error = "PeekNamedPipe() size is out of range";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
#if defined(_WIN32)
  std::string data(static_cast<size_t>(size_value), '\0');
  DWORD read = 0;
  DWORD available = 0;
  DWORD left_in_message = 0;
  if (!PeekNamedPipe(
          reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)),
          data.empty() ? nullptr : data.data(),
          static_cast<DWORD>(data.size()),
          &read,
          &available,
          &left_in_message)) {
    return raise_win32_error(runtime, "PeekNamedPipe", GetLastError(), error);
  }
  data.resize(static_cast<size_t>(read));
  out = Value::tuple({
      Value::bytes(std::move(data)),
      Value::int64(static_cast<int64_t>(available)),
      Value::int64(static_cast<int64_t>(left_in_message)),
  });
#else
  out = Value::tuple({Value::bytes({}), Value::int64(0), Value::int64(0)});
#endif
  return true;
}

bool winapi_batched_wait_for_multiple_objects(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 3) {
    error = "BatchedWaitForMultipleObjects() expected handles, wait_all, and timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> values;
  if (!runtime_collect_iterable(runtime, args[0], values, error)) {
    return false;
  }
  if (values.empty() || values.size() > 3969) {
    error = "BatchedWaitForMultipleObjects() handle sequence has invalid length";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  int64_t timeout_value = 0;
  if (!winapi_int_arg(runtime, args[2], timeout_value, 3, "BatchedWaitForMultipleObjects", error)) {
    return false;
  }
#if defined(_WIN32)
  std::vector<HANDLE> handles;
  handles.reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    int64_t handle_value = 0;
    if (!winapi_int_arg(runtime, values[i], handle_value, static_cast<uint32_t>(i + 1), "BatchedWaitForMultipleObjects", error)) {
      return false;
    }
    HANDLE handle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value));
    DWORD flags = 0;
    if (!GetHandleInformation(handle, &flags)) {
      return raise_win32_error(runtime, "BatchedWaitForMultipleObjects", GetLastError(), error);
    }
    handles.push_back(handle);
  }

  const bool wait_all = value_truthy(args[1]);
  const DWORD timeout = static_cast<DWORD>(timeout_value);
  const ULONGLONG started = GetTickCount64();
  if (wait_all) {
    XlangRuntimeExecutionSuspension suspension;
    for (size_t offset = 0; offset < handles.size(); offset += MAXIMUM_WAIT_OBJECTS) {
      const DWORD count = static_cast<DWORD>(std::min<size_t>(MAXIMUM_WAIT_OBJECTS, handles.size() - offset));
      DWORD remaining = timeout;
      if (timeout != INFINITE) {
        const ULONGLONG elapsed = GetTickCount64() - started;
        remaining = elapsed >= timeout ? 0 : timeout - static_cast<DWORD>(elapsed);
      }
      const DWORD result = WaitForMultipleObjects(count, handles.data() + offset, TRUE, remaining);
      if (result == WAIT_TIMEOUT) {
        error = "BatchedWaitForMultipleObjects() timed out";
        runtime.raise_class_error("TimeoutError", error);
        return false;
      }
      if (result == WAIT_FAILED) {
        return raise_win32_error(runtime, "BatchedWaitForMultipleObjects", GetLastError(), error);
      }
    }
    out = Value::list({});
    return true;
  }

  std::vector<Value> triggered;
  XlangRuntimeExecutionSuspension suspension;
  for (;;) {
    for (size_t i = 0; i < handles.size(); ++i) {
      const DWORD result = WaitForSingleObject(handles[i], 0);
      if (result == WAIT_OBJECT_0) {
        triggered.push_back(Value::int64(static_cast<int64_t>(i)));
      } else if (result == WAIT_FAILED) {
        return raise_win32_error(runtime, "BatchedWaitForMultipleObjects", GetLastError(), error);
      }
    }
    if (!triggered.empty()) {
      out = Value::list(std::move(triggered));
      return true;
    }
    if (timeout != INFINITE && GetTickCount64() - started >= timeout) {
      error = "BatchedWaitForMultipleObjects() timed out";
      runtime.raise_class_error("TimeoutError", error);
      return false;
    }
    Sleep(1);
  }
#else
  out = Value::list({});
  return true;
#endif
}

bool winapi_wait_named_pipe(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "WaitNamedPipe() expected name and timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string name;
  int64_t timeout = 0;
  if (!winapi_string_arg(runtime, args[0], "WaitNamedPipe", name, error) ||
      !winapi_int_arg(runtime, args[1], timeout, 2, "WaitNamedPipe", error)) {
    return false;
  }
#if defined(_WIN32)
  BOOL ready = FALSE;
  {
    XlangRuntimeExecutionSuspension suspension;
    ready = WaitNamedPipeW(utf8_to_wide(name).c_str(), static_cast<DWORD>(timeout));
  }
  if (!ready) {
    return raise_win32_error(runtime, "WaitNamedPipe", GetLastError(), error);
  }
#endif
  value_set_none(out);
  return true;
}

bool winapi_wait_for_multiple_objects(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "WaitForMultipleObjects() expected handles, wait_all, and timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> values;
  if (!runtime_collect_iterable(runtime, args[0], values, error)) {
    return false;
  }
  if (values.empty() || values.size() > 64) {
    error = "WaitForMultipleObjects() handle sequence has invalid length";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  int64_t wait_all = 0;
  int64_t timeout = 0;
  if (!winapi_int_arg(runtime, args[1], wait_all, 2, "WaitForMultipleObjects", error) ||
      !winapi_int_arg(runtime, args[2], timeout, 3, "WaitForMultipleObjects", error)) {
    return false;
  }
#if defined(_WIN32)
  std::vector<HANDLE> handles;
  handles.reserve(values.size());
  for (uint32_t i = 0; i < values.size(); ++i) {
    int64_t handle_value = 0;
    if (!winapi_int_arg(runtime, values[i], handle_value, i + 1, "WaitForMultipleObjects", error)) {
      return false;
    }
    handles.push_back(reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)));
  }
  DWORD result = WAIT_FAILED;
  {
    XlangRuntimeExecutionSuspension suspension;
    result = WaitForMultipleObjects(
        static_cast<DWORD>(handles.size()),
        handles.data(),
        wait_all != 0,
        static_cast<DWORD>(timeout));
  }
  if (result == WAIT_FAILED) {
    return raise_win32_error(runtime, "WaitForMultipleObjects", GetLastError(), error);
  }
  out = Value::int64(static_cast<int64_t>(result));
#else
  out = Value::int64(0);
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
  if ((has_application_name && application_name.find('\0') != std::string::npos) ||
      command_line.find('\0') != std::string::npos ||
      (has_current_directory && current_directory.find('\0') != std::string::npos)) {
    error = "CreateProcess() argument contains a null character";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
#if defined(_WIN32)
  std::wstring env_block;
  void* environment = nullptr;
  DWORD native_creation_flags = static_cast<DWORD>(creation_flags);
  if (args[6].tag != ValueTag::None) {
    std::vector<std::wstring> env_entries;
    if (!winapi_collect_env_entries(runtime, args[6], env_entries, error)) {
      return false;
    }
    env_block = winapi_make_environment_block(std::move(env_entries));
    environment = env_block.empty() ? nullptr : env_block.data();
    native_creation_flags |= CREATE_UNICODE_ENVIRONMENT;
  }
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
  std::vector<HANDLE> inherited_handle_list;
  if (args[8].tag != ValueTag::None) {
    Value attribute_list;
    Value handle_values;
    std::string ignored;
    if (object_get_attr(args[8], "lpAttributeList", attribute_list, ignored) &&
        attribute_list.tag != ValueTag::None &&
        mapping_get_item(attribute_list, Value::string("handle_list"), handle_values, ignored)) {
      std::vector<Value> values;
      if (!runtime_collect_iterable(runtime, handle_values, values, error)) {
        return false;
      }
      inherited_handle_list.reserve(values.size());
      for (size_t index = 0; index < values.size(); ++index) {
        int64_t handle_value = 0;
        if (!winapi_int_arg(runtime, values[index], handle_value,
                            static_cast<uint32_t>(index + 1), "CreateProcess", error)) {
          return false;
        }
        inherited_handle_list.push_back(
            reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_value)));
      }
    }
  }
  std::wstring app = utf8_to_wide(application_name);
  std::wstring cmd = utf8_to_wide(command_line);
  std::wstring cwd = utf8_to_wide(current_directory);
  STARTUPINFOEXW extended_startup{};
  std::vector<unsigned char> attribute_storage;
  bool attribute_list_initialized = false;
  STARTUPINFOW* startup_pointer = &startup;
  if (!inherited_handle_list.empty()) {
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    attribute_storage.resize(attribute_size);
    extended_startup.StartupInfo = startup;
    extended_startup.StartupInfo.cb = sizeof(extended_startup);
    extended_startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
        attribute_storage.data());
    if (!InitializeProcThreadAttributeList(
            extended_startup.lpAttributeList, 1, 0, &attribute_size)) {
      return raise_win32_error(runtime, "CreateProcess", GetLastError(), error);
    }
    attribute_list_initialized = true;
    if (!UpdateProcThreadAttribute(
            extended_startup.lpAttributeList,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited_handle_list.data(),
            inherited_handle_list.size() * sizeof(HANDLE),
            nullptr,
            nullptr)) {
      const DWORD attribute_error = GetLastError();
      DeleteProcThreadAttributeList(extended_startup.lpAttributeList);
      return raise_win32_error(runtime, "CreateProcess", attribute_error, error);
    }
    startup_pointer = &extended_startup.StartupInfo;
    native_creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
  }
  const BOOL created = CreateProcessW(
          has_application_name ? app.c_str() : nullptr,
          cmd.empty() ? nullptr : cmd.data(),
          nullptr,
          nullptr,
          inherit_handles != 0,
          native_creation_flags,
          environment,
          has_current_directory ? cwd.c_str() : nullptr,
          startup_pointer,
          &process);
  const DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
  if (attribute_list_initialized) {
    DeleteProcThreadAttributeList(extended_startup.lpAttributeList);
  }
  if (!created) {
    return raise_win32_error(runtime, "CreateProcess", create_error, error);
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
    const DWORD code = GetLastError();
    if (code == ERROR_ACCESS_DENIED) {
      return raise_win32_permission_error(runtime, "TerminateProcess", code, error);
    }
    return raise_win32_error(runtime, "TerminateProcess", code, error);
  }
#endif
  value_set_none(out);
  return true;
}

bool winapi_exit_process(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ExitProcess() expected an exit code";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t exit_code = 0;
  if (!winapi_int_arg(runtime, args[0], exit_code, 1, "ExitProcess", error)) {
    return false;
  }
  ExitProcess(static_cast<UINT>(exit_code));
  value_set_none(out);
  return true;
}

bool winapi_lcmap_string_ex(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "LCMapStringEx() expected locale, flags and source";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string locale;
  std::string source;
  int64_t flags = 0;
  if (!winapi_string_arg(runtime, args[0], "LCMapStringEx", locale, error) ||
      !winapi_int_arg(runtime, args[1], flags, 2, "LCMapStringEx", error) ||
      !winapi_string_arg(runtime, args[2], "LCMapStringEx", source, error)) {
    return false;
  }
#if defined(_WIN32)
  bool contains_surrogate = false;
  for (size_t index = 0; index + 2 < source.size(); ++index) {
    if (static_cast<unsigned char>(source[index]) == 0xedu &&
        (static_cast<unsigned char>(source[index + 1]) & 0xe0u) == 0xa0u &&
        (static_cast<unsigned char>(source[index + 2]) & 0xc0u) == 0x80u) {
      contains_surrogate = true;
      break;
    }
  }
  if (contains_surrogate && (flags & LCMAP_LOWERCASE) != 0) {
    for (char& ch : source) {
      const auto byte = static_cast<unsigned char>(ch);
      if (byte < 0x80u) ch = static_cast<char>(std::tolower(byte));
    }
    out = Value::string(std::move(source));
    return true;
  }
  const std::wstring wide_locale = utf8_to_wide(locale);
  const std::wstring wide_source = utf8_to_wide(source);
  const wchar_t* locale_name = locale.empty() ? LOCALE_NAME_INVARIANT : wide_locale.c_str();
  const int required = LCMapStringEx(
      locale_name, static_cast<DWORD>(flags), wide_source.data(),
      static_cast<int>(wide_source.size()), nullptr, 0, nullptr, nullptr, 0);
  if (required <= 0) {
    return raise_win32_error(runtime, "LCMapStringEx", GetLastError(), error);
  }
  std::wstring mapped(static_cast<size_t>(required), L'\0');
  if (LCMapStringEx(
          locale_name, static_cast<DWORD>(flags), wide_source.data(),
          static_cast<int>(wide_source.size()), mapped.data(), required,
          nullptr, nullptr, 0) <= 0) {
    return raise_win32_error(runtime, "LCMapStringEx", GetLastError(), error);
  }
  out = Value::string(wide_to_utf8(mapped));
#else
  out = Value::string(std::move(source));
#endif
  return true;
}

bool winapi_get_module_file_name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "GetModuleFileName() expected module handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  if (!winapi_int_arg(runtime, args[0], handle_value, 1, "GetModuleFileName", error)) {
    return false;
  }
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD copied = GetModuleFileNameW(
        reinterpret_cast<HMODULE>(static_cast<intptr_t>(handle_value)),
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (copied == 0) {
      return raise_win32_error(runtime, "GetModuleFileName", GetLastError(), error);
    }
    if (copied < buffer.size() - 1) {
      buffer.resize(copied);
      out = Value::string(wide_to_utf8(buffer));
      return true;
    }
    buffer.resize(buffer.size() * 2);
  }
#else
  (void)handle_value;
  out = Value::string("");
  return true;
#endif
}

} // namespace

void forget_winapi_pipe_handle(intptr_t handle) {
#if defined(_WIN32)
  (void)forget_pipe_handle(reinterpret_cast<HANDLE>(handle));
#else
  (void)handle;
#endif
}

void register_winapi_module(Runtime& runtime) {
#if defined(_WIN32)
  g_winapi_overlapped_class = make_winapi_overlapped_class(runtime);
  Value winapi_overlapped_class = g_winapi_overlapped_class;
#else
  Value winapi_overlapped_class = Value::class_object("OverlappedResult", {});
#endif
  NativeModuleBuilder builder(runtime, "_winapi");
  builder.value("__doc__", Value::none())
#if defined(_WIN32)
      .value("NULL", Value::int64(0))
      .value("PIPE_ACCESS_DUPLEX", Value::int64(PIPE_ACCESS_DUPLEX))
      .value("PIPE_ACCESS_INBOUND", Value::int64(PIPE_ACCESS_INBOUND))
      .value("PIPE_TYPE_MESSAGE", Value::int64(PIPE_TYPE_MESSAGE))
      .value("PIPE_READMODE_MESSAGE", Value::int64(PIPE_READMODE_MESSAGE))
      .value("PIPE_WAIT", Value::int64(PIPE_WAIT))
      .value("PIPE_UNLIMITED_INSTANCES", Value::int64(PIPE_UNLIMITED_INSTANCES))
      .value("NMPWAIT_WAIT_FOREVER", Value::int64(NMPWAIT_WAIT_FOREVER))
      .value("GENERIC_READ", Value::int64(static_cast<int64_t>(GENERIC_READ)))
      .value("GENERIC_WRITE", Value::int64(static_cast<int64_t>(GENERIC_WRITE)))
      .value("FILE_GENERIC_READ", Value::int64(static_cast<int64_t>(FILE_GENERIC_READ)))
      .value("FILE_GENERIC_WRITE", Value::int64(static_cast<int64_t>(FILE_GENERIC_WRITE)))
      .value("FILE_FLAG_OVERLAPPED", Value::int64(FILE_FLAG_OVERLAPPED))
      .value("FILE_FLAG_FIRST_PIPE_INSTANCE", Value::int64(FILE_FLAG_FIRST_PIPE_INSTANCE))
      .value("OPEN_EXISTING", Value::int64(OPEN_EXISTING))
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
      .value("DUPLICATE_CLOSE_SOURCE", Value::int64(DUPLICATE_CLOSE_SOURCE))
      .value("SYNCHRONIZE", Value::int64(SYNCHRONIZE))
      .value("PROCESS_DUP_HANDLE", Value::int64(PROCESS_DUP_HANDLE))
      .value("FILE_TYPE_CHAR", Value::int64(FILE_TYPE_CHAR))
      .value("WAIT_OBJECT_0", Value::int64(WAIT_OBJECT_0))
      .value("WAIT_ABANDONED_0", Value::int64(WAIT_ABANDONED_0))
      .value("WAIT_TIMEOUT", Value::int64(WAIT_TIMEOUT))
      .value("INFINITE", Value::int64(INFINITE))
      .value("STILL_ACTIVE", Value::int64(STILL_ACTIVE))
      .value("LOCALE_NAME_INVARIANT", Value::string(""))
      .value("LCMAP_LOWERCASE", Value::int64(LCMAP_LOWERCASE))
      .value("COPY_FILE_ALLOW_DECRYPTED_DESTINATION", Value::int64(COPY_FILE_ALLOW_DECRYPTED_DESTINATION))
      .value("COPY_FILE_COPY_SYMLINK", Value::int64(COPY_FILE_COPY_SYMLINK))
      .value("ERROR_PRIVILEGE_NOT_HELD", Value::int64(ERROR_PRIVILEGE_NOT_HELD))
      .value("ERROR_ACCESS_DENIED", Value::int64(ERROR_ACCESS_DENIED))
      .value("ERROR_BROKEN_PIPE", Value::int64(ERROR_BROKEN_PIPE))
      .value("ERROR_NETNAME_DELETED", Value::int64(ERROR_NETNAME_DELETED))
      .value("ERROR_IO_PENDING", Value::int64(ERROR_IO_PENDING))
      .value("ERROR_MORE_DATA", Value::int64(ERROR_MORE_DATA))
      .value("ERROR_NO_DATA", Value::int64(ERROR_NO_DATA))
      .value("ERROR_OPERATION_ABORTED", Value::int64(ERROR_OPERATION_ABORTED))
      .value("ERROR_PIPE_BUSY", Value::int64(ERROR_PIPE_BUSY))
      .value("ERROR_SEM_TIMEOUT", Value::int64(ERROR_SEM_TIMEOUT))
#endif
      .value("_OverlappedResult", winapi_overlapped_class)
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
      .function("CreateJunction", winapi_create_junction)
      .function("CloseHandle", winapi_close_handle)
      .function("GetCurrentProcess", winapi_get_current_process)
      .function("OpenProcess", winapi_open_process)
      .function("GetStdHandle", winapi_get_std_handle)
      .function("GetFileType", winapi_get_file_type)
      .function("CreatePipe", winapi_create_pipe)
      .function("CreateEventW", winapi_create_event)
      .function("SetEvent", winapi_set_event)
      .function("ResetEvent", winapi_reset_event)
      .function("CreateNamedPipe", winapi_create_named_pipe)
      .function("CreateFile", winapi_create_file)
      .function("SetNamedPipeHandleState", winapi_set_named_pipe_handle_state)
      .value(
          "ConnectNamedPipe",
          winapi_native_function(
              runtime,
              "_winapi.ConnectNamedPipe",
              "ConnectNamedPipe",
              winapi_connect_named_pipe,
              "",
              winapi_connect_named_pipe_kw))
      .value(
          "ReadFile",
          winapi_native_function(
              runtime,
              "_winapi.ReadFile",
              "ReadFile",
              winapi_read_file,
              "",
              winapi_read_file_kw))
      .value(
          "WriteFile",
          winapi_native_function(
              runtime,
              "_winapi.WriteFile",
              "WriteFile",
              winapi_write_file,
              "",
              winapi_write_file_kw))
      .function("PeekNamedPipe", winapi_peek_named_pipe)
      .function("WaitNamedPipe", winapi_wait_named_pipe)
      .function("WaitForMultipleObjects", winapi_wait_for_multiple_objects)
      .function("BatchedWaitForMultipleObjects", winapi_batched_wait_for_multiple_objects)
      .function("GetLongPathName", winapi_get_long_path_name)
      .function("GetShortPathName", winapi_get_short_path_name)
      .function("DuplicateHandle", winapi_duplicate_handle)
      .function("CreateProcess", winapi_create_process)
      .function("WaitForSingleObject", winapi_wait_for_single_object)
      .function("GetExitCodeProcess", winapi_get_exit_code_process)
      .function("GetModuleFileName", winapi_get_module_file_name)
      .function("TerminateProcess", winapi_terminate_process)
      .function("ExitProcess", winapi_exit_process)
      .function("LCMapStringEx", winapi_lcmap_string_ex);
  runtime.register_module("_winapi", builder.finish());
}

} // namespace xlang3
