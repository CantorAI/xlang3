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

#include "xlang3/xmodule.h"

#include "sqlite3.h"

namespace xlang3_sqlite {

constexpr const char* kConnectionType = "xlang3.sqlite.Connection";
constexpr const char* kCursorType = "xlang3.sqlite.Cursor";
constexpr const char* kStatementType = "xlang3.sqlite.Statement";

struct ConnectionHandle {
  sqlite3* db = nullptr;
  uint32_t refcnt = 1;
  bool closed = false;
};

struct CursorHandle {
  ConnectionHandle* connection = nullptr;
  sqlite3_stmt* stmt = nullptr;
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

ConnectionHandle* connection_from(const X3PackageHost* host, X3Value self);
CursorHandle* cursor_from(const X3PackageHost* host, X3Value self);
StatementHandle* statement_from(const X3PackageHost* host, X3Value self);

} // namespace xlang3_sqlite
