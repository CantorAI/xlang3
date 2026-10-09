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

#include "xlang3/xlang3.h"

#include "../../third_party/sqlite/sqlite3.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3_sqlite {

constexpr const char* kConnectionType = "xlang3.sqlite.Connection";
constexpr const char* kCursorType = "xlang3.sqlite.Cursor";
constexpr const char* kStatementType = "xlang3.sqlite.Statement";

struct CachedStatement {
  sqlite3_stmt* stmt = nullptr;
  std::string sql;
  uint32_t refcnt = 1;
  uint64_t generation = 0;
  bool cached = false;
  bool leased = false;
};

struct ConnectionHandle {
  sqlite3* db = nullptr;
  uint32_t refcnt = 1;
  bool closed = false;
  size_t statement_cache_capacity = 128;
  uint64_t statement_cache_generation = 0;
  uint32_t statement_cache_invalidation_depth = 0;
  // Explicit cursor prepare attempts only; excludes implicit transactions and
  // SQLite's automatic schema reprepares. Internal resource proof, no API.
  uint64_t cursor_prepare_count = 0;
  std::list<CachedStatement*> statement_cache_lru;
  std::unordered_map<std::string, std::list<CachedStatement*>::iterator> statement_cache;
};

struct CursorHandle {
  // A current SQLite row is primed by execute, then advanced before each
  // fetch returns. Description must outlive the completed statement.
  bool has_row = false;
  bool locked = false;
  bool write_statement = false;
  std::vector<std::string> column_names;
  ConnectionHandle* connection = nullptr;
  sqlite3_stmt* stmt = nullptr;
  CachedStatement* cached_statement = nullptr;
  sqlite3_int64 lastrowid = 0;
  sqlite3_int64 rowcount = -1;
  bool has_lastrowid = false;
};

struct StatementHandle {
  ConnectionHandle* connection = nullptr;
  sqlite3_stmt* stmt = nullptr;
  int last_rc = SQLITE_OK;
};

void cleanup_connection(void* data);
void cleanup_cursor(void* data);
void cleanup_statement(void* data);
void retain_connection(ConnectionHandle* handle);
void release_connection(ConnectionHandle* handle);
void close_connection(ConnectionHandle* handle);
void clear_connection_statement_cache(ConnectionHandle* handle);
int prepare_cursor_statement(CursorHandle* cursor, const std::string& sql);
void release_cursor_statement(CursorHandle* cursor, bool reusable = false);

ConnectionHandle* connection_from(const X3PackageHost* host, X3Value self);
CursorHandle* cursor_from(const X3PackageHost* host, X3Value self);
StatementHandle* statement_from(const X3PackageHost* host, X3Value self);

} // namespace xlang3_sqlite
