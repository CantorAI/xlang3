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

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xlang3 {

namespace {

struct WinregState {
  Value hkey_class;
};

#if defined(_WIN32)
struct WinregHandle {
  HKEY handle = nullptr;
  bool owned = true;
};

void close_winreg_handle(void* data) {
  auto* key = static_cast<WinregHandle*>(data);
  if (key != nullptr) {
    if (key->owned && key->handle != nullptr) RegCloseKey(key->handle);
    delete key;
  }
}

bool winreg_handle_from_value(const Value& value, HKEY& handle) {
  if (value.tag == ValueTag::Int64) {
    handle = reinterpret_cast<HKEY>(static_cast<uintptr_t>(value.as.i64));
    return true;
  }
  auto* key = static_cast<WinregHandle*>(instance_get_native_data(value, "winreg.HKEYType"));
  if (key == nullptr || key->handle == nullptr) return false;
  handle = key->handle;
  return true;
}

std::wstring winreg_wstring(const Value& value) {
  auto* text = value_as_string(value);
  if (text == nullptr) return {};
  const std::string utf8 = string_object_to_string(*text);
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
  return wide;
}

Value make_winreg_handle(WinregState* state, HKEY handle) {
  Value result = Value::instance(state->hkey_class);
  std::string ignored;
  instance_set_native_data(
      result, "winreg.HKEYType", new WinregHandle{handle, true},
      close_winreg_handle, ignored);
  return result;
}
#endif

bool winreg_hkey_close(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "HKEYType.Close() expected self";
    return false;
  }
#if defined(_WIN32)
  auto* key = static_cast<WinregHandle*>(instance_get_native_data(args[0], "winreg.HKEYType"));
  if (key == nullptr) {
    error = "invalid registry handle";
    return false;
  }
  if (key->owned && key->handle != nullptr) RegCloseKey(key->handle);
  key->handle = nullptr;
  key->owned = false;
#endif
  value_set_none(out);
  return true;
}

bool winreg_hkey_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "HKEYType.__enter__() expected self";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool winreg_hkey_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 4) {
    error = "HKEYType.__exit__() expected self and exception details";
    return false;
  }
  Value ignored;
  if (!winreg_hkey_close(runtime, args, 1, ignored, error, data)) return false;
  value_set_bool(out, false);
  return true;
}

bool winreg_connect_registry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 2) {
    error = "winreg.ConnectRegistry() expected computer_name and key";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  HKEY source = nullptr;
  if (!winreg_handle_from_value(args[1], source)) {
    error = "invalid registry key handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::wstring computer;
  const wchar_t* computer_name = nullptr;
  if (args[0].tag != ValueTag::None) {
    if (value_as_string(args[0]) == nullptr) {
      error = "computer_name must be str or None";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    computer = winreg_wstring(args[0]);
    computer_name = computer.c_str();
  }
  HKEY connected = nullptr;
  const LSTATUS status = RegConnectRegistryW(computer_name, source, &connected);
  if (status != ERROR_SUCCESS) {
    error = "unable to connect to registry";
    runtime.raise_class_error(status == ERROR_ACCESS_DENIED ? "PermissionError" : "OSError", error);
    return false;
  }
  out = make_winreg_handle(static_cast<WinregState*>(data), connected);
  return true;
#else
  error = "registry APIs are only available on Windows";
  runtime.raise_class_error("OSError", error);
  return false;
#endif
}

bool winreg_open_key(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 2 || argc > 4) {
    error = "winreg.OpenKey() expected key, sub_key, optional reserved and access";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  HKEY parent = nullptr;
  if (!winreg_handle_from_value(args[0], parent) || value_as_string(args[1]) == nullptr) {
    error = "invalid registry key or sub_key";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  REGSAM access = KEY_READ;
  if (argc == 4) {
    if (args[3].tag != ValueTag::Int64) {
      error = "registry access must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    access = static_cast<REGSAM>(args[3].as.i64);
  }
  const std::wstring subkey = winreg_wstring(args[1]);
  HKEY opened = nullptr;
  const LSTATUS status = RegOpenKeyExW(parent, subkey.c_str(), 0, access, &opened);
  if (status != ERROR_SUCCESS) {
    error = "registry key not found";
    runtime.raise_class_error(status == ERROR_ACCESS_DENIED ? "PermissionError" : "OSError", error);
    return false;
  }
  out = make_winreg_handle(static_cast<WinregState*>(data), opened);
  return true;
#else
  error = "registry APIs are only available on Windows";
  runtime.raise_class_error("OSError", error);
  return false;
#endif
}

bool winreg_missing_key(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "registry key not found";
  runtime.raise_class_error("OSError", error);
  return false;
}

bool winreg_close_key(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "winreg.CloseKey() expected one key";
    return false;
  }
  value_set_none(out);
  return true;
}

bool winreg_query_info_key(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "winreg.QueryInfoKey() expected one key handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  DWORD subkeys = 0;
  DWORD values = 0;
  FILETIME modified{};
  const auto handle = reinterpret_cast<HKEY>(static_cast<uintptr_t>(args[0].as.i64));
  const LSTATUS status = RegQueryInfoKeyW(
      handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values,
      nullptr, nullptr, nullptr, &modified);
  if (status != ERROR_SUCCESS) {
    error = "registry key not found";
    runtime.raise_class_error(status == ERROR_ACCESS_DENIED ? "PermissionError" : "OSError", error);
    return false;
  }
  const uint64_t ticks = (static_cast<uint64_t>(modified.dwHighDateTime) << 32) | modified.dwLowDateTime;
  constexpr uint64_t kWindowsEpochTicks = 116444736000000000ull;
  const int64_t seconds = ticks >= kWindowsEpochTicks
      ? static_cast<int64_t>((ticks - kWindowsEpochTicks) / 10000000ull) : 0;
  out = Value::tuple({Value::int64(subkeys), Value::int64(values), Value::int64(seconds)});
  return true;
#else
  error = "registry APIs are only available on Windows";
  runtime.raise_class_error("OSError", error);
  return false;
#endif
}

} // namespace

void register_winreg_module(Runtime& runtime) {
  auto* state = new WinregState();
  runtime.register_native_package_cleanup(state, [](void* data) { delete static_cast<WinregState*>(data); });
  Value object_base = runtime.find_builtin("object") != nullptr
      ? *runtime.find_builtin("object") : Value::invalid();
  state->hkey_class = Value::class_object(
      "HKEYType",
      {{"__module__", Value::string("winreg")},
       {"Close", runtime.make_native_function("winreg.HKEYType.Close", winreg_hkey_close)},
       {"close", runtime.make_native_function("winreg.HKEYType.close", winreg_hkey_close)},
       {"__enter__", runtime.make_native_function("winreg.HKEYType.__enter__", winreg_hkey_enter)},
       {"__exit__", runtime.make_native_function("winreg.HKEYType.__exit__", winreg_hkey_exit)}},
      object_base);
  NativeModuleBuilder builder(runtime, "winreg");
  builder.value("HKEYType", state->hkey_class)
      .value("HKEY_CLASSES_ROOT", Value::int64(0x80000000ll))
      .value("HKEY_CURRENT_USER", Value::int64(0x80000001ll))
      .value("HKEY_LOCAL_MACHINE", Value::int64(0x80000002ll))
      .value("HKEY_USERS", Value::int64(0x80000003ll))
      .value("HKEY_PERFORMANCE_DATA", Value::int64(0x80000004ll))
      .value("HKEY_CURRENT_CONFIG", Value::int64(0x80000005ll))
      .value("KEY_QUERY_VALUE", Value::int64(0x0001))
      .value("KEY_SET_VALUE", Value::int64(0x0002))
      .value("KEY_CREATE_SUB_KEY", Value::int64(0x0004))
      .value("KEY_ENUMERATE_SUB_KEYS", Value::int64(0x0008))
      .value("KEY_NOTIFY", Value::int64(0x0010))
      .value("KEY_CREATE_LINK", Value::int64(0x0020))
      .value("KEY_WOW64_64KEY", Value::int64(0x0100))
      .value("KEY_WOW64_32KEY", Value::int64(0x0200))
      .value("KEY_WRITE", Value::int64(0x20006))
      .value("KEY_READ", Value::int64(0x20019))
      .value("KEY_ALL_ACCESS", Value::int64(0xF003F))
      .value("REG_NONE", Value::int64(0))
      .value("REG_SZ", Value::int64(1))
      .value("REG_EXPAND_SZ", Value::int64(2))
      .value("REG_BINARY", Value::int64(3))
      .value("REG_DWORD", Value::int64(4))
      .value("REG_MULTI_SZ", Value::int64(7))
      .value("REG_QWORD", Value::int64(11))
      .value("ConnectRegistry", runtime.make_native_function("winreg.ConnectRegistry", winreg_connect_registry, state))
      .value("OpenKey", runtime.make_native_function("winreg.OpenKey", winreg_open_key, state))
      .value("OpenKeyEx", runtime.make_native_function("winreg.OpenKeyEx", winreg_open_key, state))
      .value("QueryValue", runtime.make_native_function("winreg.QueryValue", winreg_missing_key))
      .value("QueryValueEx", runtime.make_native_function("winreg.QueryValueEx", winreg_missing_key))
      .value("EnumKey", runtime.make_native_function("winreg.EnumKey", winreg_missing_key))
      .value("EnumValue", runtime.make_native_function("winreg.EnumValue", winreg_missing_key))
      .value("QueryInfoKey", runtime.make_native_function("winreg.QueryInfoKey", winreg_query_info_key))
      .value("CloseKey", runtime.make_native_function("winreg.CloseKey", winreg_close_key));
  runtime.register_module("winreg", builder.finish());
}

} // namespace xlang3
