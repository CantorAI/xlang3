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
#pragma once

#include "xlang3/interpreter.h"
#include "xlang3/ir.h"
#include "xlang3/rpc/file_store.h"
#include "xlang3/rpc/memory_file_store.h"
#include "xlang3/runtime.h"

#include "embedded/flash_file_store.h"

#include <cstdint>
#include <string>
#include <vector>

#include "pico/time.h"

namespace xlang3::pico {

class EmbeddedHost {
public:
  EmbeddedHost();
  void run();

private:
  bool external_led_on_ = false;
  std::string output_;
  xlang3::Runtime runtime_;
  xlang3::Interpreter interpreter_;
  xlang3::rpc::MemoryFileStore memory_files_;
  FlashFileStore flash_files_;
  xlang3::rpc::FileStore file_store_;
  xlang3::rpc::FileStore flash_store_;
  std::string last_error_;
  uint64_t rpc_requests_ = 0;
  bool runtime_busy_ = false;
  bool runtime_core_started_ = false;
  volatile uint32_t runtime_state_ = 0;
  bool servicing_rpc_ = false;
  char rpc_line_[16384] = {};
  std::size_t rpc_line_size_ = 0;
  bool rpc_line_overflow_ = false;
  absolute_time_t rpc_line_deadline_ = nil_time;

  static void output_write(void* context, const char* data, std::size_t size);
  static void service_callback(void* context);
  static void core1_trampoline();

  void set_external_led(bool on);
  void start_runtime_core();
  void run_runtime_core();
  bool has_autorun_main();
  const char* runtime_state_text() const;
  void service_rpc_once();
  void process_rpc_line(const char* line);
  bool process_busy_rpc_line(const char* line);
  void execute_python_source(const char* source);
  void put_file(const char* line);
  void get_file(const char* line);
  void list_files(const char* line);
  void delete_file(const char* line);
  void auto_run_main();
  bool cache_python_ir(const std::string& path, const std::vector<uint8_t>& source, std::string& error);
  bool run_cached_python(const std::string& path, const std::vector<uint8_t>& source, std::string& error);
  void run_ir_module(const xlang3::ir::Module& module);
  void echo(const char* line);
  void info();
  void stats();
  void store_info(const char* line);
  void init_i2c_bus(uint32_t sda, uint32_t scl, uint32_t baud);
  void i2c_scan(const char* line);
  void i2c_write(const char* line);
  void gpio_config(const char* line);
  void gpio_read(const char* line);
  void gpio_write(const char* line);
  void gpio_wait_edge(const char* line);
};

} // namespace xlang3::pico
