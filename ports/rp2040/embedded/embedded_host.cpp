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
#include "embedded/frozen_app.h"
#include "embedded/static_modules.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdio.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace xlang3::pico {
namespace {

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

} // namespace

EmbeddedHost::EmbeddedHost()
    : runtime_(xlang3::OutputSink{this, EmbeddedHost::output_write}),
      interpreter_(runtime_),
      file_store_(memory_files_.as_file_store()) {
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

void EmbeddedHost::process_rpc_line(const char* line) {
  ++rpc_requests_;
  std::string command(line == nullptr ? "" : line);
  while (!command.empty() &&
         (command.back() == '\r' || command.back() == '\n' || command.back() == ' ' || command.back() == '\t')) {
    command.pop_back();
  }
  line = command.c_str();

  constexpr const char* kPythonPrefix = "py ";
  constexpr std::size_t kPythonPrefixSize = 3;
  if (std::strncmp(line, kPythonPrefix, kPythonPrefixSize) == 0) {
    execute_python_source(line + kPythonPrefixSize);
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

  auto result = interpreter_.run(lowered.module);
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
  std::string path;
  const char* data_text = split_token(line, path);
  std::vector<uint8_t> data;
  if (path.empty() || !base64_decode(data_text, data)) {
    std::printf("ERR put: expected path and base64 data\nEND\n");
    return;
  }
  std::string error;
  if (!file_store_.put(file_store_.context, path.c_str(), data.data(), static_cast<uint32_t>(data.size()), error)) {
    std::printf("ERR put: %s\nEND\n", error.c_str());
    return;
  }
  std::printf("OK\nEND\n");
}

void EmbeddedHost::get_file(const char* line) {
  std::string path;
  split_token(line, path);
  std::vector<uint8_t> data;
  std::string error;
  if (!file_store_.get(file_store_.context, path.c_str(), data, error)) {
    std::printf("ERR get: %s\nEND\n", error.c_str());
    return;
  }
  const std::string encoded = base64_encode(data.data(), data.size());
  std::printf("DATA %s\nOK\nEND\n", encoded.c_str());
}

void EmbeddedHost::list_files(const char* line) {
  std::string path;
  split_token(line, path);
  std::vector<std::string> entries;
  std::string error;
  if (!file_store_.list(file_store_.context, path.c_str(), entries, error)) {
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
  std::string path;
  split_token(line, path);
  std::string error;
  if (!file_store_.remove(file_store_.context, path.c_str(), error)) {
    std::printf("ERR delete: %s\nEND\n", error.c_str());
    return;
  }
  std::printf("OK\nEND\n");
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
  std::printf("S last_error \n");
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
    std::printf("B implemented 0\n");
    std::printf("B persistent 1\n");
    std::printf("I files 0\n");
    std::printf("I used 0\n");
#if defined(PICO_FLASH_SIZE_BYTES)
    std::printf("I capacity %u\n", static_cast<unsigned>(PICO_FLASH_SIZE_BYTES));
#else
    std::printf("I capacity 2097152\n");
#endif
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
    const int rc = i2c_write_blocking(i2c0, address, &dummy, 1, false);
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
  const int rc = i2c_write_blocking(i2c0, static_cast<uint8_t>(address), data.data(), data.size(), false);
  if (rc < 0) {
    std::printf("ERR i2c_write: device did not acknowledge\nEND\n");
    return;
  }
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

  constexpr uint32_t kBlinkMilliseconds = 1000;
  absolute_time_t next_blink = make_timeout_time_ms(kBlinkMilliseconds);
  char line[4096] = {};
  std::size_t line_size = 0;

  while (true) {
    const int ch = getchar_timeout_us(0);
    if (ch != PICO_ERROR_TIMEOUT) {
      if (ch == '\r' || ch == '\n') {
        if (line_size > 0) {
          line[line_size] = '\0';
          process_rpc_line(line);
          line_size = 0;
        }
      } else if (line_size + 1 < sizeof(line)) {
        line[line_size++] = static_cast<char>(ch);
      }
    }

    if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
      set_external_led(!external_led_on_);
      next_blink = make_timeout_time_ms(kBlinkMilliseconds);
    }
    board::sleep_milliseconds(1);
  }
}

} // namespace xlang3::pico
