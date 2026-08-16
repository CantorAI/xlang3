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
#include "embedded/embedded_host.h"

#include "board/board_config.h"
#include "board/console.h"
#include "board/gpio.h"
#include "board/time.h"
#include "embedded/device_file_system.h"
#include "embedded/embedded_modules.h"
#include "embedded/frozen_app.h"
#include "embedded/gpio_irq.h"
#include "embedded/static_modules.h"
#include "xlang3/ir_codec.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdio.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace xlang3::pico {
namespace {

constexpr uint kI2CTimeoutUs = 1000;
constexpr uint32_t kSafeBootPin = 22;

enum RuntimeState : uint32_t {
  kRuntimeStopped = 0,
  kRuntimeStarting = 1,
  kRuntimeRunning = 2,
  kRuntimeError = 3,
};

EmbeddedHost* g_core1_host = nullptr;

constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

bool base64_decode(const char* text, std::vector<uint8_t>& out) {
  out.clear();
  int values[4] = {};
  int count = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p == '=') {
      values[count++] = -2;
    } else {
      const int value = base64_value(*p);
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

const char* split_token(const char* text, std::string& token) {
  while (*text == ' ') ++text;
  const char* start = text;
  while (*text != '\0' && *text != ' ') ++text;
  token.assign(start, text - start);
  while (*text == ' ') ++text;
  return text;
}

bool parse_uint_token(const char*& text, uint32_t& value) {
  std::string token;
  text = split_token(text, token);
  if (token.empty()) {
    return false;
  }
  uint32_t parsed = 0;
  for (char ch : token) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed = parsed * 10u + static_cast<uint32_t>(ch - '0');
  }
  value = parsed;
  return true;
}

const char* split_store_and_path(const char* line, std::string& store, std::string& path) {
  std::string first;
  const char* rest = split_token(line, first);
  if (first == "ram" || first == "flash") {
    store = first;
    return split_token(rest, path);
  }
  store = "ram";
  path = first;
  return rest;
}

bool ends_with(const std::string& text, const char* suffix) {
  const std::size_t suffix_size = std::strlen(suffix);
  return text.size() >= suffix_size &&
         text.compare(text.size() - suffix_size, suffix_size, suffix) == 0;
}

std::string normalize_device_path(const std::string& raw) {
  if (raw.empty() || raw[0] == '/') {
    return raw.empty() ? "/" : raw;
  }
  return "/" + raw;
}

std::string ir_cache_path_for(const std::string& path) {
  return "/.xlang3/ir" + normalize_device_path(path) + ".x3ir";
}

std::string source_text_from_bytes(const std::vector<uint8_t>& data) {
  return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

bool should_skip_autorun_for_safe_boot() {
  std::string error;
  if (!gpio_configure(kSafeBootPin, kGpioModeIn, kGpioPullUp, error)) {
    return false;
  }
  board::sleep_milliseconds(20);
  uint32_t value = 1;
  if (!gpio_read_pin(kSafeBootPin, value, error)) {
    return false;
  }
  return value == 0;
}

} // namespace

EmbeddedHost::EmbeddedHost()
    : runtime_(xlang3::OutputSink{this, EmbeddedHost::output_write}),
      interpreter_(runtime_),
      file_store_(memory_files_.as_file_store()),
      flash_store_(flash_files_.as_file_store()) {
  if (!flash_files_.mount(last_error_)) {
    last_error_ = "flash mount: " + last_error_;
  }
  runtime_.vfs().set_root(std::make_unique<DeviceFileSystem>(&file_store_, &flash_store_));
  register_embedded_modules(runtime_);
}

void EmbeddedHost::output_write(void* context, const char* data, std::size_t size) {
  auto* host = static_cast<EmbeddedHost*>(context);
  if (host == nullptr || data == nullptr || size == 0) {
    return;
  }
  host->output_.append(data, size);
}

void EmbeddedHost::set_external_led(bool on) {
  external_led_on_ = on;
  board::write_pin(board::kExternalLedPin, on);
  if (board::kHasBoardLed) {
    board::write_pin(board::kBoardLedPin, on);
  }
}

void EmbeddedHost::service_callback(void* context) {
  auto* host = static_cast<EmbeddedHost*>(context);
  if (host != nullptr) {
    host->service_rpc_once();
  }
}

void EmbeddedHost::core1_trampoline() {
  if (g_core1_host != nullptr) {
    g_core1_host->run_runtime_core();
  }
  while (true) {
    board::sleep_milliseconds(1000);
  }
}

void EmbeddedHost::start_runtime_core() {
  if (runtime_core_started_) {
    return;
  }
  runtime_core_started_ = true;
  runtime_state_ = kRuntimeStarting;
  g_core1_host = this;
  multicore_launch_core1(EmbeddedHost::core1_trampoline);
}

void EmbeddedHost::run_runtime_core() {
  runtime_state_ = kRuntimeRunning;
  auto_run_main();
  if (runtime_state_ == kRuntimeRunning) {
    runtime_state_ = kRuntimeStopped;
  }
  while (true) {
    board::sleep_milliseconds(1000);
  }
}

const char* EmbeddedHost::runtime_state_text() const {
  switch (runtime_state_) {
    case kRuntimeStarting:
      return "starting";
    case kRuntimeRunning:
      return "running";
    case kRuntimeError:
      return "error";
    case kRuntimeStopped:
    default:
      return "stopped";
  }
}

void EmbeddedHost::service_rpc_once() {
  if (servicing_rpc_) {
    return;
  }
  if (rpc_line_size_ > 0 && !is_nil_time(rpc_line_deadline_) &&
      absolute_time_diff_us(get_absolute_time(), rpc_line_deadline_) <= 0) {
    rpc_line_size_ = 0;
    rpc_line_overflow_ = false;
    rpc_line_deadline_ = nil_time;
    std::printf("ERR protocol: partial line timeout\nEND\n");
  }
  const int ch = getchar_timeout_us(0);
  if (ch == PICO_ERROR_TIMEOUT) {
    return;
  }
  if (ch == '\r' || ch == '\n') {
    if (rpc_line_size_ > 0) {
      rpc_line_[rpc_line_size_] = '\0';
      servicing_rpc_ = true;
      if (rpc_line_overflow_) {
        std::printf("ERR protocol: line too long\nEND\n");
      } else {
        process_rpc_line(rpc_line_);
      }
      servicing_rpc_ = false;
      rpc_line_size_ = 0;
      rpc_line_overflow_ = false;
      rpc_line_deadline_ = nil_time;
    }
    return;
  }
  if (rpc_line_size_ + 1 < sizeof(rpc_line_)) {
    rpc_line_[rpc_line_size_++] = static_cast<char>(ch);
    rpc_line_deadline_ = make_timeout_time_ms(250);
  } else {
    rpc_line_overflow_ = true;
    rpc_line_deadline_ = make_timeout_time_ms(250);
  }
}

bool EmbeddedHost::process_busy_rpc_line(const char* line) {
  if (!runtime_busy_ && !runtime_core_started_) {
    return false;
  }
  if (std::strncmp(line, "py ", 3) == 0 ||
      std::strncmp(line, "pyb ", 4) == 0 ||
      std::strncmp(line, "put flash ", 10) == 0 ||
      std::strncmp(line, "delete flash ", 13) == 0 ||
      std::strncmp(line, "gpio_wait_edge ", 15) == 0) {
    std::printf("ERR runtime busy: command cannot run while main.py is active\nEND\n");
    return true;
  }
  return false;
}

void EmbeddedHost::process_rpc_line(const char* line) {
  ++rpc_requests_;
  std::string command(line == nullptr ? "" : line);
  while (!command.empty() &&
         (command.back() == '\r' || command.back() == '\n' || command.back() == ' ' || command.back() == '\t')) {
    command.pop_back();
  }
  line = command.c_str();
  constexpr const char* kRpcEnvelopePrefix = "rpc ";
  constexpr std::size_t kRpcEnvelopePrefixSize = 4;
  if (std::strncmp(line, kRpcEnvelopePrefix, kRpcEnvelopePrefixSize) == 0) {
    std::string request_id;
    const char* inner = split_token(line + kRpcEnvelopePrefixSize, request_id);
    if (request_id.empty() || *inner == '\0') {
      std::printf("ERR protocol: malformed rpc envelope\nEND\n");
      return;
    }
    std::printf("RPC %s BEGIN\n", request_id.c_str());
    process_rpc_line(inner);
    std::printf("RPC %s END\n", request_id.c_str());
    return;
  }
  if (process_busy_rpc_line(line)) {
    return;
  }

  constexpr const char* kPythonPrefix = "py ";
  constexpr std::size_t kPythonPrefixSize = 3;
  if (std::strncmp(line, kPythonPrefix, kPythonPrefixSize) == 0) {
    execute_python_source(line + kPythonPrefixSize);
    return;
  }
  constexpr const char* kPythonBase64Prefix = "pyb ";
  constexpr std::size_t kPythonBase64PrefixSize = 4;
  if (std::strncmp(line, kPythonBase64Prefix, kPythonBase64PrefixSize) == 0) {
    std::vector<uint8_t> data;
    if (!base64_decode(line + kPythonBase64PrefixSize, data)) {
      std::printf("ERR pyb: expected base64 source\nEND\n");
      return;
    }
    data.push_back(0);
    execute_python_source(reinterpret_cast<const char*>(data.data()));
    return;
  }
  if (std::strcmp(line, "ping") == 0) {
    std::printf("PONG\nOK\nEND\n");
    return;
  }
  if (std::strncmp(line, "echo ", 5) == 0) {
    echo(line + 5);
    return;
  }
  if (std::strcmp(line, "info") == 0) {
    info();
    return;
  }
  if (std::strcmp(line, "stats") == 0) {
    stats();
    return;
  }
  if (std::strcmp(line, "runtime_status") == 0) {
    std::printf("S runtime_state %s\n", runtime_state_text());
    std::printf("B runtime_busy %u\n", runtime_busy_ ? 1u : 0u);
    std::printf("S last_error %s\n", last_error_.c_str());
    std::printf("OK\nEND\n");
    return;
  }
  if (std::strcmp(line, "reset") == 0) {
    std::printf("OK\nEND\n");
    board::sleep_milliseconds(20);
    watchdog_reboot(0, 0, 0);
    return;
  }
  if (std::strncmp(line, "store_info ", 11) == 0) {
    store_info(line + 11);
    return;
  }
  if (std::strncmp(line, "put ", 4) == 0) {
    put_file(line + 4);
    return;
  }
  if (std::strncmp(line, "get ", 4) == 0) {
    get_file(line + 4);
    return;
  }
  if (std::strncmp(line, "list ", 5) == 0) {
    list_files(line + 5);
    return;
  }
  if (std::strncmp(line, "delete ", 7) == 0) {
    delete_file(line + 7);
    return;
  }
  if (std::strncmp(line, "i2c_scan ", 9) == 0) {
    i2c_scan(line + 9);
    return;
  }
  if (std::strncmp(line, "i2c_write ", 10) == 0) {
    i2c_write(line + 10);
    return;
  }
  if (std::strncmp(line, "gpio_config ", 12) == 0) {
    gpio_config(line + 12);
    return;
  }
  if (std::strncmp(line, "gpio_read ", 10) == 0) {
    gpio_read(line + 10);
    return;
  }
  if (std::strncmp(line, "gpio_write ", 11) == 0) {
    gpio_write(line + 11);
    return;
  }
  if (std::strncmp(line, "gpio_wait_edge ", 15) == 0) {
    gpio_wait_edge(line + 15);
    return;
  }

  std::printf("ERR protocol: unknown command\n");
  std::printf("END\n");
}

void EmbeddedHost::execute_python_source(const char* source) {
  output_.clear();

  auto parsed = xlang3::parse_source(source);
  if (!parsed.errors.empty()) {
    for (const auto& error : parsed.errors) {
      std::printf("ERR parse: %s\n", error.c_str());
    }
    std::printf("END\n");
    return;
  }

  auto lowered = xlang3::lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    for (const auto& error : lowered.errors) {
      std::printf("ERR lower: %s\n", error.c_str());
    }
    std::printf("END\n");
    return;
  }

  runtime_busy_ = true;
  auto result = interpreter_.run(lowered.module);
  runtime_busy_ = false;
  if (!result.errors.empty()) {
    for (const auto& error : result.errors) {
      std::printf("ERR runtime: %s\n", error.c_str());
    }
    std::printf("END\n");
    return;
  }

  const std::string& text = output_;
  if (!text.empty()) {
    std::printf("%s", text.c_str());
    if (text.back() != '\n') {
      std::printf("\n");
    }
  }
  std::printf("OK\n");
  std::printf("END\n");
}

void EmbeddedHost::put_file(const char* line) {
  std::string store;
  std::string path;
  const char* data_text = split_store_and_path(line, store, path);
  std::vector<uint8_t> data;
  if (path.empty() || !base64_decode(data_text, data)) {
    std::printf("ERR put: expected path and base64 data\nEND\n");
    return;
  }
  std::string error;
  auto& target = store == "flash" ? flash_store_ : file_store_;
  if (!target.put(target.context, path.c_str(), data.data(), static_cast<uint32_t>(data.size()), error)) {
    last_error_ = error;
    std::printf("ERR put: %s\nEND\n", error.c_str());
    return;
  }
  std::printf("OK\nEND\n");
}

void EmbeddedHost::get_file(const char* line) {
  std::string store;
  std::string path;
  split_store_and_path(line, store, path);
  std::vector<uint8_t> data;
  std::string error;
  auto& target = store == "flash" ? flash_store_ : file_store_;
  if (!target.get(target.context, path.c_str(), data, error)) {
    last_error_ = error;
    std::printf("ERR get: %s\nEND\n", error.c_str());
    return;
  }
  const std::string encoded = base64_encode(data.data(), data.size());
  std::printf("DATA %s\nOK\nEND\n", encoded.c_str());
}

void EmbeddedHost::list_files(const char* line) {
  std::string store;
  std::string path;
  split_store_and_path(line, store, path);
  std::vector<std::string> entries;
  std::string error;
  auto& target = store == "flash" ? flash_store_ : file_store_;
  if (!target.list(target.context, path.c_str(), entries, error)) {
    last_error_ = error;
    std::printf("ERR list: %s\nEND\n", error.c_str());
    return;
  }
  for (const auto& entry : entries) {
    const auto encoded = base64_encode(reinterpret_cast<const uint8_t*>(entry.data()), entry.size());
    std::printf("ITEM %s\n", encoded.c_str());
  }
  std::printf("OK\nEND\n");
}

void EmbeddedHost::delete_file(const char* line) {
  std::string store;
  std::string path;
  split_store_and_path(line, store, path);
  std::string error;
  auto& target = store == "flash" ? flash_store_ : file_store_;
  if (!target.remove(target.context, path.c_str(), error)) {
    last_error_ = error;
    std::printf("ERR delete: %s\nEND\n", error.c_str());
    return;
  }
  if (store == "flash" && ends_with(path, ".py")) {
    const std::string ir_path = ir_cache_path_for(path);
    std::string cache_error;
    runtime_.vfs().remove(ir_path, cache_error);
  }
  std::printf("OK\nEND\n");
}

void EmbeddedHost::auto_run_main() {
  std::vector<uint8_t> data;
  std::string error;
  if (!runtime_.vfs().read_file("/main.py", data, error)) {
    return;
  }
  board::write_line("xlang3 flash autorun: /main.py");
  data.push_back(0);
  execute_python_source(reinterpret_cast<const char*>(data.data()));
}

bool EmbeddedHost::has_autorun_main() {
  VfsStat stat;
  std::string error;
  return runtime_.vfs().stat("/main.py", stat, error) && stat.kind == VfsNodeKind::File;
}

bool EmbeddedHost::cache_python_ir(const std::string& path, const std::vector<uint8_t>& source, std::string& error) {
  const std::string source_text = source_text_from_bytes(source);
  auto parsed = xlang3::parse_source(source_text.c_str());
  if (!parsed.errors.empty()) {
    error = "IR cache parse: " + parsed.errors.front();
    return false;
  }
  auto lowered = xlang3::lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    error = "IR cache lower: " + lowered.errors.front();
    return false;
  }

  ir::EncodedModule encoded;
  const uint64_t hash = ir::source_hash64(source.data(), source.size());
  if (!ir::encode_module(lowered.module, hash, encoded, error)) {
    error = "IR cache encode: " + error;
    return false;
  }

  const std::string cache_path = ir_cache_path_for(path);
  if (!runtime_.vfs().write_file(
          cache_path,
          encoded.bytes.data(),
          encoded.bytes.size(),
          error)) {
    error = "IR cache write: " + error;
    return false;
  }
  return true;
}

bool EmbeddedHost::run_cached_python(const std::string& path, const std::vector<uint8_t>& source, std::string& error) {
  const std::string cache_path = ir_cache_path_for(path);
  const uint64_t hash = ir::source_hash64(source.data(), source.size());

  std::vector<uint8_t> encoded;
  if (runtime_.vfs().read_file(cache_path, encoded, error)) {
    ir::Module module;
    if (ir::decode_module(encoded.data(), encoded.size(), hash, module, error)) {
      run_ir_module(module);
      return true;
    }
  }

  if (!cache_python_ir(path, source, error)) {
    return false;
  }
  if (!runtime_.vfs().read_file(cache_path, encoded, error)) {
    return false;
  }
  ir::Module module;
  if (!ir::decode_module(encoded.data(), encoded.size(), hash, module, error)) {
    return false;
  }
  run_ir_module(module);
  return true;
}

void EmbeddedHost::run_ir_module(const xlang3::ir::Module& module) {
  output_.clear();
  runtime_busy_ = true;
  auto result = interpreter_.run(module);
  runtime_busy_ = false;
  if (!result.errors.empty()) {
    runtime_state_ = kRuntimeError;
    last_error_ = result.errors.front();
    for (const auto& error : result.errors) {
      std::printf("ERR runtime: %s\n", error.c_str());
    }
    std::printf("END\n");
    return;
  }

  const std::string& text = output_;
  if (!text.empty()) {
    std::printf("%s", text.c_str());
    if (text.back() != '\n') {
      std::printf("\n");
    }
  }
  std::printf("OK\n");
  std::printf("END\n");
}

void EmbeddedHost::echo(const char* line) {
  std::vector<uint8_t> data;
  if (!base64_decode(line, data)) {
    std::printf("ERR echo: expected base64 data\nEND\n");
    return;
  }
  const std::string encoded = base64_encode(data.data(), data.size());
  std::printf("DATA %s\nOK\nEND\n", encoded.c_str());
}

void EmbeddedHost::info() {
  std::printf("S name rp2040-pico\n");
  std::printf("S runtime xlang3\n");
  std::printf("I protocol 1\n");
  std::printf("S cpu rp2040\n");
  std::printf("I cores 2\n");
  std::printf("I clock_hz %u\n", static_cast<unsigned>(clock_get_hz(clk_sys)));
  std::printf("I ram_bytes 270336\n");
#if defined(PICO_FLASH_SIZE_BYTES)
  std::printf("I flash_bytes %u\n", static_cast<unsigned>(PICO_FLASH_SIZE_BYTES));
#else
  std::printf("I flash_bytes 2097152\n");
#endif
  std::printf("S stores ram,flash\n");
  std::printf("S modules gpio,time,console,i2c\n");
  std::printf("OK\nEND\n");
}

void EmbeddedHost::stats() {
  std::printf("I uptime_ms %llu\n", static_cast<unsigned long long>(to_ms_since_boot(get_absolute_time())));
  std::printf("I rpc_requests %llu\n", static_cast<unsigned long long>(rpc_requests_));
  std::printf("I ram_store_files %u\n", static_cast<unsigned>(memory_files_.file_count()));
  std::printf("I ram_store_bytes %u\n", static_cast<unsigned>(memory_files_.byte_count()));
  std::printf("I flash_store_files %u\n", static_cast<unsigned>(flash_files_.file_count()));
  std::printf("I flash_store_bytes %u\n", static_cast<unsigned>(flash_files_.byte_count()));
  std::printf("I flash_store_used %u\n", static_cast<unsigned>(flash_files_.used_bytes()));
  std::printf("S runtime_state %s\n", runtime_state_text());
  std::printf("B runtime_busy %u\n", runtime_busy_ ? 1u : 0u);
  std::printf("S last_error %s\n", last_error_.c_str());
  std::printf("OK\nEND\n");
}

void EmbeddedHost::store_info(const char* line) {
  std::string store;
  split_token(line, store);
  if (store.empty() || store == "ram") {
    std::printf("S name ram\n");
    std::printf("B implemented 1\n");
    std::printf("B persistent 0\n");
    std::printf("I files %u\n", static_cast<unsigned>(memory_files_.file_count()));
    std::printf("I used %u\n", static_cast<unsigned>(memory_files_.byte_count()));
    std::printf("I capacity 0\n");
    std::printf("OK\nEND\n");
    return;
  }
  if (store == "flash") {
    std::printf("S name flash\n");
    std::printf("B implemented %u\n", flash_files_.implemented() ? 1u : 0u);
    std::printf("B persistent 1\n");
    std::printf("I files %u\n", static_cast<unsigned>(flash_files_.file_count()));
    std::printf("I bytes %u\n", static_cast<unsigned>(flash_files_.byte_count()));
    std::printf("I used %u\n", static_cast<unsigned>(flash_files_.used_bytes()));
    std::printf("I base %u\n", static_cast<unsigned>(flash_files_.base_offset()));
    std::printf("I capacity %u\n", static_cast<unsigned>(flash_files_.capacity()));
    std::printf("OK\nEND\n");
    return;
  }
  std::printf("ERR store_info: unknown store\nEND\n");
}

void EmbeddedHost::init_i2c_bus(uint32_t sda, uint32_t scl, uint32_t baud) {
  i2c_init(i2c0, baud == 0 ? 100000 : baud);
  gpio_set_function(sda, GPIO_FUNC_I2C);
  gpio_set_function(scl, GPIO_FUNC_I2C);
  gpio_pull_up(sda);
  gpio_pull_up(scl);
}

void EmbeddedHost::i2c_scan(const char* line) {
  const char* cursor = line;
  uint32_t sda = 0;
  uint32_t scl = 0;
  uint32_t baud = 100000;
  if (!parse_uint_token(cursor, sda) || !parse_uint_token(cursor, scl)) {
    std::printf("ERR i2c_scan: expected sda scl [baud]\nEND\n");
    return;
  }
  uint32_t parsed_baud = 0;
  if (parse_uint_token(cursor, parsed_baud)) {
    baud = parsed_baud;
  }
  init_i2c_bus(sda, scl, baud);
  for (uint8_t address = 1; address < 0x7f; ++address) {
    uint8_t dummy = 0;
    const int rc = i2c_write_timeout_us(i2c0, address, &dummy, 1, false, kI2CTimeoutUs);
    if (rc >= 0) {
      std::printf("ADDR %u\n", static_cast<unsigned>(address));
    }
  }
  std::printf("OK\nEND\n");
}

void EmbeddedHost::i2c_write(const char* line) {
  const char* cursor = line;
  uint32_t sda = 0;
  uint32_t scl = 0;
  uint32_t baud = 100000;
  uint32_t address = 0;
  if (!parse_uint_token(cursor, sda) ||
      !parse_uint_token(cursor, scl) ||
      !parse_uint_token(cursor, baud) ||
      !parse_uint_token(cursor, address)) {
    std::printf("ERR i2c_write: expected sda scl baud address base64\nEND\n");
    return;
  }
  while (*cursor == ' ') ++cursor;
  std::vector<uint8_t> data;
  if (address == 0 || address >= 0x80 || !base64_decode(cursor, data)) {
    std::printf("ERR i2c_write: invalid address or base64 data\nEND\n");
    return;
  }
  init_i2c_bus(sda, scl, baud);
  const int rc = i2c_write_timeout_us(
      i2c0,
      static_cast<uint8_t>(address),
      data.data(),
      data.size(),
      false,
      kI2CTimeoutUs);
  if (rc < 0) {
    std::printf("ERR i2c_write: device did not acknowledge\nEND\n");
    return;
  }
  std::printf("OK\nEND\n");
}

void EmbeddedHost::gpio_config(const char* line) {
  const char* cursor = line;
  uint32_t pin = 0;
  uint32_t mode = 0;
  uint32_t pull = 0;
  if (!parse_uint_token(cursor, pin) || !parse_uint_token(cursor, mode)) {
    std::printf("ERR gpio_config: expected pin mode [pull]\nEND\n");
    return;
  }
  parse_uint_token(cursor, pull);
  if (pin >= kMaxGpioPins) {
    std::printf("ERR gpio_config: invalid pin\nEND\n");
    return;
  }
  std::string error;
  if (!gpio_configure(pin, mode, pull, error)) {
    std::printf("ERR gpio_config: %s\nEND\n", error.c_str());
    return;
  }
  std::printf("OK\nEND\n");
}

void EmbeddedHost::gpio_read(const char* line) {
  const char* cursor = line;
  uint32_t pin = 0;
  if (!parse_uint_token(cursor, pin) || pin >= kMaxGpioPins) {
    std::printf("ERR gpio_read: expected valid pin\nEND\n");
    return;
  }
  uint32_t value = 0;
  std::string error;
  if (!gpio_read_pin(pin, value, error)) {
    std::printf("ERR gpio_read: %s\nEND\n", error.c_str());
    return;
  }
  std::printf("I value %u\nOK\nEND\n", value);
}

void EmbeddedHost::gpio_write(const char* line) {
  const char* cursor = line;
  uint32_t pin = 0;
  uint32_t value = 0;
  if (!parse_uint_token(cursor, pin) || !parse_uint_token(cursor, value) || pin >= kMaxGpioPins) {
    std::printf("ERR gpio_write: expected pin value\nEND\n");
    return;
  }
  std::string error;
  if (!gpio_write_pin(pin, value, error)) {
    std::printf("ERR gpio_write: %s\nEND\n", error.c_str());
    return;
  }
  std::printf("OK\nEND\n");
}

void EmbeddedHost::gpio_wait_edge(const char* line) {
  const char* cursor = line;
  uint32_t pin = 0;
  uint32_t edge = kGpioEdgeFalling;
  uint32_t timeout_ms = 10000;
  if (!parse_uint_token(cursor, pin) || !parse_uint_token(cursor, edge)) {
    std::printf("ERR gpio_wait_edge: expected pin edge [timeout_ms]\nEND\n");
    return;
  }
  parse_uint_token(cursor, timeout_ms);
  if (pin >= kMaxGpioPins) {
    std::printf("ERR gpio_wait_edge: invalid pin\nEND\n");
    return;
  }

  GpioWaitResult result;
  std::string error;
  if (!gpio_wait_for_edge(pin, edge, timeout_ms, result, error)) {
    std::printf("ERR gpio_wait_edge: %s\nEND\n", error.c_str());
    return;
  }
  std::printf("B triggered %u\n", result.triggered ? 1u : 0u);
  std::printf("I pin %u\n", static_cast<unsigned>(result.pin));
  std::printf("I events %u\n", static_cast<unsigned>(result.events));
  std::printf("I sequence %u\n", static_cast<unsigned>(result.sequence));
  std::printf("I overflow %u\n", static_cast<unsigned>(result.overflow));
  std::printf("I value %u\n", static_cast<unsigned>(result.value));
  std::printf("I elapsed_ms %llu\n", static_cast<unsigned long long>(result.elapsed_ms));
  std::printf("OK\nEND\n");
}

void EmbeddedHost::run() {
  const FrozenApp& app = get_frozen_app();

  board::write_line("xlang3 rp2040 embedded host");
  board::write_line("xlang3 rp2040 rpc server ready");
  std::printf("frozen app: %s (%u bytes)\n", app.name, static_cast<unsigned>(app.source_size));

  std::size_t module_count = 0;
  const StaticModuleDef* modules = static_modules(module_count);
  for (std::size_t i = 0; i < module_count; ++i) {
    std::printf("static module: %s\n", modules[i].name);
  }

  board::init_output_pin(board::kExternalLedPin);
  if (board::kHasBoardLed) {
    board::init_output_pin(board::kBoardLedPin);
  }

#if defined(XLANG3_FORCE_SAFE_BOOT) && XLANG3_FORCE_SAFE_BOOT
  runtime_state_ = kRuntimeStopped;
  board::write_line("xlang3 flash autorun skipped: forced safe boot");
#else
  if (should_skip_autorun_for_safe_boot()) {
    runtime_state_ = kRuntimeStopped;
    board::write_line("xlang3 flash autorun skipped: GP22 held low");
  } else if (has_autorun_main()) {
    start_runtime_core();
  } else {
    runtime_state_ = kRuntimeStopped;
    board::write_line("xlang3 flash autorun: no /main.py");
  }
#endif

  constexpr uint32_t kBlinkMilliseconds = 1000;
  absolute_time_t next_blink = make_timeout_time_ms(kBlinkMilliseconds);

  while (true) {
    service_rpc_once();

    if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
      set_external_led(!external_led_on_);
      next_blink = make_timeout_time_ms(kBlinkMilliseconds);
    }
    board::sleep_milliseconds(1);
  }
}

} // namespace xlang3::pico
