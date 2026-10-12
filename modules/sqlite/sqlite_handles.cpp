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
#include "sqlite_handles.h"

#include <memory>
#include <new>

namespace xlang3_sqlite {

namespace {

void release_cached_statement(CachedStatement* statement) {
  if (statement == nullptr || --statement->refcnt != 0) return;
  sqlite3_stmt* detached = statement->stmt;
  statement->stmt = nullptr;
  if (detached != nullptr) sqlite3_finalize(detached);
  delete statement;
}

void discard_cache_owner(ConnectionHandle* connection, CachedStatement* statement) {
  if (!statement->cached) return;
  auto found = connection->statement_cache.find(statement->sql);
  if (found != connection->statement_cache.end() && *found->second == statement) {
    connection->statement_cache_lru.erase(found->second);
    connection->statement_cache.erase(found);
  }
  statement->cached = false;
  // The finishing cursor still owns its lease; this cannot finalize it yet.
  release_cached_statement(statement);
}

} // namespace

void clear_connection_statement_cache(ConnectionHandle* connection) {
  if (connection == nullptr) return;
  ++connection->statement_cache_generation;
  ++connection->statement_cache_invalidation_depth;
  // Detach each cache owner before finalization. A destructor may close the
  // connection, invalidate the remaining cache, or execute unrelated SQL.
  // Nested calls run uncached until every outer invalidation has completed.
  while (!connection->statement_cache_lru.empty()) {
    CachedStatement* statement = connection->statement_cache_lru.back();
    connection->statement_cache.erase(statement->sql);
    connection->statement_cache_lru.pop_back();
    statement->cached = false;
    release_cached_statement(statement);
  }
  --connection->statement_cache_invalidation_depth;
}

void close_connection(ConnectionHandle* connection) {
  if (connection == nullptr) return;
  sqlite3* db = connection->db;
  // Publish closed state before idle-statement or factory destruction calls
  // Python. Active cursor leases keep their SQLite DB alive via close_v2 and
  // must never return their statement into this connection's cache.
  connection->db = nullptr;
  connection->closed = true;
  clear_connection_statement_cache(connection);
  if (db != nullptr) sqlite3_close_v2(db);
}

int prepare_cursor_statement(CursorHandle* cursor, const std::string& sql) {
  auto* connection = cursor->connection;
  const bool cache_enabled = connection->statement_cache_capacity != 0 &&
      connection->statement_cache_invalidation_depth == 0;
  if (cache_enabled) {
    auto found = connection->statement_cache.find(sql);
    if (found != connection->statement_cache.end()) {
      CachedStatement* statement = *found->second;
      connection->statement_cache_lru.splice(connection->statement_cache_lru.begin(),
          connection->statement_cache_lru, found->second);
      if (!statement->leased) {
        // SQLite reports busy only after stepping. Lease before binding:
        // __len__/__getitem__, adapters, and nested native callbacks can enter
        // this same connection while the prepared VM is not yet busy.
        statement->leased = true;
        ++statement->refcnt;
        cursor->cached_statement = statement;
        cursor->stmt = statement->stmt;
        return SQLITE_OK;
      }
    }
  }
  sqlite3* db = connection->db;
  const uint64_t generation = connection->statement_cache_generation;
  ++connection->cursor_prepare_count;
  const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &cursor->stmt, nullptr);
  if (rc != SQLITE_OK || cursor->stmt == nullptr || !cache_enabled ||
      connection->closed || connection->db != db ||
      connection->statement_cache_generation != generation ||
      connection->statement_cache_invalidation_depth != 0 ||
      connection->statement_cache.find(sql) != connection->statement_cache.end())
    return rc;

  // The cursor owns the raw VM until every cache allocation succeeds. A cache
  // allocation failure is an uncached execution, never a leaked/lost lease.
  std::unique_ptr<CachedStatement> statement;
  try {
    statement = std::make_unique<CachedStatement>();
    statement->sql = sql;
    statement->stmt = cursor->stmt;
    statement->generation = generation;
    connection->statement_cache_lru.push_front(statement.get());
    try {
      const auto inserted = connection->statement_cache.emplace(
          statement->sql, connection->statement_cache_lru.begin());
      if (!inserted.second) {
        connection->statement_cache_lru.pop_front();
        return rc;
      }
    } catch (...) {
      connection->statement_cache_lru.pop_front();
      throw;
    }
  } catch (const std::bad_alloc&) {
    return rc;
  }
  statement->cached = true;
  statement->leased = true;
  statement->refcnt = 2; // cache + cursor; no connection-reference cycle
  cursor->cached_statement = statement.release();
  if (connection->statement_cache.size() > connection->statement_cache_capacity) {
    CachedStatement* evicted = connection->statement_cache_lru.back();
    connection->statement_cache.erase(evicted->sql);
    connection->statement_cache_lru.pop_back();
    evicted->cached = false;
    // No cache iterator survives this callback-capable finalization. The new
    // cursor lease is already published if eviction closes or mutates the DB.
    release_cached_statement(evicted);
  }
  return rc;
}

void release_cursor_statement(CursorHandle* cursor, bool reusable) {
  sqlite3_stmt* statement = cursor->stmt;
  CachedStatement* cached = cursor->cached_statement;
  cursor->stmt = nullptr;
  cursor->cached_statement = nullptr;
  cursor->has_row = false;
  if (cached == nullptr) {
    if (statement != nullptr) sqlite3_finalize(statement);
    return;
  }
  auto* connection = cursor->connection;
  // Keep leased=true throughout reset/binding destruction. They may call
  // Python, evict this entry, close the DB or perform same-SQL nested work.
  bool ready = reusable && cached->cached && !connection->closed &&
      connection->db != nullptr && cached->generation == connection->statement_cache_generation;
  if (ready) {
    ready = sqlite3_reset(statement) == SQLITE_OK;
    if (ready) ready = sqlite3_clear_bindings(statement) == SQLITE_OK;
    ready = ready && cached->cached && !connection->closed && connection->db != nullptr &&
        cached->generation == connection->statement_cache_generation;
  }
  if (ready) cached->leased = false;
  else discard_cache_owner(connection, cached);
  release_cached_statement(cached);
}


void retain_connection(ConnectionHandle* handle) {
  if (handle != nullptr) {
    ++handle->refcnt;
  }
}

void release_connection(ConnectionHandle* handle) {
  if (handle == nullptr) {
    return;
  }
  if (--handle->refcnt != 0) {
    return;
  }
  close_connection(handle);
  delete handle;
}

void cleanup_connection(void* data) {
  auto* handle = static_cast<ConnectionHandle*>(data);
  if (handle == nullptr) {
    return;
  }
  close_connection(handle);
  release_connection(handle);
}

void cleanup_cursor(void* data) {
  auto* handle = static_cast<CursorHandle*>(data);
  if (handle == nullptr) {
    return;
  }
  release_cursor_statement(handle);
  release_connection(handle->connection);
  delete handle;
}

void cleanup_statement(void* data) {
  auto* handle = static_cast<StatementHandle*>(data);
  if (handle == nullptr) {
    return;
  }
  if (handle->stmt != nullptr) {
    sqlite3_finalize(handle->stmt);
    handle->stmt = nullptr;
  }
  release_connection(handle->connection);
  delete handle;
}

ConnectionHandle* connection_from(const X3PackageHost* host, X3Value self) {
  return static_cast<ConnectionHandle*>(host->instance_get_native_data(self, kConnectionType));
}

CursorHandle* cursor_from(const X3PackageHost* host, X3Value self) {
  return static_cast<CursorHandle*>(host->instance_get_native_data(self, kCursorType));
}

StatementHandle* statement_from(const X3PackageHost* host, X3Value self) {
  return static_cast<StatementHandle*>(host->instance_get_native_data(self, kStatementType));
}

} // namespace xlang3_sqlite
