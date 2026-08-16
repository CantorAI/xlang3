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
#include "sqlite_values.h"

namespace xlang3_sqlite {

bool require_string(
    const X3PackageHost* host,
    X3CallContext* context,
    X3Runtime* runtime,
    X3Value value,
    const char* message,
    const char** out) {
  if (host->value_object_kind(value) != X3_OBJECT_KIND_STRING) {
    host->set_error(context, message);
    return false;
  }
  *out = host->value_to_cstr(runtime, value);
  return *out != nullptr;
}

bool bind_value(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt, int index, X3Value value) {
  switch (value.tag) {
    case X3_TAG_NONE:
      return sqlite3_bind_null(stmt, index) == SQLITE_OK;
    case X3_TAG_BOOL:
      return sqlite3_bind_int64(stmt, index, value.as.b ? 1 : 0) == SQLITE_OK;
    case X3_TAG_INT64:
      return sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(value.as.i64)) == SQLITE_OK;
    case X3_TAG_UINT64:
      return sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(value.as.u64)) == SQLITE_OK;
    case X3_TAG_DOUBLE:
      return sqlite3_bind_double(stmt, index, value.as.f64) == SQLITE_OK;
    case X3_TAG_OBJECT:
      if (host->value_object_kind(value) == X3_OBJECT_KIND_STRING) {
        const char* text = host->value_to_cstr(runtime, value);
        return sqlite3_bind_text(stmt, index, text, -1, SQLITE_TRANSIENT) == SQLITE_OK;
      }
      return false;
    default:
      return false;
  }
}

bool bind_params(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt, X3Value params) {
  uint64_t length = 0;
  if (host->len(runtime, params, &length) != X3_STATUS_OK) {
    return false;
  }
  for (uint64_t i = 0; i < length; ++i) {
    X3Value item = x3_value_invalid();
    if (host->get_item(runtime, params, x3_value_int64(static_cast<int64_t>(i)), &item) != X3_STATUS_OK) {
      return false;
    }
    const bool ok = bind_value(host, runtime, stmt, static_cast<int>(i + 1), item);
    host->value_release(item);
    if (!ok) {
      return false;
    }
  }
  return true;
}

X3Value column_value(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt, int column) {
  switch (sqlite3_column_type(stmt, column)) {
    case SQLITE_INTEGER:
      return x3_value_int64(static_cast<int64_t>(sqlite3_column_int64(stmt, column)));
    case SQLITE_FLOAT:
      return x3_value_double(sqlite3_column_double(stmt, column));
    case SQLITE_TEXT:
      return host->value_string(runtime, reinterpret_cast<const char*>(sqlite3_column_text(stmt, column)));
    case SQLITE_NULL:
      return x3_value_none();
    case SQLITE_BLOB:
    default:
      return host->value_string(runtime, reinterpret_cast<const char*>(sqlite3_column_text(stmt, column)));
  }
}

X3Value row_list(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt) {
  X3Value row = host->value_list(runtime);
  const int count = sqlite3_column_count(stmt);
  for (int i = 0; i < count; ++i) {
    X3Value value = column_value(host, runtime, stmt, i);
    host->list_append(runtime, row, value);
    host->value_release(value);
  }
  return row;
}

X3Value row_dict(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt) {
  X3Value row = host->value_dict(runtime);
  const int count = sqlite3_column_count(stmt);
  for (int i = 0; i < count; ++i) {
    X3Value key = host->value_string(runtime, sqlite3_column_name(stmt, i));
    X3Value value = column_value(host, runtime, stmt, i);
    host->dict_set_item(runtime, row, key, value);
    host->value_release(key);
    host->value_release(value);
  }
  return row;
}

} // namespace xlang3_sqlite
