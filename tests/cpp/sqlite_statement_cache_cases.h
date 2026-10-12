#pragma once
#include "test_harness.h"
#include "sqlite_aggregate_abi_cases.h"
#include "../../modules/sqlite/sqlite_handles.h"
#include <filesystem>
#include <stdexcept>

namespace xlang3::test::sqlite_cache_probe {

inline void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

inline xlang3_sqlite::ConnectionHandle* connection_handle(X::Runtime& runtime, const X::Value& value) {
  auto* handle = static_cast<xlang3_sqlite::ConnectionHandle*>(
      runtime.host()->instance_get_native_data(value.raw(), xlang3_sqlite::kConnectionType));
  require(handle != nullptr, "missing actual native cache connection handle");
  return handle;
}

inline xlang3_sqlite::CursorHandle* cursor_handle(X::Runtime& runtime, const X::Value& value) {
  auto* handle = static_cast<xlang3_sqlite::CursorHandle*>(
      runtime.host()->instance_get_native_data(value.raw(), xlang3_sqlite::kCursorType));
  require(handle != nullptr, "missing actual native cache cursor lease");
  return handle;
}

inline void check(X::Runtime& runtime) {
  X::Module module(runtime, "sqlite_statement_cache_sdk_probe");
  X::Value connection, result;
  require(module["connect"].Call({}, connection), "cannot open default cached connection");
  auto* handle = connection_handle(runtime, connection);
  require(handle->statement_cache_capacity == 128, "default native cache capacity differs from 3.14");
  require(module["setup"].Call({connection}, result), "cannot create native cache table");
  const auto before = handle->cursor_prepare_count;
  for (int index = 0; index < 8; ++index)
    require(module["insert"].Call({connection, X::Value(index)}, result), "repeated native shortcut INSERT failed");
  require(handle->cursor_prepare_count == before + 1 && handle->statement_cache.size() == 2 &&
      handle->statement_cache_lru.size() == 2, "identical INSERT across fresh cursors did not reuse one actual prepare");
  require(module["omitted_binding"].Call({connection}, result) && result.IsNone(),
      "cache retained a prior binding instead of the accepted uncached NULL behavior");
  require(module["failed_binding"].Call({connection}, result) &&
      handle->statement_cache.count("INSERT INTO cache_rows VALUES (?)") == 0,
      "a failed binding retained its unusable cache lease");
  require(module["insert"].Call({connection, X::Value(0)}, result), "cannot re-prime INSERT lease");
  const auto nested_before = handle->cursor_prepare_count;
  require(module["bind_nested"].Call({connection}, result) &&
      handle->cursor_prepare_count == nested_before + 1,
      "same-SQL nested binding reused an unstepped leased VM or missed the busy fallback");

  X::Value first, second;
  require(module["open_rows"].Call({connection}, first), "cannot open first cached SELECT lease");
  auto* first_handle = cursor_handle(runtime, first);
  require(first_handle->cached_statement != nullptr && first_handle->cached_statement->leased,
      "row-producing cursor did not publish a cache lease before callbacks");
  const auto busy_before = handle->cursor_prepare_count;
  require(module["open_rows"].Call({connection}, second), "cannot open independent busy SELECT");
  auto* second_handle = cursor_handle(runtime, second);
  require(handle->cursor_prepare_count == busy_before + 1 && second_handle->cached_statement == nullptr &&
      first_handle->stmt != second_handle->stmt, "active identical SQL aliased its cached cursor statement");
  require(first["fetchall"].Call({}, result) && second["fetchall"].Call({}, result),
      "independent row leases did not finish");
  require(first_handle->stmt == nullptr && second_handle->stmt == nullptr,
      "completed cursor retained a published active VM");
  require(connection["close"].Call({}, result) && handle->statement_cache.empty() &&
      handle->statement_cache_lru.empty(), "explicit close retained native cached SQLite resources");

  X::Value disabled;
  require(module["connect"].Call({X::Value(0)}, disabled), "cached_statements=0 rejected");
  auto* uncached = connection_handle(runtime, disabled);
  require(module["setup"].Call({disabled}, result), "cannot set up uncached connection");
  const auto disabled_before = uncached->cursor_prepare_count;
  for (int index = 0; index < 3; ++index)
    require(module["insert"].Call({disabled, X::Value(index)}, result), "uncached INSERT failed");
  require(uncached->statement_cache_capacity == 0 && uncached->statement_cache.empty() &&
      uncached->cursor_prepare_count == disabled_before + 3, "cache disable option did not preserve uncached preparation");

  X::Value lru;
  require(module["connect"].Call({X::Value(2)}, lru), "cannot open bounded LRU connection");
  auto* bounded = connection_handle(runtime, lru);
  for (const char* sql : {"SELECT 1", "SELECT 2", "SELECT 1", "SELECT 3"})
    require(module["scalar"].Call({lru, X::Value(runtime, sql)}, result), "LRU scalar execution failed");
  require(bounded->cursor_prepare_count == 3 && bounded->statement_cache.size() == 2 &&
      bounded->statement_cache.count("SELECT 1") == 1 && bounded->statement_cache.count("SELECT 2") == 0,
      "bounded cache did not retain the touched statement and evict the least recently used SQL");
  require(module["scalar"].Call({lru, X::Value(runtime, "SELECT 2")}, result) &&
      bounded->cursor_prepare_count == 4, "evicted SQL did not require a new native prepare");

  X::Module aggregate(runtime, "sqlite_aggregate_sdk_probe");
  auto counters = std::make_shared<sqlite_aggregate_probe::FactoryCounters>();
  auto callback = sqlite_aggregate_probe::factory(runtime, aggregate["Sum"], counters);
  require(lru["create_aggregate"].Call({X::Value(runtime, "sdk_sum"), X::Value(1), callback}, result),
      "cannot register cached aggregate factory");
  callback = X::Value();
  require(aggregate["query"].Call({lru}, result) && result.ToInt64() == 6 && counters->called == 1,
      "cached aggregate did not execute its Python algorithm");
  require(lru["create_aggregate"].Call({X::Value(runtime, "sdk_sum"), X::Value(1), aggregate["Sum"]}, result) &&
      counters->destroyed == 1 && bounded->statement_cache.empty(),
      "cache invalidation changed accepted immediate aggregate replacement lifetime");
  require(disabled["close"].Call({}, result) && lru["close"].Call({}, result), "cache test teardown failed");
}

} // namespace xlang3::test::sqlite_cache_probe

namespace xlang3::test {
inline void check_sqlite_statement_cache(CaseResult& result, const char* program) {
  try {
    using namespace sqlite_cache_probe;
    require(program != nullptr, "cannot locate native cache test executable");
    const auto fixtures = std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path();
    const auto modules = std::filesystem::absolute(std::filesystem::path(program)).parent_path() / "modules";
    sqlite_aggregate_probe::ProbeState state;
    X::Runtime runtime;
    X3Value package = x3_value_invalid();
    runtime.check(x3_runtime_register_package(runtime.get(), "sqlite_cache_index_probe",
        sqlite_aggregate_probe::capture_host, &state, &package));
    x3_value_release(package);
    require(state.host != nullptr && state.host->size >= offsetof(X3PackageHost, value_index) +
        sizeof(state.host->value_index) && state.host->value_index != nullptr,
        "real package host lacks the prefix-compatible intrinsic index callback");
    static_assert(offsetof(X3PackageHost, value_index) ==
        offsetof(X3PackageHost, runtime_restore_exception) + sizeof(X3PackageHost::runtime_restore_exception));
    X3Value indexed = x3_value_invalid();
    require(state.host->value_index(runtime.get(), x3_value_bool(1), &indexed) == X3_STATUS_OK &&
        indexed.tag == X3_TAG_INT64 && indexed.as.i64 == 1,
        "native intrinsic bool index did not return an owned canonical integer");
    require(state.host->value_index(nullptr, x3_value_int64(1), &indexed) == X3_STATUS_ERROR &&
        state.host->value_index(runtime.get(), x3_value_int64(1), nullptr) == X3_STATUS_ERROR,
        "native intrinsic index accepted null runtime/output");
    runtime.AddImportRoot(fixtures.string().c_str());
    runtime.AddImportRoot(modules.string().c_str());
    check(runtime);
    expect_true(result, true, "SQLite native prepare reuse, LRU, leases, bindings and callback owners");
  } catch (const std::exception& error) {
    expect_true(result, false, std::string("SQLite cache probe: ") + error.what());
  }
}
} // namespace xlang3::test
