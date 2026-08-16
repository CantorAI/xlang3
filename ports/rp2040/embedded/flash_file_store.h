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

#include "xlang3/rpc/file_store.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3::pico {

class FlashFileStore {
public:
  FlashFileStore();

  xlang3::rpc::FileStore as_file_store();

  bool mount(std::string& error);
  std::size_t file_count() const;
  std::size_t byte_count() const;
  uint32_t base_offset() const { return base_offset_; }
  uint32_t capacity() const { return capacity_; }
  uint32_t used_bytes() const { return used_bytes_; }
  bool implemented() const { return capacity_ > 0; }

private:
  struct Entry {
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
  };

  uint32_t base_offset_ = 0;
  uint32_t capacity_ = 0;
  uint32_t used_bytes_ = 0;
  std::unordered_map<std::string, Entry> entries_;

  bool put(const char* path, const uint8_t* data, uint32_t size, std::string& error);
  bool get(const char* path, std::vector<uint8_t>& out, std::string& error);
  bool remove(const char* path, std::string& error);
  bool list(const char* path, std::vector<std::string>& out, std::string& error);

  bool append_record(const std::string& path, const uint8_t* data, uint32_t size, bool deleted, std::string& error);
  bool compact(std::string& error);
  bool erase_region(std::string& error);
  bool program(uint32_t relative_offset, const uint8_t* data, uint32_t size, std::string& error);
};

} // namespace xlang3::pico
