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

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xlang3 {

namespace {

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
  NativeModuleBuilder builder(runtime, "winreg");
  builder.value("HKEY_CLASSES_ROOT", Value::int64(0x80000000ll))
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
      .value("OpenKey", runtime.make_native_function("winreg.OpenKey", winreg_missing_key))
      .value("OpenKeyEx", runtime.make_native_function("winreg.OpenKeyEx", winreg_missing_key))
      .value("QueryValue", runtime.make_native_function("winreg.QueryValue", winreg_missing_key))
      .value("QueryValueEx", runtime.make_native_function("winreg.QueryValueEx", winreg_missing_key))
      .value("EnumKey", runtime.make_native_function("winreg.EnumKey", winreg_missing_key))
      .value("EnumValue", runtime.make_native_function("winreg.EnumValue", winreg_missing_key))
      .value("QueryInfoKey", runtime.make_native_function("winreg.QueryInfoKey", winreg_query_info_key))
      .value("CloseKey", runtime.make_native_function("winreg.CloseKey", winreg_close_key));
  runtime.register_module("winreg", builder.finish());
}

} // namespace xlang3
