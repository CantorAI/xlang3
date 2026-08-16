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
#include "embedded/flash_file_store.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <algorithm>
#include <cstring>
#include <set>

#ifndef XLANG3_FLASH_STORE_BASE
#define XLANG3_FLASH_STORE_BASE 0
#endif

#ifndef XLANG3_FLASH_STORE_SIZE
#define XLANG3_FLASH_STORE_SIZE (-1)
#endif

#ifndef XLANG3_FLASH_STORE_SAFETY_SECTORS
#define XLANG3_FLASH_STORE_SAFETY_SECTORS 1
#endif

extern "C" char __flash_binary_end;

namespace xlang3::pico {
namespace {

constexpr uint32_t kRecordMagic = 0x33534658u; // XFS3
constexpr uint32_t kRecordLive = 1;
constexpr uint32_t kRecordDeleted = 2;

struct RecordHeader {
  uint32_t magic;
  uint32_t flags;
  uint32_t path_size;
  uint32_t data_size;
  uint32_t checksum;
};

uint32_t align_up(uint32_t value, uint32_t alignment) {
  return (value + alignment - 1u) & ~(alignment - 1u);
}

uint32_t firmware_end_offset() {
  const auto address = reinterpret_cast<uintptr_t>(&__flash_binary_end);
  if (address < XIP_BASE) {
    return 0;
  }
  return static_cast<uint32_t>(address - XIP_BASE);
}

uint32_t configured_base_offset() {
#if XLANG3_FLASH_STORE_BASE != 0
  return align_up(static_cast<uint32_t>(XLANG3_FLASH_STORE_BASE), FLASH_SECTOR_SIZE);
#else
  const uint32_t safety = static_cast<uint32_t>(XLANG3_FLASH_STORE_SAFETY_SECTORS) * FLASH_SECTOR_SIZE;
  return align_up(firmware_end_offset(), FLASH_SECTOR_SIZE) + safety;
#endif
}

uint32_t configured_capacity(uint32_t base) {
  if (base >= PICO_FLASH_SIZE_BYTES) {
    return 0;
  }
  const uint32_t available = PICO_FLASH_SIZE_BYTES - base;
#if XLANG3_FLASH_STORE_SIZE < 0
  return available - (available % FLASH_SECTOR_SIZE);
#else
  const uint32_t requested = static_cast<uint32_t>(XLANG3_FLASH_STORE_SIZE);
  if (requested > available) {
    return available - (available % FLASH_SECTOR_SIZE);
  }
  return requested - (requested % FLASH_SECTOR_SIZE);
#endif
}

const uint8_t* flash_ptr(uint32_t offset) {
  return reinterpret_cast<const uint8_t*>(XIP_BASE + offset);
}

uint32_t checksum_bytes(const uint8_t* data, uint32_t size, uint32_t seed) {
  uint32_t hash = seed == 0 ? 2166136261u : seed;
  for (uint32_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

std::string normalize_path(const char* raw) {
  std::string input = raw == nullptr ? "" : raw;
  std::string out = "/";
  std::size_t i = 0;
  while (i < input.size()) {
    while (i < input.size() && input[i] == '/') {
      ++i;
    }
    const std::size_t start = i;
    while (i < input.size() && input[i] != '/') {
      ++i;
    }
    if (i == start) {
      continue;
    }
    const std::string part = input.substr(start, i - start);
    if (part == "." || part == "..") {
      continue;
    }
    if (out.size() > 1) {
      out.push_back('/');
    }
    out += part;
  }
  return out;
}

std::string normalize_dir(const char* raw) {
  std::string path = normalize_path(raw);
  if (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

bool direct_child_of(const std::string& path, const std::string& dir, std::string& child) {
  if (dir == "/") {
    if (path.size() <= 1 || path[0] != '/') {
      return false;
    }
    const auto slash = path.find('/', 1);
    child = slash == std::string::npos ? path.substr(1) : path.substr(1, slash - 1);
    return !child.empty();
  }
  if (path.rfind(dir + "/", 0) != 0) {
    return false;
  }
  const std::size_t start = dir.size() + 1;
  const auto slash = path.find('/', start);
  child = slash == std::string::npos ? path.substr(start) : path.substr(start, slash - start);
  return !child.empty();
}

} // namespace

FlashFileStore::FlashFileStore() {
  base_offset_ = configured_base_offset();
  capacity_ = configured_capacity(base_offset_);
}

xlang3::rpc::FileStore FlashFileStore::as_file_store() {
  xlang3::rpc::FileStore store{};
  store.context = this;
  store.put = [](void* context, const char* path, const uint8_t* data, uint32_t size, std::string& error) {
    return static_cast<FlashFileStore*>(context)->put(path, data, size, error);
  };
  store.get = [](void* context, const char* path, std::vector<uint8_t>& out, std::string& error) {
    return static_cast<FlashFileStore*>(context)->get(path, out, error);
  };
  store.remove = [](void* context, const char* path, std::string& error) {
    return static_cast<FlashFileStore*>(context)->remove(path, error);
  };
  store.list = [](void* context, const char* path, std::vector<std::string>& out, std::string& error) {
    return static_cast<FlashFileStore*>(context)->list(path, out, error);
  };
  return store;
}

bool FlashFileStore::mount(std::string& error) {
  entries_.clear();
  used_bytes_ = 0;
  if (capacity_ < FLASH_SECTOR_SIZE || base_offset_ % FLASH_SECTOR_SIZE != 0) {
    error = "invalid flash store layout";
    return false;
  }

  uint32_t offset = 0;
  while (offset + sizeof(RecordHeader) <= capacity_) {
    const auto* header = reinterpret_cast<const RecordHeader*>(flash_ptr(base_offset_ + offset));
    if (header->magic == 0xffffffffu) {
      used_bytes_ = offset;
      return true;
    }
    if (header->magic != kRecordMagic ||
        header->path_size == 0 ||
        header->path_size > 512 ||
        header->data_size > capacity_) {
      if (offset == 0) {
        if (!erase_region(error)) {
          return false;
        }
        used_bytes_ = 0;
        entries_.clear();
        return true;
      }
      used_bytes_ = offset;
      return true;
    }
    const uint32_t raw_size = sizeof(RecordHeader) + header->path_size + header->data_size;
    const uint32_t record_size = align_up(raw_size, FLASH_PAGE_SIZE);
    if (record_size == 0 || offset + record_size > capacity_) {
      used_bytes_ = offset;
      return true;
    }
    const auto* path_data = flash_ptr(base_offset_ + offset + sizeof(RecordHeader));
    const auto* file_data = path_data + header->path_size;
    uint32_t check = checksum_bytes(reinterpret_cast<const uint8_t*>(&header->flags), sizeof(header->flags), 0);
    check = checksum_bytes(reinterpret_cast<const uint8_t*>(&header->path_size), sizeof(header->path_size), check);
    check = checksum_bytes(reinterpret_cast<const uint8_t*>(&header->data_size), sizeof(header->data_size), check);
    check = checksum_bytes(path_data, header->path_size, check);
    check = checksum_bytes(file_data, header->data_size, check);
    if (check != header->checksum) {
      used_bytes_ = offset;
      return true;
    }
    const std::string path(reinterpret_cast<const char*>(path_data), header->path_size);
    if (header->flags == kRecordDeleted) {
      entries_.erase(path);
    } else if (header->flags == kRecordLive) {
      entries_[path] = Entry{offset + sizeof(RecordHeader) + header->path_size, header->data_size};
    }
    offset += record_size;
  }
  used_bytes_ = offset;
  return true;
}

std::size_t FlashFileStore::file_count() const {
  return entries_.size();
}

std::size_t FlashFileStore::byte_count() const {
  std::size_t total = 0;
  for (const auto& item : entries_) {
    total += item.second.data_size;
  }
  return total;
}

bool FlashFileStore::put(const char* path, const uint8_t* data, uint32_t size, std::string& error) {
  const std::string normalized = normalize_path(path);
  if (normalized == "/") {
    error = "flash store path must name a file";
    return false;
  }
  if (!append_record(normalized, data, size, false, error)) {
    if (!compact(error)) {
      return false;
    }
    if (!append_record(normalized, data, size, false, error)) {
      return false;
    }
  }
  return mount(error);
}

bool FlashFileStore::get(const char* path, std::vector<uint8_t>& out, std::string& error) {
  const std::string normalized = normalize_path(path);
  const auto it = entries_.find(normalized);
  if (it == entries_.end()) {
    error = "file not found: " + normalized;
    return false;
  }
  const Entry& entry = it->second;
  out.assign(flash_ptr(base_offset_ + entry.data_offset), flash_ptr(base_offset_ + entry.data_offset + entry.data_size));
  return true;
}

bool FlashFileStore::remove(const char* path, std::string& error) {
  const std::string normalized = normalize_path(path);
  if (entries_.find(normalized) == entries_.end()) {
    error = "file not found: " + normalized;
    return false;
  }
  if (!append_record(normalized, nullptr, 0, true, error)) {
    if (!compact(error)) {
      return false;
    }
    if (!append_record(normalized, nullptr, 0, true, error)) {
      return false;
    }
  }
  return mount(error);
}

bool FlashFileStore::list(const char* path, std::vector<std::string>& out, std::string&) {
  const std::string dir = normalize_dir(path);
  std::set<std::string> children;
  for (const auto& item : entries_) {
    std::string child;
    if (direct_child_of(item.first, dir, child)) {
      children.insert(child);
    }
  }
  out.assign(children.begin(), children.end());
  return true;
}

bool FlashFileStore::append_record(
    const std::string& path,
    const uint8_t* data,
    uint32_t size,
    bool deleted,
    std::string& error) {
  const uint32_t raw_size = sizeof(RecordHeader) + static_cast<uint32_t>(path.size()) + size;
  const uint32_t record_size = align_up(raw_size, FLASH_PAGE_SIZE);
  if (record_size > capacity_ || used_bytes_ + record_size > capacity_) {
    error = "flash store is full";
    return false;
  }
  std::vector<uint8_t> page(record_size, 0xff);
  auto* header = reinterpret_cast<RecordHeader*>(page.data());
  header->magic = kRecordMagic;
  header->flags = deleted ? kRecordDeleted : kRecordLive;
  header->path_size = static_cast<uint32_t>(path.size());
  header->data_size = deleted ? 0 : size;
  std::memcpy(page.data() + sizeof(RecordHeader), path.data(), path.size());
  if (!deleted && data != nullptr && size != 0) {
    std::memcpy(page.data() + sizeof(RecordHeader) + path.size(), data, size);
  }
  uint32_t check = checksum_bytes(reinterpret_cast<const uint8_t*>(&header->flags), sizeof(header->flags), 0);
  check = checksum_bytes(reinterpret_cast<const uint8_t*>(&header->path_size), sizeof(header->path_size), check);
  check = checksum_bytes(reinterpret_cast<const uint8_t*>(&header->data_size), sizeof(header->data_size), check);
  check = checksum_bytes(reinterpret_cast<const uint8_t*>(path.data()), static_cast<uint32_t>(path.size()), check);
  if (!deleted && data != nullptr && size != 0) {
    check = checksum_bytes(data, size, check);
  }
  header->checksum = check;
  if (!program(used_bytes_, page.data(), static_cast<uint32_t>(page.size()), error)) {
    return false;
  }
  used_bytes_ += record_size;
  return true;
}

bool FlashFileStore::compact(std::string& error) {
  std::vector<std::pair<std::string, std::vector<uint8_t>>> live;
  live.reserve(entries_.size());
  for (const auto& item : entries_) {
    std::vector<uint8_t> data;
    if (!get(item.first.c_str(), data, error)) {
      return false;
    }
    live.emplace_back(item.first, std::move(data));
  }
  if (!erase_region(error)) {
    return false;
  }
  used_bytes_ = 0;
  entries_.clear();
  for (const auto& item : live) {
    if (!append_record(item.first, item.second.data(), static_cast<uint32_t>(item.second.size()), false, error)) {
      return false;
    }
  }
  return mount(error);
}

bool FlashFileStore::erase_region(std::string& error) {
  if (base_offset_ % FLASH_SECTOR_SIZE != 0 || capacity_ % FLASH_SECTOR_SIZE != 0 ||
      base_offset_ + capacity_ > PICO_FLASH_SIZE_BYTES) {
    error = "flash erase range outside store";
    return false;
  }
  const uint32_t interrupts = save_and_disable_interrupts();
  flash_range_erase(base_offset_, capacity_);
  restore_interrupts(interrupts);
  return true;
}

bool FlashFileStore::program(uint32_t relative_offset, const uint8_t* data, uint32_t size, std::string& error) {
  if (data == nullptr ||
      relative_offset % FLASH_PAGE_SIZE != 0 ||
      size % FLASH_PAGE_SIZE != 0 ||
      relative_offset > capacity_ ||
      size > capacity_ - relative_offset ||
      base_offset_ + relative_offset < base_offset_ ||
      base_offset_ + relative_offset + size > PICO_FLASH_SIZE_BYTES) {
    error = "flash program range outside store";
    return false;
  }
  const uint32_t interrupts = save_and_disable_interrupts();
  flash_range_program(base_offset_ + relative_offset, data, size);
  restore_interrupts(interrupts);
  return true;
}

} // namespace xlang3::pico
