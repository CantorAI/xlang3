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

#include "test_harness.h"
#include <atomic>
#include <thread>

namespace xlang3::test {

inline void check_unicode_index_cache(CaseResult& result) {
  // Metadata must fit the old flags/padding word: ASCII allocation buckets and
  // the inline UTF-8 payload offset stay unchanged on supported platforms.
  struct LegacyStringLayout {
    Object header;
    uint32_t size;
    uint32_t alloc_size;
    memory::X3BucketAllocator* allocator;
    std::atomic_size_t cached_hash;
    std::atomic_bool immortal;
    bool ascii;
  };
  static_assert(sizeof(StringObject) == sizeof(LegacyStringLayout));
  static_assert(offsetof(StringObject, cached_hash) == offsetof(LegacyStringLayout, cached_hash));

  Value source = Value::string(std::string(65536, 'a') + "\xC3\xA9\xF0\x9F\x98\x80");
  std::atomic_bool failed{false};
  std::vector<std::thread> threads;
  for (int index = 0; index < 4; ++index) {
    threads.emplace_back([source, &failed] {
      const auto* text = value_as_string(source);
      for (int repeat = 0; repeat < 128; ++repeat) {
        if (string_object_length(*text) != 65538 ||
            string_object_byte_offset(*text, 65537) != 65538 ||
            string_object_character_index(*text, 65538) != 65537 ||
            string_object_codepoint_at(*text, 65537) != "\xF0\x9F\x98\x80") {
          failed.store(true, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& thread : threads) thread.join();
  expect_true(result, !failed.load(), "Unicode cache publication must support concurrent readers");
  std::string dense_bytes;
  for (int i = 0; i < 1025; ++i) dense_bytes += "\xC3\xA9\xF0\x9F\x98\x80";
  Value dense = Value::string(dense_bytes);
  const auto* dense_string = value_as_string(dense);
  for (size_t index = 0; index <= 2050; ++index) {
    const size_t offset = string_object_byte_offset(*dense_string, index);
    expect_true(result, string_object_character_index(*dense_string, offset) == index,
        "dense checkpoint positions must round-trip at every codepoint boundary");
  }
  // A selected character owns its bytes after releasing the indexed source.
  Value selected = Value::string_view(string_object_codepoint_at(*value_as_string(source), 65537));
  source = Value::none();
  expect_true(result, string_object_view(*value_as_string(selected)) == "\xF0\x9F\x98\x80",
      "indexed Unicode result must survive source/cache destruction");
}

}  // namespace xlang3::test
