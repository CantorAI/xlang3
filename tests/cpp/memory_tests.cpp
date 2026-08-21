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
#include "test_harness.h"

#include "runtime/memory/x3_memory.h"
#include "runtime/memory/x3_string_ref.h"

#include <cstring>

namespace {

struct DummyStringObject {
  uint32_t refcnt = 0;
  uint32_t flags = 0;
  void* data = nullptr;
};

} // namespace

int main() {
  xlang3::test::CaseResult result;

  xlang3::memory::X3MemoryManager memory;
  const auto string_kind = xlang3::memory::x3_object_kind_id(xlang3::memory::X3ObjectKind::String);
  xlang3::test::expect_true(
      result,
      memory.object_pools().register_pool(
          xlang3::memory::X3ObjectPoolDesc{
              string_kind,
              static_cast<uint32_t>(sizeof(DummyStringObject)),
              static_cast<uint32_t>(alignof(DummyStringObject))}),
      "object pool registration should succeed");

  void* first = memory.object_pools().allocate(string_kind);
  memory.object_pools().release(string_kind, first);
  void* second = memory.object_pools().allocate(string_kind);
  xlang3::test::expect_true(result, first == second, "object pool should reuse freed fixed-size slots");
  memory.object_pools().release(string_kind, second);

  auto pool_stats = memory.object_pools().stats();
  xlang3::test::expect_true(result, pool_stats.alloc_count == 2, "object pool should count allocations");
  xlang3::test::expect_true(result, pool_stats.free_count == 2, "object pool should count frees");
  xlang3::test::expect_true(result, pool_stats.reuse_count >= 1, "object pool should count reuse");

  void* bytes_a = memory.buckets().allocate(70);
  memory.buckets().release(bytes_a, 70);
  void* bytes_b = memory.buckets().allocate(70);
  xlang3::test::expect_true(result, bytes_a == bytes_b, "bucket allocator should reuse same-size slots");
  memory.buckets().release(bytes_b, 70);

  void* large = memory.buckets().allocate(8192);
  xlang3::test::expect_true(result, large != nullptr, "bucket allocator should handle large fallback allocations");
  memory.buckets().release(large, 8192);

  xlang3::memory::X3TempArena arena(128);
  auto* temp_a = static_cast<char*>(arena.allocate(16));
  std::memcpy(temp_a, "abc", 4);
  arena.reset();
  auto* temp_b = static_cast<char*>(arena.allocate(16));
  xlang3::test::expect_true(result, temp_a == temp_b, "temp arena should reuse memory after reset");

  auto trimmed = xlang3::memory::x3_trim_ascii(xlang3::memory::x3_string_view("  hello  ", 9));
  xlang3::test::expect_true(result, trimmed.size == 5, "string view trim should produce expected size");
  xlang3::test::expect_true(result, xlang3::memory::x3_to_string_view(trimmed) == "hello",
                            "string view trim should not allocate and should point to trimmed text");

  auto copy = xlang3::memory::x3_copy_string(arena, trimmed);
  xlang3::test::expect_true(result, copy.size == 5, "string buffer copy should preserve size");
  xlang3::test::expect_true(result, std::string_view(copy.data, copy.size) == "hello",
                            "string buffer copy should preserve bytes");

  return xlang3::test::finish(result);
}
