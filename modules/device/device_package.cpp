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
#include "xlang3/xmodule.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(_WIN32)
#define X3_DEVICE_EXPORT __declspec(dllexport)
#else
#define X3_DEVICE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

constexpr const char* kDeviceNativeType = "xlang3.device.Device";
constexpr const char* kI2CModuleNativeType = "xlang3.device.I2CModule";
constexpr const char* kI2CBusNativeType = "xlang3.device.I2CBus";
constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct DevicePackageState {
  const X3PackageHost* host = nullptr;
  X3Value device_class = x3_value_invalid();
  X3Value i2c_module_class = x3_value_invalid();
  X3Value i2c_bus_class = x3_value_invalid();
};

struct DeviceHandle {
  std::string kind = "disconnected";
  std::string transport = "none";
  std::string port;
#if defined(_WIN32)
  HANDLE serial = INVALID_HANDLE_VALUE;
#endif
};

struct DeviceModuleProxy {
  DeviceHandle* handle = nullptr;
};

struct I2CBusProxy {
  DeviceHandle* handle = nullptr;
  uint32_t sda = 4;
  uint32_t scl = 5;
  uint32_t baud = 100000;
};

void cleanup_device(void* data) {
  auto* handle = static_cast<DeviceHandle*>(data);
#if defined(_WIN32)
  if (handle != nullptr && handle->serial != INVALID_HANDLE_VALUE) {
    CloseHandle(handle->serial);
    handle->serial = INVALID_HANDLE_VALUE;
  }
#endif
  delete handle;
}

void cleanup_proxy(void* data) {
  delete static_cast<DeviceModuleProxy*>(data);
}

void cleanup_i2c_bus(void* data) {
  delete static_cast<I2CBusProxy*>(data);
}

DevicePackageState* state_from(void* user_data) {
  return static_cast<DevicePackageState*>(user_data);
}

bool check_argc(const X3PackageHost* host, X3CallContext* context, uint32_t argc, uint32_t expected, const char* name) {
  if (argc == expected) {
    return true;
  }
  const std::string error = std::string(name) + " expected " + std::to_string(expected) + " arguments";
  host->set_error(context, error.c_str());
  return false;
}

bool check_argc_range(
    const X3PackageHost* host,
    X3CallContext* context,
    uint32_t argc,
    uint32_t min_argc,
    uint32_t max_argc,
    const char* name) {
  if (argc >= min_argc && argc <= max_argc) {
    return true;
  }
  const std::string error = std::string(name) + " expected " + std::to_string(min_argc) + " to " +
                            std::to_string(max_argc) + " arguments";
  host->set_error(context, error.c_str());
  return false;
}

DeviceHandle* require_device(DevicePackageState* state, X3CallContext* context, X3Value self) {
  auto* handle = static_cast<DeviceHandle*>(state->host->instance_get_native_data(self, kDeviceNativeType));
  if (handle == nullptr) {
    state->host->set_error(context, "invalid device object");
  }
  return handle;
}

bool require_string(
    const X3PackageHost* host,
    X3CallContext* context,
    X3Runtime* runtime,
    X3Value value,
    const char* message,
    std::string& out) {
  if (host->value_object_kind(value) != X3_OBJECT_KIND_STRING) {
    host->set_error(context, message);
    return false;
  }
  out = host->value_to_cstr(runtime, value);
  return true;
}

bool require_uint(
    const X3PackageHost* host,
    X3CallContext* context,
    X3Value value,
    const char* message,
    uint32_t& out) {
  if (value.tag == X3_TAG_UINT64) {
    if (value.as.u64 > UINT32_MAX) {
      host->set_error(context, message);
      return false;
    }
    out = static_cast<uint32_t>(value.as.u64);
    return true;
  }
  if (value.tag == X3_TAG_INT64 && value.as.i64 >= 0 && value.as.i64 <= UINT32_MAX) {
    out = static_cast<uint32_t>(value.as.i64);
    return true;
  }
  host->set_error(context, message);
  return false;
}

DeviceModuleProxy* require_module_proxy(
    DevicePackageState* state,
    X3CallContext* context,
    X3Value self,
    const char* type_name) {
  auto* proxy = static_cast<DeviceModuleProxy*>(state->host->instance_get_native_data(self, type_name));
  if (proxy == nullptr || proxy->handle == nullptr) {
    state->host->set_error(context, "invalid device module object");
  }
  return proxy;
}

I2CBusProxy* require_i2c_bus(DevicePackageState* state, X3CallContext* context, X3Value self) {
  auto* proxy = static_cast<I2CBusProxy*>(state->host->instance_get_native_data(self, kI2CBusNativeType));
  if (proxy == nullptr || proxy->handle == nullptr) {
    state->host->set_error(context, "invalid i2c bus object");
  }
  return proxy;
}

std::string base64_encode(const uint8_t* data, std::size_t size) {
  std::string out;
  out.reserve(((size + 2) / 3) * 4);
  for (std::size_t i = 0; i < size; i += 3) {
    const uint32_t b0 = data[i];
    const uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
    const uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out.push_back(kBase64Alphabet[(triple >> 18) & 0x3f]);
    out.push_back(kBase64Alphabet[(triple >> 12) & 0x3f]);
    out.push_back(i + 1 < size ? kBase64Alphabet[(triple >> 6) & 0x3f] : '=');
    out.push_back(i + 2 < size ? kBase64Alphabet[triple & 0x3f] : '=');
  }
  return out;
}

int base64_value(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

bool base64_decode(const std::string& text, std::vector<uint8_t>& out) {
  out.clear();
  int values[4] = {};
  int count = 0;
  for (char ch : text) {
    if (ch == '=') {
      values[count++] = -2;
    } else {
      const int value = base64_value(ch);
      if (value < 0) {
        return false;
      }
      values[count++] = value;
    }
    if (count == 4) {
      if (values[0] < 0 || values[1] < 0) return false;
      const uint32_t triple =
          (static_cast<uint32_t>(values[0]) << 18) |
          (static_cast<uint32_t>(values[1]) << 12) |
          (values[2] >= 0 ? static_cast<uint32_t>(values[2]) << 6 : 0) |
          (values[3] >= 0 ? static_cast<uint32_t>(values[3]) : 0);
      out.push_back(static_cast<uint8_t>((triple >> 16) & 0xff));
      if (values[2] != -2) out.push_back(static_cast<uint8_t>((triple >> 8) & 0xff));
      if (values[3] != -2) out.push_back(static_cast<uint8_t>(triple & 0xff));
      count = 0;
    }
  }
  return count == 0;
}

#if defined(_WIN32)
bool open_serial_port(const std::string& port, HANDLE& out) {
  const std::string path = "\\\\.\\" + port;
  HANDLE serial = CreateFileA(
      path.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (serial == INVALID_HANDLE_VALUE) {
    return false;
  }

  DCB dcb{};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(serial, &dcb)) {
    CloseHandle(serial);
    return false;
  }
  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  if (!SetCommState(serial, &dcb)) {
    CloseHandle(serial);
    return false;
  }

  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutConstant = 100;
  timeouts.ReadTotalTimeoutMultiplier = 1;
  timeouts.WriteTotalTimeoutConstant = 1000;
  timeouts.WriteTotalTimeoutMultiplier = 1;
  if (!SetCommTimeouts(serial, &timeouts)) {
    CloseHandle(serial);
    return false;
  }

  EscapeCommFunction(serial, SETDTR);
  EscapeCommFunction(serial, SETRTS);
  std::this_thread::sleep_for(std::chrono::milliseconds(1800));
  PurgeComm(serial, PURGE_RXCLEAR | PURGE_TXCLEAR);
  out = serial;
  return true;
}

bool read_serial_line(HANDLE serial, std::string& out) {
  out.clear();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    char ch = 0;
    DWORD read = 0;
    if (!ReadFile(serial, &ch, 1, &read, nullptr)) {
      return false;
    }
    if (read == 0) {
      continue;
    }
    if (ch == '\n') {
      if (!out.empty() && out.back() == '\r') {
        out.pop_back();
      }
      return true;
    }
    out.push_back(ch);
  }
  return !out.empty();
}

bool write_serial_text(HANDLE serial, const std::string& text) {
  DWORD written = 0;
  return WriteFile(serial, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
}

std::string configured_port() {
  char buffer[64] = {};
  DWORD length = GetEnvironmentVariableA("XLANG3_DEVICE_PORT", buffer, static_cast<DWORD>(sizeof(buffer)));
  if (length > 0 && length < sizeof(buffer)) {
    return buffer;
  }
  return "COM5";
}
#endif

bool connect_device(DeviceHandle& handle) {
#if defined(_WIN32)
  HANDLE serial = INVALID_HANDLE_VALUE;
  const std::string port = configured_port();
  if (!open_serial_port(port, serial)) {
    return false;
  }
  handle.kind = "rp2040";
  handle.transport = "usb-serial";
  handle.port = port;
  handle.serial = serial;
  return true;
#else
  (void)handle;
  return false;
#endif
}

bool device_request(DeviceHandle& handle, const std::string& frame, std::vector<std::string>& lines, std::string& error) {
#if defined(_WIN32)
  if (handle.serial == INVALID_HANDLE_VALUE) {
    error = "device is not connected";
    return false;
  }

  PurgeComm(handle.serial, PURGE_RXCLEAR | PURGE_TXCLEAR);
  if (!write_serial_text(handle.serial, frame + "\n")) {
    error = "failed to write to device serial port";
    return false;
  }

  lines.clear();
  std::string line;
  while (read_serial_line(handle.serial, line)) {
    if (line == "END") {
      return true;
    }
    if (line == "OK") {
      continue;
    }
    if (line.rfind("ERR ", 0) == 0) {
      error = line;
      return false;
    }
    lines.push_back(line);
  }
  error = "timed out waiting for device response";
  return false;
#else
  (void)handle;
  (void)frame;
  (void)lines;
  error = "device serial transport is only implemented on Windows in this checkpoint";
  return false;
#endif
}

bool device_exec_source(DeviceHandle& handle, const std::string& source, std::string& output, std::string& error) {
  std::vector<std::string> lines;
  if (!device_request(handle, "py " + source, lines, error)) {
    return false;
  }
  output.clear();
  for (const auto& line : lines) {
    output += line;
    output += '\n';
  }
  return true;
}

bool device_put_data(DeviceHandle& handle, const std::string& remote_path, const std::string& data, std::string& error) {
  const auto encoded = base64_encode(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  std::vector<std::string> lines;
  return device_request(handle, "put " + remote_path + " " + encoded, lines, error);
}

bool device_get_data(DeviceHandle& handle, const std::string& remote_path, std::string& data, std::string& error) {
  std::vector<std::string> lines;
  if (!device_request(handle, "get " + remote_path, lines, error)) {
    return false;
  }
  for (const auto& line : lines) {
    if (line.rfind("DATA ", 0) == 0) {
      std::vector<uint8_t> decoded;
      if (!base64_decode(line.substr(5), decoded)) {
        error = "device returned invalid base64 data";
        return false;
      }
      data.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
      return true;
    }
  }
  error = "device did not return data";
  return false;
}

bool device_list_files(DeviceHandle& handle, const std::string& path, std::vector<std::string>& entries, std::string& error) {
  std::vector<std::string> lines;
  if (!device_request(handle, "list " + path, lines, error)) {
    return false;
  }
  entries.clear();
  for (const auto& line : lines) {
    if (line.rfind("ITEM ", 0) == 0) {
      std::vector<uint8_t> decoded;
      if (!base64_decode(line.substr(5), decoded)) {
        error = "device returned invalid base64 item";
        return false;
      }
      entries.emplace_back(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    }
  }
  return true;
}

bool device_delete_file(DeviceHandle& handle, const std::string& remote_path, std::string& error) {
  std::vector<std::string> lines;
  return device_request(handle, "delete " + remote_path, lines, error);
}

bool device_i2c_scan(
    DeviceHandle& handle,
    uint32_t sda,
    uint32_t scl,
    uint32_t baud,
    std::vector<uint32_t>& addresses,
    std::string& error) {
  std::vector<std::string> lines;
  const std::string command =
      "i2c_scan " + std::to_string(sda) + " " + std::to_string(scl) + " " + std::to_string(baud);
  if (!device_request(handle, command, lines, error)) {
    return false;
  }
  addresses.clear();
  for (const auto& line : lines) {
    if (line.rfind("ADDR ", 0) != 0) {
      continue;
    }
    try {
      addresses.push_back(static_cast<uint32_t>(std::stoul(line.substr(5))));
    } catch (...) {
      error = "device returned invalid i2c address";
      return false;
    }
  }
  return true;
}

bool device_i2c_write(
    DeviceHandle& handle,
    uint32_t sda,
    uint32_t scl,
    uint32_t baud,
    uint32_t address,
    const std::vector<uint8_t>& data,
    std::string& error) {
  const auto encoded = base64_encode(data.data(), data.size());
  std::vector<std::string> lines;
  const std::string command = "i2c_write " + std::to_string(sda) + " " + std::to_string(scl) + " " +
                              std::to_string(baud) + " " + std::to_string(address) + " " + encoded;
  return device_request(handle, command, lines, error);
}

bool device_ping(DeviceHandle& handle, std::string& error) {
  std::vector<std::string> lines;
  if (!device_request(handle, "ping", lines, error)) {
    return false;
  }
  return !lines.empty() && lines[0] == "PONG";
}

bool device_echo_data(DeviceHandle& handle, const std::string& data, std::string& echoed, std::string& error) {
  const auto encoded = base64_encode(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  std::vector<std::string> lines;
  if (!device_request(handle, "echo " + encoded, lines, error)) {
    return false;
  }
  for (const auto& line : lines) {
    if (line.rfind("DATA ", 0) == 0) {
      std::vector<uint8_t> decoded;
      if (!base64_decode(line.substr(5), decoded)) {
        error = "device returned invalid base64 echo";
        return false;
      }
      echoed.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
      return true;
    }
  }
  error = "device did not return echo data";
  return false;
}

bool device_kv_request(DeviceHandle& handle, const std::string& command, std::vector<std::string>& lines, std::string& error) {
  return device_request(handle, command, lines, error);
}

bool require_supported_store(const std::string& store, std::string& error) {
  if (store.empty() || store == "ram") {
    return true;
  }
  if (store == "flash") {
    error = "device flash file store is not implemented in this checkpoint";
    return false;
  }
  error = "unknown device file store: " + store;
  return false;
}

bool require_store_arg(
    const X3PackageHost* host,
    X3CallContext* context,
    X3Runtime* runtime,
    const X3Value* args,
    uint32_t argc,
    uint32_t index,
    std::string& store) {
  store = "ram";
  if (argc <= index) {
    return true;
  }
  if (!require_string(host, context, runtime, args[index], "device file store must be dev.ram or dev.flash", store)) {
    return false;
  }
  std::string error;
  if (!require_supported_store(store, error)) {
    host->set_error(context, error.c_str());
    return false;
  }
  return true;
}

bool read_store_selector(
    const X3PackageHost* host,
    X3CallContext* context,
    X3Runtime* runtime,
    const X3Value* args,
    uint32_t argc,
    uint32_t index,
    std::string& store) {
  store = "ram";
  if (argc <= index) {
    return true;
  }
  return require_string(host, context, runtime, args[index], "device file store must be dev.ram or dev.flash", store);
}

void dict_set_owned(
    const X3PackageHost* host,
    X3Runtime* runtime,
    X3Value dict,
    const char* key,
    X3Value value) {
  X3Value key_value = host->value_string(runtime, key);
  host->dict_set_item(runtime, dict, key_value, value);
  host->value_release(key_value);
  host->value_release(value);
}

void dict_set_string(const X3PackageHost* host, X3Runtime* runtime, X3Value dict, const char* key, const std::string& value) {
  dict_set_owned(host, runtime, dict, key, host->value_string(runtime, value.c_str()));
}

void dict_set_bool(const X3PackageHost* host, X3Runtime* runtime, X3Value dict, const char* key, bool value) {
  X3Value key_value = host->value_string(runtime, key);
  X3Value bool_value = x3_value_bool(value ? 1 : 0);
  host->dict_set_item(runtime, dict, key_value, bool_value);
  host->value_release(key_value);
}

void dict_set_uint(const X3PackageHost* host, X3Runtime* runtime, X3Value dict, const char* key, uint64_t value) {
  X3Value key_value = host->value_string(runtime, key);
  X3Value int_value = x3_value_uint64(value);
  host->dict_set_item(runtime, dict, key_value, int_value);
  host->value_release(key_value);
}

void apply_kv_lines(const X3PackageHost* host, X3Runtime* runtime, X3Value dict, const std::vector<std::string>& lines) {
  for (const auto& line : lines) {
    if (line.size() < 4 || line[1] != ' ') {
      continue;
    }
    const char type = line[0];
    const auto key_end = line.find(' ', 2);
    if (key_end == std::string::npos) {
      continue;
    }
    const std::string key = line.substr(2, key_end - 2);
    const std::string value = line.substr(key_end + 1);
    if (type == 'S') {
      dict_set_string(host, runtime, dict, key.c_str(), value);
    } else if (type == 'B') {
      dict_set_bool(host, runtime, dict, key.c_str(), value == "1" || value == "true" || value == "True");
    } else if (type == 'I') {
      try {
        dict_set_uint(host, runtime, dict, key.c_str(), static_cast<uint64_t>(std::stoull(value)));
      } catch (...) {
        dict_set_string(host, runtime, dict, key.c_str(), value);
      }
    }
  }
}

bool read_i2c_payload(
    const X3PackageHost* host,
    X3CallContext* context,
    X3Runtime* runtime,
    X3Value value,
    std::vector<uint8_t>& out) {
  out.clear();
  if (host->value_object_kind(value) == X3_OBJECT_KIND_STRING) {
    const char* text = host->value_to_cstr(runtime, value);
    if (text == nullptr) {
      host->set_error(context, "I2C.write() data string is invalid");
      return false;
    }
    const std::string bytes = text;
    out.assign(bytes.begin(), bytes.end());
    return true;
  }
  if (host->value_object_kind(value) != X3_OBJECT_KIND_LIST &&
      host->value_object_kind(value) != X3_OBJECT_KIND_TUPLE) {
    host->set_error(context, "I2C.write() data must be a list of byte values or a string");
    return false;
  }
  uint64_t length = 0;
  if (host->len(runtime, value, &length) != X3_STATUS_OK) {
    host->set_error(context, "I2C.write() cannot read data length");
    return false;
  }
  out.reserve(static_cast<std::size_t>(length));
  for (uint64_t i = 0; i < length; ++i) {
    X3Value item = x3_value_invalid();
    if (host->get_item(runtime, value, x3_value_uint64(i), &item) != X3_STATUS_OK) {
      host->set_error(context, "I2C.write() cannot read data item");
      return false;
    }
    uint32_t byte = 0;
    const bool ok = require_uint(host, context, item, "I2C.write() byte values must be 0..255", byte);
    host->value_release(item);
    if (!ok || byte > 255) {
      host->set_error(context, "I2C.write() byte values must be 0..255");
      return false;
    }
    out.push_back(static_cast<uint8_t>(byte));
  }
  return true;
}

X3Status device_info(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 1, "Device.info()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }

  X3Value info = host->value_dict(runtime);
  if (info.tag == X3_TAG_INVALID) {
    host->set_error(context, "cannot allocate device info dict");
    return X3_STATUS_ERROR;
  }

  dict_set_string(host, runtime, info, "name", "xlang3-device");
  dict_set_string(host, runtime, info, "kind", handle->kind);
  dict_set_string(host, runtime, info, "transport", handle->transport);
  dict_set_bool(host, runtime, info, "rpc", true);
  dict_set_string(host, runtime, info, "port", handle->port);

  std::vector<std::string> lines;
  std::string error;
  if (handle->transport != "none" && device_kv_request(*handle, "info", lines, error)) {
    apply_kv_lines(host, runtime, info, lines);
  } else if (!error.empty()) {
    dict_set_string(host, runtime, info, "info_error", error);
  }

  *result = info;
  return X3_STATUS_OK;
}

X3Status device_exec(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 2, "Device.exec()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string source;
  if (!require_string(host, context, runtime, args[1], "Device.exec() source must be a string", source)) {
    return X3_STATUS_ERROR;
  }

  std::string output;
  std::string error;
  if (!device_exec_source(*handle, source, output, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  *result = host->value_string(runtime, output.c_str());
  return X3_STATUS_OK;
}

X3Status device_ping_method(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 1, "Device.ping()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  const bool ok = device_ping(*handle, error);
  if (!ok && !error.empty()) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  *result = x3_value_bool(ok ? 1 : 0);
  return X3_STATUS_OK;
}

X3Status device_echo_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 2, "Device.echo()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string data;
  if (!require_string(host, context, runtime, args[1], "Device.echo() data must be a string", data)) {
    return X3_STATUS_ERROR;
  }
  std::string echoed;
  std::string error;
  if (!device_echo_data(*handle, data, echoed, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  *result = host->value_string(runtime, echoed.c_str());
  return X3_STATUS_OK;
}

X3Status device_stats_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 1, "Device.stats()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::vector<std::string> lines;
  std::string error;
  if (!device_kv_request(*handle, "stats", lines, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  X3Value stats = host->value_dict(runtime);
  if (stats.tag == X3_TAG_INVALID) {
    host->set_error(context, "cannot allocate device stats dict");
    return X3_STATUS_ERROR;
  }
  apply_kv_lines(host, runtime, stats, lines);
  *result = stats;
  return X3_STATUS_OK;
}

X3Status device_store_info_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc_range(host, context, argc, 1, 2, "Device.store_info()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string store;
  if (!read_store_selector(host, context, runtime, args, argc, 1, store)) {
    return X3_STATUS_ERROR;
  }
  std::vector<std::string> lines;
  std::string error;
  if (!device_kv_request(*handle, "store_info " + store, lines, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  X3Value info = host->value_dict(runtime);
  if (info.tag == X3_TAG_INVALID) {
    host->set_error(context, "cannot allocate store info dict");
    return X3_STATUS_ERROR;
  }
  apply_kv_lines(host, runtime, info, lines);
  *result = info;
  return X3_STATUS_OK;
}

X3Status device_put_data_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc_range(host, context, argc, 3, 4, "Device.put_data()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string remote_path;
  std::string data;
  std::string store;
  if (!require_string(host, context, runtime, args[1], "Device.put_data() remote path must be a string", remote_path) ||
      !require_string(host, context, runtime, args[2], "Device.put_data() data must be a string", data) ||
      !require_store_arg(host, context, runtime, args, argc, 3, store)) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  if (!device_put_data(*handle, remote_path, data, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status device_get_data_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc_range(host, context, argc, 2, 3, "Device.get_data()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string remote_path;
  std::string store;
  if (!require_string(host, context, runtime, args[1], "Device.get_data() remote path must be a string", remote_path) ||
      !require_store_arg(host, context, runtime, args, argc, 2, store)) {
    return X3_STATUS_ERROR;
  }
  std::string data;
  std::string error;
  if (!device_get_data(*handle, remote_path, data, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  *result = host->value_string(runtime, data.c_str());
  return X3_STATUS_OK;
}

X3Status device_put_file_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc_range(host, context, argc, 3, 4, "Device.put_file()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string remote_path;
  std::string local_path;
  std::string store;
  if (!require_string(host, context, runtime, args[1], "Device.put_file() remote path must be a string", remote_path) ||
      !require_string(host, context, runtime, args[2], "Device.put_file() local path must be a string", local_path) ||
      !require_store_arg(host, context, runtime, args, argc, 3, store)) {
    return X3_STATUS_ERROR;
  }
  std::ifstream file(local_path, std::ios::binary);
  if (!file) {
    const std::string error = "Device.put_file() cannot open local file: " + local_path;
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  std::string error;
  if (!device_put_data(*handle, remote_path, buffer.str(), error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status device_get_file_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc_range(host, context, argc, 3, 4, "Device.get_file()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string remote_path;
  std::string local_path;
  std::string store;
  if (!require_string(host, context, runtime, args[1], "Device.get_file() remote path must be a string", remote_path) ||
      !require_string(host, context, runtime, args[2], "Device.get_file() local path must be a string", local_path) ||
      !require_store_arg(host, context, runtime, args, argc, 3, store)) {
    return X3_STATUS_ERROR;
  }
  std::string data;
  std::string error;
  if (!device_get_data(*handle, remote_path, data, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  std::ofstream file(local_path, std::ios::binary);
  if (!file) {
    const std::string write_error = "Device.get_file() cannot write local file: " + local_path;
    host->set_error(context, write_error.c_str());
    return X3_STATUS_ERROR;
  }
  file.write(data.data(), static_cast<std::streamsize>(data.size()));
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status device_list_files_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc_range(host, context, argc, 2, 3, "Device.list_files()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string path;
  std::string store;
  if (!require_string(host, context, runtime, args[1], "Device.list_files() path must be a string", path) ||
      !require_store_arg(host, context, runtime, args, argc, 2, store)) {
    return X3_STATUS_ERROR;
  }
  std::vector<std::string> entries;
  std::string error;
  if (!device_list_files(*handle, path, entries, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  X3Value list = host->value_list(runtime);
  if (list.tag == X3_TAG_INVALID) {
    host->set_error(context, "cannot allocate file list");
    return X3_STATUS_ERROR;
  }
  for (const auto& entry : entries) {
    X3Value value = host->value_string(runtime, entry.c_str());
    host->list_append(runtime, list, value);
    host->value_release(value);
  }
  *result = list;
  return X3_STATUS_OK;
}

X3Status device_delete_file_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc_range(host, context, argc, 2, 3, "Device.delete_file()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string remote_path;
  std::string store;
  if (!require_string(host, context, runtime, args[1], "Device.delete_file() remote path must be a string", remote_path) ||
      !require_store_arg(host, context, runtime, args, argc, 2, store)) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  if (!device_delete_file(*handle, remote_path, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status i2c_module_bus_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc_range(host, context, argc, 3, 4, "I2C.Bus()")) {
    return X3_STATUS_ERROR;
  }
  auto* module = require_module_proxy(state, context, args[0], kI2CModuleNativeType);
  if (module == nullptr) {
    return X3_STATUS_ERROR;
  }
  uint32_t sda = 0;
  uint32_t scl = 0;
  uint32_t baud = 100000;
  if (!require_uint(host, context, args[1], "I2C.Bus() SDA pin must be an integer", sda) ||
      !require_uint(host, context, args[2], "I2C.Bus() SCL pin must be an integer", scl)) {
    return X3_STATUS_ERROR;
  }
  if (argc == 4 && !require_uint(host, context, args[3], "I2C.Bus() baud must be an integer", baud)) {
    return X3_STATUS_ERROR;
  }

  X3Value instance = host->value_instance(runtime, state->i2c_bus_class);
  if (instance.tag == X3_TAG_INVALID) {
    host->set_error(context, "cannot create I2C bus object");
    return X3_STATUS_ERROR;
  }
  auto* bus = new I2CBusProxy();
  bus->handle = module->handle;
  bus->sda = sda;
  bus->scl = scl;
  bus->baud = baud;
  if (host->instance_set_native_data(instance, kI2CBusNativeType, bus, cleanup_i2c_bus) != X3_STATUS_OK) {
    cleanup_i2c_bus(bus);
    host->value_release(instance);
    host->set_error(context, "cannot attach I2C bus data");
    return X3_STATUS_ERROR;
  }
  *result = instance;
  return X3_STATUS_OK;
}

X3Status i2c_bus_scan_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 1, "I2CBus.scan()")) {
    return X3_STATUS_ERROR;
  }
  auto* bus = require_i2c_bus(state, context, args[0]);
  if (bus == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::vector<uint32_t> addresses;
  std::string error;
  if (!device_i2c_scan(*bus->handle, bus->sda, bus->scl, bus->baud, addresses, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  X3Value list = host->value_list(runtime);
  if (list.tag == X3_TAG_INVALID) {
    host->set_error(context, "cannot allocate I2C scan list");
    return X3_STATUS_ERROR;
  }
  for (uint32_t address : addresses) {
    host->list_append(runtime, list, x3_value_uint64(address));
  }
  *result = list;
  return X3_STATUS_OK;
}

X3Status i2c_bus_write_method(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 3, "I2CBus.write()")) {
    return X3_STATUS_ERROR;
  }
  auto* bus = require_i2c_bus(state, context, args[0]);
  if (bus == nullptr) {
    return X3_STATUS_ERROR;
  }
  uint32_t address = 0;
  if (!require_uint(host, context, args[1], "I2CBus.write() address must be an integer", address)) {
    return X3_STATUS_ERROR;
  }
  std::vector<uint8_t> data;
  if (!read_i2c_payload(host, context, runtime, args[2], data)) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  if (!device_i2c_write(*bus->handle, bus->sda, bus->scl, bus->baud, address, data, error)) {
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status device_import_module(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 2, "Device.import_module()")) {
    return X3_STATUS_ERROR;
  }
  auto* handle = require_device(state, context, args[0]);
  if (handle == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string name;
  if (!require_string(host, context, runtime, args[1], "Device.import_module() module name must be a string", name)) {
    return X3_STATUS_ERROR;
  }
  if (name == "i2c") {
    X3Value module = host->value_instance(runtime, state->i2c_module_class);
    if (module.tag == X3_TAG_INVALID) {
      host->set_error(context, "cannot create I2C module object");
      return X3_STATUS_ERROR;
    }
    auto* proxy = new DeviceModuleProxy();
    proxy->handle = handle;
    if (host->instance_set_native_data(module, kI2CModuleNativeType, proxy, cleanup_proxy) != X3_STATUS_OK) {
      cleanup_proxy(proxy);
      host->value_release(module);
      host->set_error(context, "cannot attach I2C module data");
      return X3_STATUS_ERROR;
    }
    *result = module;
    return X3_STATUS_OK;
  }
  const std::string error = "device module '" + name + "' not found";
  host->set_error(context, error.c_str());
  *result = x3_value_invalid();
  return X3_STATUS_ERROR;
}

X3Status device_connect(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value*,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 0, "device.connect()")) {
    return X3_STATUS_ERROR;
  }

  X3Value instance = host->value_instance(runtime, state->device_class);
  if (instance.tag == X3_TAG_INVALID) {
    host->set_error(context, "cannot create Device instance");
    return X3_STATUS_ERROR;
  }

  auto* handle = new DeviceHandle();
  connect_device(*handle);
  if (host->instance_set_native_data(instance, kDeviceNativeType, handle, cleanup_device) != X3_STATUS_OK) {
    cleanup_device(handle);
    host->value_release(instance);
    host->set_error(context, "cannot attach Device handle");
    return X3_STATUS_ERROR;
  }

  X3Value ram = host->value_string(runtime, "ram");
  X3Value flash = host->value_string(runtime, "flash");
  if (host->set_attr(runtime, instance, "ram", ram) != X3_STATUS_OK ||
      host->set_attr(runtime, instance, "flash", flash) != X3_STATUS_OK) {
    host->value_release(ram);
    host->value_release(flash);
    host->value_release(instance);
    host->set_error(context, "cannot attach Device file store selectors");
    return X3_STATUS_ERROR;
  }
  host->value_release(ram);
  host->value_release(flash);

  *result = instance;
  return X3_STATUS_OK;
}

void add_function(const X3PackageHost* host, X3Module* module, const char* name, X3NativeFn callback, void* user_data) {
  X3NativeFunctionDef def{};
  def.size = sizeof(def);
  def.name = name;
  def.callback = callback;
  def.user_data = user_data;
  host->module_add_function(module, &def);
}

} // namespace

extern "C" X3_DEVICE_EXPORT X3Status x3_package_init(const X3PackageHost* host, X3Package* package) {
  if (host == nullptr || host->abi_version != X3_ABI_VERSION) {
    return X3_STATUS_ERROR;
  }

  auto* state = new DevicePackageState();
  state->host = host;
  host->package_set_cleanup(package, state, [](void* data) {
    auto* state = static_cast<DevicePackageState*>(data);
    if (state->host != nullptr && state->device_class.tag != X3_TAG_INVALID) {
      state->host->value_release(state->device_class);
    }
    if (state->host != nullptr && state->i2c_module_class.tag != X3_TAG_INVALID) {
      state->host->value_release(state->i2c_module_class);
    }
    if (state->host != nullptr && state->i2c_bus_class.tag != X3_TAG_INVALID) {
      state->host->value_release(state->i2c_bus_class);
    }
    delete state;
  });

  host->package_set_metadata(package, "package", "xlang_device");
  host->package_set_metadata(package, "version", "0.1.0");
  host->package_set_metadata(package, "abi", "8");

  X3Module* device = nullptr;
  if (host->add_module(package, "device", &device) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  X3NativeFunctionDef device_methods[] = {
      {sizeof(X3NativeFunctionDef), "info", device_info, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "ping", device_ping_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "echo", device_echo_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "stats", device_stats_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "store_info", device_store_info_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "exec", device_exec, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "put_data", device_put_data_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "get_data", device_get_data_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "put_file", device_put_file_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "get_file", device_get_file_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "list_files", device_list_files_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "delete_file", device_delete_file_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "import_module", device_import_module, state, 0, 0, 0},
  };
  if (host->module_add_class(device, "Device", device_methods, 13, &state->device_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  X3NativeFunctionDef i2c_module_methods[] = {
      {sizeof(X3NativeFunctionDef), "Bus", i2c_module_bus_method, state, 0, 0, 0},
  };
  if (host->module_add_class(device, "I2CModule", i2c_module_methods, 1, &state->i2c_module_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  X3NativeFunctionDef i2c_bus_methods[] = {
      {sizeof(X3NativeFunctionDef), "scan", i2c_bus_scan_method, state, 0, 0, 0},
      {sizeof(X3NativeFunctionDef), "write", i2c_bus_write_method, state, 0, 0, 0},
  };
  if (host->module_add_class(device, "I2CBus", i2c_bus_methods, 2, &state->i2c_bus_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  add_function(host, device, "connect", device_connect, state);
  return X3_STATUS_OK;
}
