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
#include "xlang3/rpc/file_store.h"
#include "xlang3/rpc/memory_file_store.h"
#include "xlang3/runtime.h"

#include <cstdint>
#include <string>

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
  xlang3::rpc::FileStore file_store_;
  uint64_t rpc_requests_ = 0;

  static void output_write(void* context, const char* data, std::size_t size);

  void set_external_led(bool on);
  void process_rpc_line(const char* line);
  void execute_python_source(const char* source);
  void put_file(const char* line);
  void get_file(const char* line);
  void list_files(const char* line);
  void delete_file(const char* line);
  void echo(const char* line);
  void info();
  void stats();
  void store_info(const char* line);
  void init_i2c_bus(uint32_t sda, uint32_t scl, uint32_t baud);
  void i2c_scan(const char* line);
  void i2c_write(const char* line);
};

} // namespace xlang3::pico
