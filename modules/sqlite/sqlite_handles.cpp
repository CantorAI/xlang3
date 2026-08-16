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

namespace xlang3_sqlite {

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
  if (handle->db != nullptr) {
    sqlite3_close_v2(handle->db);
    handle->db = nullptr;
  }
  delete handle;
}

void cleanup_connection(void* data) {
  auto* handle = static_cast<ConnectionHandle*>(data);
  if (handle == nullptr) {
    return;
  }
  if (handle->db != nullptr) {
    sqlite3_close_v2(handle->db);
    handle->db = nullptr;
  }
  handle->closed = true;
  release_connection(handle);
}

void cleanup_cursor(void* data) {
  auto* handle = static_cast<CursorHandle*>(data);
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
