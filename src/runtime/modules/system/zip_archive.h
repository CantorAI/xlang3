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

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3 {

struct ZipArchiveEntry {
  std::string name;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint32_t crc32 = 0;
  uint32_t compressed_size = 0;
  uint32_t uncompressed_size = 0;
  uint32_t local_header_offset = 0;
};

struct ZipArchiveMember {
  std::string name;
  std::string data;
  uint16_t method = 0;
  uint32_t compressed_size = 0;
};

bool zip_archive_list_entries(
    const std::vector<uint8_t>& archive,
    std::vector<ZipArchiveEntry>& entries,
    std::string& error);

bool zip_archive_find_entry(
    const std::vector<uint8_t>& archive,
    const std::string& member,
    ZipArchiveEntry& out,
    std::string& error);

bool zip_archive_extract_stored(
    const std::vector<uint8_t>& archive,
    const ZipArchiveEntry& entry,
    std::string& out,
    std::string& error);

bool zip_archive_extract_member(
    const std::vector<uint8_t>& archive,
    const ZipArchiveEntry& entry,
    std::string& out,
    std::string& error);

bool zip_archive_build_stored(
    const std::vector<ZipArchiveMember>& members,
    std::string& out,
    std::string& error);

bool zip_archive_build(
    const std::vector<ZipArchiveMember>& members,
    std::string& out,
    std::string& error);

bool zip_archive_split_member_path(
    const std::string& archive,
    const std::string& path,
    std::string& member);

} // namespace xlang3
