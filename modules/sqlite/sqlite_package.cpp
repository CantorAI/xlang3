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
#include "sqlite_values.h"

#include "xlang3/xmodule.h"

#include <cctype>
#include <string>

#if defined(_WIN32)
#define X3_SQLITE_EXPORT __declspec(dllexport)
#else
#define X3_SQLITE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

struct PackageState {
  const X3PackageHost* host = nullptr;
  X3Value connection_class = x3_value_invalid();
  X3Value cursor_class = x3_value_invalid();
  X3Value database_class = x3_value_invalid();
  X3Value statement_class = x3_value_invalid();
  X3Value error_class = x3_value_invalid();
  X3Value database_error_class = x3_value_invalid();
  X3Value operational_error_class = x3_value_invalid();
  X3Value programming_error_class = x3_value_invalid();
};

PackageState* state_from(void* user_data) {
  return static_cast<PackageState*>(user_data);
}

void cleanup_package_state(void* data) {
  auto* state = state_from(data);
  if (state == nullptr) {
    return;
  }
  if (state->host != nullptr) {
    state->host->value_release(state->connection_class);
    state->host->value_release(state->cursor_class);
    state->host->value_release(state->database_class);
    state->host->value_release(state->statement_class);
    state->host->value_release(state->error_class);
    state->host->value_release(state->database_error_class);
    state->host->value_release(state->operational_error_class);
    state->host->value_release(state->programming_error_class);
  }
  delete state;
}

void raise_sqlite_error(PackageState* state, X3CallContext* context, sqlite3* db, const char* prefix) {
  const char* sqlite_error = db == nullptr ? "database is closed" : sqlite3_errmsg(db);
  const std::string error = std::string(prefix) + ": " + sqlite_error;
  state->host->raise_error(context, state->operational_error_class, error.c_str());
}

bool check_argc(const X3PackageHost* host, X3CallContext* context, uint32_t argc, uint32_t expected, const char* name) {
  if (argc == expected) {
    return true;
  }
  const std::string error = std::string(name) + " expected " + std::to_string(expected) + " arguments";
  host->raise_class_error(context, "TypeError", error.c_str());
  return false;
}

bool is_write_sql(const char* sql) {
  while (*sql != '\0' && std::isspace(static_cast<unsigned char>(*sql))) {
    ++sql;
  }
  std::string head;
  while (*sql != '\0' && std::isalpha(static_cast<unsigned char>(*sql)) && head.size() < 8) {
    head.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*sql++))));
  }
  return head == "insert" || head == "update" || head == "delete" || head == "replace";
}

bool ensure_transaction(xlang3_sqlite::ConnectionHandle* connection) {
  if (connection == nullptr || connection->db == nullptr || connection->closed) {
    return false;
  }
  if (connection->closed) {
    return false;
  }
  return true;
}

bool begin_python_transaction(xlang3_sqlite::ConnectionHandle* connection, const char* sql) {
  if (connection == nullptr || connection->db == nullptr || connection->closed) {
    return false;
  }
  if (!is_write_sql(sql) || !sqlite3_get_autocommit(connection->db)) {
    return true;
  }
  char* message = nullptr;
  const int rc = sqlite3_exec(connection->db, "BEGIN", nullptr, nullptr, &message);
  if (message != nullptr) {
    sqlite3_free(message);
  }
  return rc == SQLITE_OK;
}

X3Status make_connection(
    PackageState* state,
    X3CallContext* context,
    X3Runtime* runtime,
    const char* path,
    X3Value klass,
    X3Value* result) {
  auto* host = state->host;
  sqlite3* db = nullptr;
  const int rc = sqlite3_open(path, &db);
  if (rc != SQLITE_OK) {
    raise_sqlite_error(state, context, db, "sqlite3.open failed");
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return X3_STATUS_ERROR;
  }
  X3Value instance = host->value_instance(runtime, klass);
  if (instance.tag == X3_TAG_INVALID) {
    sqlite3_close(db);
    host->set_error(context, "cannot create sqlite connection instance");
    return X3_STATUS_ERROR;
  }
  auto* handle = new xlang3_sqlite::ConnectionHandle();
  handle->db = db;
  if (host->instance_set_native_data(instance, xlang3_sqlite::kConnectionType, handle, xlang3_sqlite::cleanup_connection) != X3_STATUS_OK) {
    xlang3_sqlite::cleanup_connection(handle);
    host->value_release(instance);
    host->set_error(context, "cannot attach sqlite connection handle");
    return X3_STATUS_ERROR;
  }
  *result = instance;
  return X3_STATUS_OK;
}

X3Status connection_init(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 2, "Connection.__init__()")) {
    return X3_STATUS_ERROR;
  }
  const char* path = nullptr;
  if (!xlang3_sqlite::require_string(host, context, runtime, args[1], "sqlite path must be a string", &path)) {
    return X3_STATUS_ERROR;
  }
  sqlite3* db = nullptr;
  const int rc = sqlite3_open(path, &db);
  if (rc != SQLITE_OK) {
    raise_sqlite_error(state, context, db, "sqlite3.open failed");
    if (db != nullptr) sqlite3_close(db);
    return X3_STATUS_ERROR;
  }
  auto* handle = new xlang3_sqlite::ConnectionHandle();
  handle->db = db;
  if (host->instance_set_native_data(args[0], xlang3_sqlite::kConnectionType, handle, xlang3_sqlite::cleanup_connection) != X3_STATUS_OK) {
    xlang3_sqlite::cleanup_connection(handle);
    host->set_error(context, "cannot attach sqlite connection handle");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status sqlite3_connect(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 1, "sqlite3.connect()")) {
    return X3_STATUS_ERROR;
  }
  const char* path = nullptr;
  if (!xlang3_sqlite::require_string(host, context, runtime, args[0], "sqlite3.connect() path must be a string", &path)) {
    return X3_STATUS_ERROR;
  }
  return make_connection(state, context, runtime, path, state->connection_class, result);
}

X3Status connection_cursor(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 1, "Connection.cursor()")) {
    return X3_STATUS_ERROR;
  }
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  if (connection == nullptr || connection->db == nullptr || connection->closed) {
    host->raise_error(context, state->programming_error_class, "sqlite connection is closed");
    return X3_STATUS_ERROR;
  }
  X3Value cursor = host->value_instance(runtime, state->cursor_class);
  auto* handle = new xlang3_sqlite::CursorHandle();
  handle->connection = connection;
  xlang3_sqlite::retain_connection(connection);
  if (host->instance_set_native_data(cursor, xlang3_sqlite::kCursorType, handle, xlang3_sqlite::cleanup_cursor) != X3_STATUS_OK) {
    xlang3_sqlite::cleanup_cursor(handle);
    host->value_release(cursor);
    host->set_error(context, "cannot attach sqlite cursor handle");
    return X3_STATUS_ERROR;
  }
  *result = cursor;
  return X3_STATUS_OK;
}

X3Status connection_enter(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Connection.__enter__()")) return X3_STATUS_ERROR;
  *result = args[0];
  return X3_STATUS_OK;
}

X3Status connection_commit(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Connection.commit()")) return X3_STATUS_ERROR;
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  if (connection == nullptr || !ensure_transaction(connection)) {
    host->raise_error(context, state_from(user_data)->programming_error_class, "sqlite connection is closed");
    return X3_STATUS_ERROR;
  }
  if (!sqlite3_get_autocommit(connection->db) && sqlite3_exec(connection->db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
    raise_sqlite_error(state_from(user_data), context, connection->db, "commit failed");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status connection_exit(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 4, "Connection.__exit__()")) return X3_STATUS_ERROR;
  X3Value ignored = x3_value_invalid();
  const X3Status status = connection_commit(context, runtime, user_data, args, 1, &ignored);
  host->value_release(ignored);
  if (status != X3_STATUS_OK) {
    return status;
  }
  *result = x3_value_bool(0);
  return X3_STATUS_OK;
}

X3Status connection_rollback(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Connection.rollback()")) return X3_STATUS_ERROR;
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  if (connection == nullptr || !ensure_transaction(connection)) {
    host->raise_error(context, state_from(user_data)->programming_error_class, "sqlite connection is closed");
    return X3_STATUS_ERROR;
  }
  if (!sqlite3_get_autocommit(connection->db) && sqlite3_exec(connection->db, "ROLLBACK", nullptr, nullptr, nullptr) != SQLITE_OK) {
    raise_sqlite_error(state_from(user_data), context, connection->db, "rollback failed");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status connection_close(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Connection.close()")) return X3_STATUS_ERROR;
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  if (connection != nullptr && connection->db != nullptr) {
    sqlite3_close_v2(connection->db);
    connection->db = nullptr;
    connection->closed = true;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status cursor_close(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Cursor.close()")) return X3_STATUS_ERROR;
  auto* cursor = xlang3_sqlite::cursor_from(host, args[0]);
  if (cursor != nullptr && cursor->stmt != nullptr) {
    sqlite3_finalize(cursor->stmt);
    cursor->stmt = nullptr;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status cursor_enter(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Cursor.__enter__()")) return X3_STATUS_ERROR;
  *result = args[0];
  return X3_STATUS_OK;
}

X3Status cursor_exit(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 4, "Cursor.__exit__()")) return X3_STATUS_ERROR;
  X3Value ignored = x3_value_invalid();
  const X3Status status = cursor_close(context, runtime, user_data, args, 1, &ignored);
  host->value_release(ignored);
  if (status != X3_STATUS_OK) {
    return status;
  }
  *result = x3_value_bool(0);
  return X3_STATUS_OK;
}

X3Status cursor_execute(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (argc != 2 && argc != 3) {
    host->raise_class_error(context, "TypeError", "Cursor.execute() expected 1 or 2 arguments");
    return X3_STATUS_ERROR;
  }
  auto* cursor = xlang3_sqlite::cursor_from(host, args[0]);
  if (cursor == nullptr || cursor->connection == nullptr || cursor->connection->db == nullptr || cursor->connection->closed) {
    host->raise_error(context, state_from(user_data)->programming_error_class, "sqlite cursor is closed");
    return X3_STATUS_ERROR;
  }
  const char* sql = nullptr;
  if (!xlang3_sqlite::require_string(host, context, runtime, args[1], "Cursor.execute() SQL must be a string", &sql)) {
    return X3_STATUS_ERROR;
  }
  if (cursor->stmt != nullptr) {
    sqlite3_finalize(cursor->stmt);
    cursor->stmt = nullptr;
  }
  if (!begin_python_transaction(cursor->connection, sql)) {
    raise_sqlite_error(state_from(user_data), context, cursor->connection->db, "begin transaction failed");
    return X3_STATUS_ERROR;
  }
  if (sqlite3_prepare_v2(cursor->connection->db, sql, -1, &cursor->stmt, nullptr) != SQLITE_OK) {
    raise_sqlite_error(state_from(user_data), context, cursor->connection->db, "prepare failed");
    return X3_STATUS_ERROR;
  }
  if (argc == 3 && !xlang3_sqlite::bind_params(host, runtime, cursor->stmt, args[2])) {
    raise_sqlite_error(state_from(user_data), context, cursor->connection->db, "bind failed");
    return X3_STATUS_ERROR;
  }
  if (sqlite3_column_count(cursor->stmt) == 0) {
    const int rc = sqlite3_step(cursor->stmt);
    if (rc != SQLITE_DONE) {
      raise_sqlite_error(state_from(user_data), context, cursor->connection->db, "execute failed");
      return X3_STATUS_ERROR;
    }
  }
  *result = args[0];
  return X3_STATUS_OK;
}

X3Status cursor_fetchone(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Cursor.fetchone()")) return X3_STATUS_ERROR;
  auto* cursor = xlang3_sqlite::cursor_from(host, args[0]);
  if (cursor == nullptr || cursor->stmt == nullptr) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  const int rc = sqlite3_step(cursor->stmt);
  if (rc == SQLITE_ROW) {
    *result = xlang3_sqlite::row_list(host, runtime, cursor->stmt);
    return X3_STATUS_OK;
  }
  if (rc == SQLITE_DONE) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  raise_sqlite_error(state_from(user_data), context, cursor->connection->db, "fetchone failed");
  return X3_STATUS_ERROR;
}

X3Status cursor_fetchall(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Cursor.fetchall()")) return X3_STATUS_ERROR;
  auto* cursor = xlang3_sqlite::cursor_from(host, args[0]);
  X3Value rows = host->value_list(runtime);
  if (cursor == nullptr || cursor->stmt == nullptr) {
    *result = rows;
    return X3_STATUS_OK;
  }
  while (true) {
    const int rc = sqlite3_step(cursor->stmt);
    if (rc == SQLITE_DONE) {
      *result = rows;
      return X3_STATUS_OK;
    }
    if (rc != SQLITE_ROW) {
      host->value_release(rows);
      raise_sqlite_error(state_from(user_data), context, cursor->connection->db, "fetchall failed");
      return X3_STATUS_ERROR;
    }
    X3Value row = xlang3_sqlite::row_list(host, runtime, cursor->stmt);
    host->list_append(runtime, rows, row);
    host->value_release(row);
  }
}

X3Status database_exec(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 2, "Database.exec()")) return X3_STATUS_ERROR;
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  const char* sql = nullptr;
  if (!xlang3_sqlite::require_string(host, context, runtime, args[1], "Database.exec() SQL must be a string", &sql)) return X3_STATUS_ERROR;
  char* message = nullptr;
  const int rc = sqlite3_exec(connection->db, sql, nullptr, nullptr, &message);
  if (message != nullptr) sqlite3_free(message);
  *result = x3_value_int64(rc);
  return X3_STATUS_OK;
}

X3Status database_statement(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 2, "Database.statement()")) return X3_STATUS_ERROR;
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  const char* sql = nullptr;
  if (!xlang3_sqlite::require_string(host, context, runtime, args[1], "Database.statement() SQL must be a string", &sql)) return X3_STATUS_ERROR;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(connection->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    raise_sqlite_error(state, context, connection->db, "prepare failed");
    return X3_STATUS_ERROR;
  }
  X3Value instance = host->value_instance(runtime, state->statement_class);
  auto* handle = new xlang3_sqlite::StatementHandle();
  handle->connection = connection;
  handle->stmt = stmt;
  xlang3_sqlite::retain_connection(connection);
  if (host->instance_set_native_data(instance, xlang3_sqlite::kStatementType, handle, xlang3_sqlite::cleanup_statement) != X3_STATUS_OK) {
    xlang3_sqlite::cleanup_statement(handle);
    host->value_release(instance);
    host->set_error(context, "cannot attach sqlite statement handle");
    return X3_STATUS_ERROR;
  }
  *result = instance;
  return X3_STATUS_OK;
}

X3Status database_begin(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Database.beginTransaction()")) return X3_STATUS_ERROR;
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  *result = x3_value_bool(sqlite3_exec(connection->db, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK);
  return X3_STATUS_OK;
}

X3Status database_end(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Database.endTransaction()")) return X3_STATUS_ERROR;
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  *result = x3_value_bool(sqlite3_exec(connection->db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK);
  return X3_STATUS_OK;
}

X3Status database_close(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  const X3Status status = connection_close(context, runtime, user_data, args, argc, result);
  if (status == X3_STATUS_OK) {
    *result = x3_value_bool(1);
  }
  return status;
}

X3Status statement_bind(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 3, "Statement.bind()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  if (args[1].tag != X3_TAG_INT64) {
    host->set_error(context, "Statement.bind() index must be int");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_bool(xlang3_sqlite::bind_value(host, runtime, statement->stmt, static_cast<int>(args[1].as.i64), args[2]));
  return X3_STATUS_OK;
}

X3Status statement_step(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Statement.step()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  if (statement->last_rc == SQLITE_DONE) {
    *result = x3_value_int64(SQLITE_DONE);
    return X3_STATUS_OK;
  }
  statement->last_rc = sqlite3_step(statement->stmt);
  *result = x3_value_int64(statement->last_rc);
  return X3_STATUS_OK;
}

X3Status statement_reset(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Statement.reset()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  *result = x3_value_bool(sqlite3_reset(statement->stmt) == SQLITE_OK);
  statement->last_rc = SQLITE_OK;
  sqlite3_clear_bindings(statement->stmt);
  return X3_STATUS_OK;
}

X3Status statement_close(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Statement.close()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  if (statement != nullptr && statement->stmt != nullptr) {
    sqlite3_finalize(statement->stmt);
    statement->stmt = nullptr;
  }
  *result = x3_value_bool(1);
  return X3_STATUS_OK;
}

X3Status statement_colnum(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Statement.colnum()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  *result = x3_value_int64(sqlite3_column_count(statement->stmt));
  return X3_STATUS_OK;
}

X3Status statement_colname(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 2, "Statement.colname()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  *result = host->value_string(runtime, sqlite3_column_name(statement->stmt, static_cast<int>(args[1].as.i64)));
  return X3_STATUS_OK;
}

X3Status statement_get(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 2, "Statement.get()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  *result = xlang3_sqlite::column_value(host, runtime, statement->stmt, static_cast<int>(args[1].as.i64));
  return X3_STATUS_OK;
}

X3Status statement_fetchall(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Statement.fetchall()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  X3Value rows = host->value_list(runtime);
  while (true) {
    const int rc = sqlite3_step(statement->stmt);
    statement->last_rc = rc;
    if (rc == SQLITE_DONE) {
      *result = rows;
      return X3_STATUS_OK;
    }
    if (rc != SQLITE_ROW) {
      host->value_release(rows);
      raise_sqlite_error(state_from(user_data), context, statement->connection->db, "fetchall failed");
      return X3_STATUS_ERROR;
    }
    X3Value row = xlang3_sqlite::row_list(host, runtime, statement->stmt);
    host->list_append(runtime, rows, row);
    host->value_release(row);
  }
}

X3Status statement_fetchall_dict(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Statement.fetchallDict()")) return X3_STATUS_ERROR;
  auto* statement = xlang3_sqlite::statement_from(host, args[0]);
  X3Value rows = host->value_list(runtime);
  while (true) {
    const int rc = sqlite3_step(statement->stmt);
    statement->last_rc = rc;
    if (rc == SQLITE_DONE) {
      *result = rows;
      return X3_STATUS_OK;
    }
    if (rc != SQLITE_ROW) {
      host->value_release(rows);
      raise_sqlite_error(state_from(user_data), context, statement->connection->db, "fetchallDict failed");
      return X3_STATUS_ERROR;
    }
    X3Value row = xlang3_sqlite::row_dict(host, runtime, statement->stmt);
    host->list_append(runtime, rows, row);
    host->value_release(row);
  }
}

void add_function(const X3PackageHost* host, X3Module* module, const char* name, X3NativeFn callback, PackageState* state) {
  X3NativeFunctionDef def{};
  def.size = sizeof(def);
  def.name = name;
  def.callback = callback;
  def.user_data = state;
  host->module_add_function(module, &def);
}

void def_method(X3NativeFunctionDef& def, const char* name, X3NativeFn callback, PackageState* state) {
  def.size = sizeof(def);
  def.name = name;
  def.callback = callback;
  def.user_data = state;
}

} // namespace

extern "C" X3_SQLITE_EXPORT X3Status x3_package_init(const X3PackageHost* host, X3Package* package) {
  if (host == nullptr || host->abi_version != X3_ABI_VERSION) {
    return X3_STATUS_ERROR;
  }
  host->package_set_metadata(package, "package", "xlang_sqlite3");
  host->package_set_metadata(package, "version", "0.1.0");
  host->package_set_metadata(package, "abi", "7");
  auto* state = new PackageState();
  state->host = host;
  host->package_set_cleanup(package, state, cleanup_package_state);

  X3Module* sqlite3 = nullptr;
  X3Module* sqlite = nullptr;
  if (host->add_module(package, "sqlite3", &sqlite3) != X3_STATUS_OK ||
      host->add_module(package, "sqlite", &sqlite) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  if (host->module_add_class(sqlite3, "Error", nullptr, 0, &state->error_class) != X3_STATUS_OK ||
      host->module_add_class(sqlite3, "DatabaseError", nullptr, 0, &state->database_error_class) != X3_STATUS_OK ||
      host->module_add_class(sqlite3, "OperationalError", nullptr, 0, &state->operational_error_class) != X3_STATUS_OK ||
      host->module_add_class(sqlite3, "ProgrammingError", nullptr, 0, &state->programming_error_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  X3Value exception_class = x3_value_invalid();
  if (host->builtin_value(package, "Exception", &exception_class) != X3_STATUS_OK ||
      host->class_set_base(state->error_class, exception_class) != X3_STATUS_OK ||
      host->class_set_base(state->database_error_class, state->error_class) != X3_STATUS_OK ||
      host->class_set_base(state->operational_error_class, state->database_error_class) != X3_STATUS_OK ||
      host->class_set_base(state->programming_error_class, state->database_error_class) != X3_STATUS_OK) {
    host->value_release(exception_class);
    return X3_STATUS_ERROR;
  }
  host->value_release(exception_class);
  host->module_add_value(sqlite, "Error", state->error_class);
  host->module_add_value(sqlite, "DatabaseError", state->database_error_class);
  host->module_add_value(sqlite, "OperationalError", state->operational_error_class);
  host->module_add_value(sqlite, "ProgrammingError", state->programming_error_class);

  X3NativeFunctionDef connection_methods[7]{};
  def_method(connection_methods[0], "__init__", connection_init, state);
  def_method(connection_methods[1], "cursor", connection_cursor, state);
  def_method(connection_methods[2], "commit", connection_commit, state);
  def_method(connection_methods[3], "rollback", connection_rollback, state);
  def_method(connection_methods[4], "close", connection_close, state);
  def_method(connection_methods[5], "__enter__", connection_enter, state);
  def_method(connection_methods[6], "__exit__", connection_exit, state);
  if (host->module_add_class(sqlite3, "Connection", connection_methods, 7, &state->connection_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  X3NativeFunctionDef cursor_methods[6]{};
  def_method(cursor_methods[0], "execute", cursor_execute, state);
  def_method(cursor_methods[1], "fetchone", cursor_fetchone, state);
  def_method(cursor_methods[2], "fetchall", cursor_fetchall, state);
  def_method(cursor_methods[3], "close", cursor_close, state);
  def_method(cursor_methods[4], "__enter__", cursor_enter, state);
  def_method(cursor_methods[5], "__exit__", cursor_exit, state);
  if (host->module_add_class(sqlite3, "Cursor", cursor_methods, 6, &state->cursor_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  add_function(host, sqlite3, "connect", sqlite3_connect, state);

  X3NativeFunctionDef database_methods[8]{};
  def_method(database_methods[0], "__init__", connection_init, state);
  def_method(database_methods[1], "exec", database_exec, state);
  def_method(database_methods[2], "statement", database_statement, state);
  def_method(database_methods[3], "beginTransaction", database_begin, state);
  def_method(database_methods[4], "endTransaction", database_end, state);
  def_method(database_methods[5], "close", database_close, state);
  def_method(database_methods[6], "__enter__", connection_enter, state);
  def_method(database_methods[7], "__exit__", connection_exit, state);
  if (host->module_add_class(sqlite, "Database", database_methods, 8, &state->database_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  add_function(host, sqlite, "connect", sqlite3_connect, state);

  X3NativeFunctionDef statement_methods[9]{};
  def_method(statement_methods[0], "bind", statement_bind, state);
  def_method(statement_methods[1], "step", statement_step, state);
  def_method(statement_methods[2], "reset", statement_reset, state);
  def_method(statement_methods[3], "close", statement_close, state);
  def_method(statement_methods[4], "colnum", statement_colnum, state);
  def_method(statement_methods[5], "colname", statement_colname, state);
  def_method(statement_methods[6], "get", statement_get, state);
  def_method(statement_methods[7], "fetchall", statement_fetchall, state);
  def_method(statement_methods[8], "fetchallDict", statement_fetchall_dict, state);
  if (host->module_add_class(sqlite, "Statement", statement_methods, 9, &state->statement_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  host->module_add_value(sqlite, "OK", x3_value_int64(SQLITE_OK));
  host->module_add_value(sqlite, "ROW", x3_value_int64(SQLITE_ROW));
  host->module_add_value(sqlite, "DONE", x3_value_int64(SQLITE_DONE));
  host->module_add_value(sqlite3, "OK", x3_value_int64(SQLITE_OK));
  host->module_add_value(sqlite3, "ROW", x3_value_int64(SQLITE_ROW));
  host->module_add_value(sqlite3, "DONE", x3_value_int64(SQLITE_DONE));
  return X3_STATUS_OK;
}
