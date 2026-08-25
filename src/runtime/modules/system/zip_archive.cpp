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
#include "zip_archive.h"

#include <limits>

namespace xlang3 {

namespace {

uint16_t zip_u16(const std::vector<uint8_t>& data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8u);
}

uint32_t zip_u32(const std::vector<uint8_t>& data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8u) |
         (static_cast<uint32_t>(data[offset + 2]) << 16u) |
         (static_cast<uint32_t>(data[offset + 3]) << 24u);
}

void append_u16(std::string& out, uint16_t value) {
  out.push_back(static_cast<char>(value & 0xffu));
  out.push_back(static_cast<char>((value >> 8u) & 0xffu));
}

void append_u32(std::string& out, uint32_t value) {
  out.push_back(static_cast<char>(value & 0xffu));
  out.push_back(static_cast<char>((value >> 8u) & 0xffu));
  out.push_back(static_cast<char>((value >> 16u) & 0xffu));
  out.push_back(static_cast<char>((value >> 24u) & 0xffu));
}

bool fits_u16(size_t value) {
  return value <= std::numeric_limits<uint16_t>::max();
}

bool fits_u32(size_t value) {
  return value <= std::numeric_limits<uint32_t>::max();
}

uint32_t crc32_bytes(const std::string& data) {
  uint32_t crc = 0xffffffffu;
  for (unsigned char byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1u) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ 0xffffffffu;
}

bool find_eocd(const std::vector<uint8_t>& archive, size_t& eocd, std::string& error) {
  if (archive.size() < 22) {
    error = "zip archive is too small";
    return false;
  }

  const size_t search_start = archive.size() > 66000 ? archive.size() - 66000 : 0;
  for (size_t pos = archive.size() - 22; pos + 4 <= archive.size() && pos >= search_start; --pos) {
    if (zip_u32(archive, pos) == 0x06054b50u) {
      eocd = pos;
      return true;
    }
    if (pos == 0) {
      break;
    }
  }
  error = "zip end-of-central-directory not found";
  return false;
}

} // namespace

bool zip_archive_list_entries(
    const std::vector<uint8_t>& archive,
    std::vector<ZipArchiveEntry>& entries,
    std::string& error) {
  size_t eocd = 0;
  if (!find_eocd(archive, eocd, error)) {
    return false;
  }

  const uint16_t entry_count = zip_u16(archive, eocd + 10);
  const uint32_t central_offset = zip_u32(archive, eocd + 16);
  size_t pos = central_offset;
  entries.clear();
  entries.reserve(entry_count);
  for (uint16_t i = 0; i < entry_count; ++i) {
    if (pos + 46 > archive.size() || zip_u32(archive, pos) != 0x02014b50u) {
      error = "zip central-directory entry is invalid";
      return false;
    }
    const uint16_t method = zip_u16(archive, pos + 10);
    const uint32_t crc32 = zip_u32(archive, pos + 16);
    const uint32_t compressed_size = zip_u32(archive, pos + 20);
    const uint32_t uncompressed_size = zip_u32(archive, pos + 24);
    const uint16_t name_len = zip_u16(archive, pos + 28);
    const uint16_t extra_len = zip_u16(archive, pos + 30);
    const uint16_t comment_len = zip_u16(archive, pos + 32);
    const uint32_t local_header_offset = zip_u32(archive, pos + 42);
    if (pos + 46u + name_len + extra_len + comment_len > archive.size()) {
      error = "zip central-directory entry exceeds archive size";
      return false;
    }
    std::string name(reinterpret_cast<const char*>(archive.data() + pos + 46), name_len);
    entries.push_back(ZipArchiveEntry{std::move(name), method, crc32, compressed_size, uncompressed_size, local_header_offset});
    pos += 46u + name_len + extra_len + comment_len;
  }
  return true;
}

bool zip_archive_find_entry(
    const std::vector<uint8_t>& archive,
    const std::string& member,
    ZipArchiveEntry& out,
    std::string& error) {
  std::vector<ZipArchiveEntry> entries;
  if (!zip_archive_list_entries(archive, entries, error)) {
    return false;
  }
  for (auto& entry : entries) {
    if (entry.name == member) {
      out = std::move(entry);
      return true;
    }
  }
  error = "zip member not found: " + member;
  return false;
}

bool zip_archive_extract_stored(
    const std::vector<uint8_t>& archive,
    const ZipArchiveEntry& entry,
    std::string& out,
    std::string& error) {
  if (entry.method != 0) {
    error = "zip member uses unsupported compression method " + std::to_string(entry.method);
    return false;
  }
  const size_t local = entry.local_header_offset;
  if (local + 30 > archive.size() || zip_u32(archive, local) != 0x04034b50u) {
    error = "zip local header is invalid";
    return false;
  }
  const uint16_t name_len = zip_u16(archive, local + 26);
  const uint16_t extra_len = zip_u16(archive, local + 28);
  const size_t data_offset = local + 30u + name_len + extra_len;
  if (data_offset + entry.compressed_size > archive.size()) {
    error = "zip member data exceeds archive size";
    return false;
  }
  if (entry.compressed_size != entry.uncompressed_size) {
    error = "zip stored member has mismatched sizes";
    return false;
  }
  out.assign(reinterpret_cast<const char*>(archive.data() + data_offset), entry.uncompressed_size);
  return true;
}

bool zip_archive_build_stored(
    const std::vector<ZipArchiveMember>& members,
    std::string& out,
    std::string& error) {
  if (!fits_u16(members.size())) {
    error = "too many zip members";
    return false;
  }

  struct CentralRecord {
    std::string name;
    uint32_t crc32 = 0;
    uint32_t size = 0;
    uint32_t local_offset = 0;
  };

  std::vector<CentralRecord> central;
  central.reserve(members.size());
  out.clear();

  for (const auto& member : members) {
    if (!fits_u16(member.name.size()) || !fits_u32(member.data.size()) || !fits_u32(out.size())) {
      error = "zip member exceeds classic ZIP limits";
      return false;
    }
    const uint32_t crc = crc32_bytes(member.data);
    const uint32_t size = static_cast<uint32_t>(member.data.size());
    const uint32_t local_offset = static_cast<uint32_t>(out.size());

    append_u32(out, 0x04034b50u);
    append_u16(out, 20);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u32(out, crc);
    append_u32(out, size);
    append_u32(out, size);
    append_u16(out, static_cast<uint16_t>(member.name.size()));
    append_u16(out, 0);
    out.append(member.name.data(), member.name.size());
    out.append(member.data.data(), member.data.size());

    central.push_back(CentralRecord{member.name, crc, size, local_offset});
  }

  if (!fits_u32(out.size())) {
    error = "zip central directory offset exceeds classic ZIP limits";
    return false;
  }
  const uint32_t central_offset = static_cast<uint32_t>(out.size());
  for (const auto& record : central) {
    append_u32(out, 0x02014b50u);
    append_u16(out, 20);
    append_u16(out, 20);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u32(out, record.crc32);
    append_u32(out, record.size);
    append_u32(out, record.size);
    append_u16(out, static_cast<uint16_t>(record.name.size()));
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u16(out, 0);
    append_u32(out, 0);
    append_u32(out, record.local_offset);
    out.append(record.name.data(), record.name.size());
  }

  if (!fits_u32(out.size() - central_offset)) {
    error = "zip central directory size exceeds classic ZIP limits";
    return false;
  }
  const uint32_t central_size = static_cast<uint32_t>(out.size() - central_offset);
  append_u32(out, 0x06054b50u);
  append_u16(out, 0);
  append_u16(out, 0);
  append_u16(out, static_cast<uint16_t>(central.size()));
  append_u16(out, static_cast<uint16_t>(central.size()));
  append_u32(out, central_size);
  append_u32(out, central_offset);
  append_u16(out, 0);
  return true;
}

bool zip_archive_split_member_path(
    const std::string& archive,
    const std::string& path,
    std::string& member) {
  if (path.size() <= archive.size() || path.compare(0, archive.size(), archive) != 0) {
    return false;
  }
  const char separator = path[archive.size()];
  if (separator != '/' && separator != '\\') {
    return false;
  }
  member = path.substr(archive.size() + 1);
  for (auto& ch : member) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return !member.empty();
}

} // namespace xlang3
