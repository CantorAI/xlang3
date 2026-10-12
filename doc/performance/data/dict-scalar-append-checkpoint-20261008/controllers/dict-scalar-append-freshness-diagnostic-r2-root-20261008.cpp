#include "xlang3/mapping.h"
#include "xlang3/runtime.h"
#include <iostream>
#include <string>

int main() {
  xlang3::Runtime runtime(std::cout);
  xlang3::Value dictionary = xlang3::Value::dict({});
  auto* storage = xlang3::value_as_dict(dictionary);
  size_t fresh_before_append = 0, stale_after_append = 0;
  size_t rebuilt_entries_after_append = 0;
  for (int64_t index = 0; index < 64; ++index) {
    const auto key = xlang3::Value::int64(1000 + index);
    xlang3::Value result;
    std::string error;
    if (xlang3::mapping_get_item_runtime(runtime, dictionary, key, result, error, false) ||
        error != "key not found" || storage->entries.size() != static_cast<size_t>(index) ||
        storage->runtime_hash_indexed_entry_count != storage->entries.size()) return 1;
    ++fresh_before_append;
    error.clear();
    if (!xlang3::mapping_set_item(dictionary, key, xlang3::Value::int64(index), error) ||
        !error.empty() || storage->entries.size() != static_cast<size_t>(index + 1)) return 2;
    if (storage->runtime_hash_indexed_entry_count != storage->entries.size()) {
      ++stale_after_append;
      rebuilt_entries_after_append += storage->entries.size();
    }
    if (!xlang3::mapping_get_item_runtime(runtime, dictionary, key, result, error, false) ||
        !error.empty() || result.tag != xlang3::ValueTag::Int64 || result.as.i64 != index ||
        storage->runtime_hash_indexed_entry_count != storage->entries.size()) return 3;
  }
  std::cout << "{\"status\":\"completed_public_dll_index_state_diagnostic\","
      "\"scored\":false,\"append_count\":64,\"fresh_before_append\":" << fresh_before_append <<
      ",\"stale_after_append\":" << stale_after_append <<
      ",\"entries_in_rebuilt_tables_after_append\":" << rebuilt_entries_after_append <<
      ",\"all_values_correct\":true}\n";
  return 0;
}
