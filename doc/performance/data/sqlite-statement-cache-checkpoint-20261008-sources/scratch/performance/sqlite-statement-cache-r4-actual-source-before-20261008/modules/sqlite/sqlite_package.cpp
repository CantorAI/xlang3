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

#include "xlang3/xlang3.h"

#include <atomic>
#include <cctype>
#include <charconv>
#include <climits>
#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value connection_class = x3_value_invalid();
  X3Value cursor_class = x3_value_invalid();
  X3Value database_class = x3_value_invalid();
  X3Value statement_class = x3_value_invalid();
  X3Value row_class = x3_value_invalid();
  X3Value error_class = x3_value_invalid();
  X3Value database_error_class = x3_value_invalid();
  X3Value operational_error_class = x3_value_invalid();
  X3Value programming_error_class = x3_value_invalid();
  X3Value data_error_class = x3_value_invalid();
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
    state->host->value_release(state->row_class);
    state->host->value_release(state->error_class);
    state->host->value_release(state->database_error_class);
    state->host->value_release(state->operational_error_class);
    state->host->value_release(state->programming_error_class);
    state->host->value_release(state->data_error_class);
  }
  delete state;
}

void raise_sqlite_error(PackageState* state, X3CallContext* context, sqlite3* db, const char* prefix) {
  const char* sqlite_error = db == nullptr ? "database is closed" : sqlite3_errmsg(db);
  const int code = db == nullptr ? SQLITE_ERROR : (sqlite3_errcode(db) & 0xff);
  if (code == SQLITE_NOMEM) {
    state->host->raise_class_error(context, "MemoryError", sqlite_error);
    return;
  }
  if (code == SQLITE_TOOBIG) {
    state->host->raise_error(context, state->data_error_class, sqlite_error);
    return;
  }
  // Aggregate callback messages are the public error, without an extra cursor
  // prefix. Other existing SQLite diagnostics keep their current formatting.
  const bool aggregate_error = std::string_view(sqlite_error).find("user-defined aggregate's '") == 0;
  const std::string error = aggregate_error ? std::string(sqlite_error)
      : std::string(prefix) + ": " + sqlite_error;
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

bool set_public_dbapi_module(X3PackageHost* host, X3Value klass) {
  X3Value module_name = host->value_string_utf8(host->runtime, "sqlite3", 7);
  if (module_name.tag == X3_TAG_INVALID) return false;
  const bool ok = host->set_attr(
      host->runtime, klass, "__module__", module_name) == X3_STATUS_OK;
  host->value_release(module_name);
  return ok;
}

X3Status sqlite3_connect_kw(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    const X3KeywordArg* kwargs,
    uint32_t kwargc,
    X3Value* result) {
  auto* state = state_from(user_data);
  for (uint32_t index = 0; index < kwargc; ++index) {
    const char* name = kwargs[index].name;
    if (name == nullptr || std::string_view(name) != "check_same_thread") {
      const std::string message = std::string("'") +
          (name == nullptr ? "" : name) +
          "' is an invalid keyword argument for Connection()";
      state->host->raise_class_error(context, "TypeError", message.c_str());
      return X3_STATUS_ERROR;
    }
    if (kwargs[index].value.tag != X3_TAG_BOOL && kwargs[index].value.tag != X3_TAG_INT64) {
      state->host->raise_class_error(
          context, "TypeError", "check_same_thread must be a boolean");
      return X3_STATUS_ERROR;
    }
  }
  return sqlite3_connect(context, runtime, user_data, args, argc, result);
}

struct ScalarFunction {
  const X3PackageHost* host = nullptr;
  X3Runtime* runtime = nullptr;
  X3Value callable = x3_value_invalid();
};

void destroy_scalar_function(void* raw) {
  auto* function = static_cast<ScalarFunction*>(raw);
  if (function == nullptr) return;
  function->host->value_release(function->callable);
  delete function;
}

void invoke_scalar_function(sqlite3_context* context, int argc, sqlite3_value** argv) {
  auto* function = static_cast<ScalarFunction*>(sqlite3_user_data(context));
  std::vector<X3Value> values;
  values.reserve(static_cast<size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    switch (sqlite3_value_type(argv[index])) {
      case SQLITE_NULL:
        values.push_back(x3_value_none());
        break;
      case SQLITE_INTEGER:
        values.push_back(x3_value_int64(sqlite3_value_int64(argv[index])));
        break;
      case SQLITE_FLOAT:
        values.push_back(x3_value_double(sqlite3_value_double(argv[index])));
        break;
      case SQLITE_TEXT: {
        const auto* data = reinterpret_cast<const char*>(sqlite3_value_text(argv[index]));
        const auto size = static_cast<uint64_t>(sqlite3_value_bytes(argv[index]));
        values.push_back(function->host->value_string_utf8(function->runtime, data, size));
        break;
      }
      case SQLITE_BLOB: {
        const void* data = sqlite3_value_blob(argv[index]);
        const auto size = static_cast<uint64_t>(sqlite3_value_bytes(argv[index]));
        values.push_back(function->host->value_bytes(function->runtime, data, size));
        break;
      }
      default:
        values.push_back(x3_value_none());
        break;
    }
  }

  X3Value output = x3_value_invalid();
  const X3Status status = function->host->call(
      function->runtime, function->callable, values.data(),
      static_cast<uint32_t>(values.size()), &output);
  for (const auto& value : values) function->host->value_release(value);
  if (status != X3_STATUS_OK) {
    const char* message = function->host->runtime_last_error(function->runtime);
    sqlite3_result_error(context, message == nullptr ? "user-defined function raised an exception" : message, -1);
    return;
  }

  switch (output.tag) {
    case X3_TAG_NONE:
    case X3_TAG_INVALID:
      sqlite3_result_null(context);
      break;
    case X3_TAG_BOOL:
      sqlite3_result_int(context, output.as.b != 0 ? 1 : 0);
      break;
    case X3_TAG_INT64:
      sqlite3_result_int64(context, output.as.i64);
      break;
    case X3_TAG_UINT64:
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(output.as.u64));
      break;
    case X3_TAG_DOUBLE:
      sqlite3_result_double(context, output.as.f64);
      break;
    case X3_TAG_OBJECT: {
      const X3ObjectKind kind = function->host->value_object_kind(output);
      if (kind == X3_OBJECT_KIND_STRING) {
        const char* data = nullptr;
        uint64_t size = 0;
        if (function->host->value_string_data(function->runtime, output, &data, &size) == X3_STATUS_OK) {
          sqlite3_result_text64(context, data, size, SQLITE_TRANSIENT, SQLITE_UTF8);
        } else {
          sqlite3_result_error(context, "cannot convert function result to SQLite text", -1);
        }
      } else if (kind == X3_OBJECT_KIND_BYTES || kind == X3_OBJECT_KIND_BYTEARRAY) {
        const void* data = nullptr;
        uint64_t size = 0;
        if (function->host->value_bytes_data(function->runtime, output, &data, &size) == X3_STATUS_OK) {
          sqlite3_result_blob64(context, data, size, SQLITE_TRANSIENT);
        } else {
          sqlite3_result_error(context, "cannot convert function result to SQLite blob", -1);
        }
      } else {
        sqlite3_result_error(context, "user-defined function returned an unsupported value", -1);
      }
      break;
    }
    default:
      sqlite3_result_error(context, "user-defined function returned an unsupported value", -1);
      break;
  }
  function->host->value_release(output);
}

X3Status connection_create_function_kw(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    const X3KeywordArg* kwargs,
    uint32_t kwargc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (argc != 4) {
    host->raise_class_error(context, "TypeError", "create_function() expected name, narg, and func");
    return X3_STATUS_ERROR;
  }
  bool deterministic = false;
  for (uint32_t index = 0; index < kwargc; ++index) {
    const char* name = kwargs[index].name;
    if (name == nullptr || std::string_view(name) != "deterministic") {
      host->raise_class_error(context, "TypeError", "create_function() got an unexpected keyword argument");
      return X3_STATUS_ERROR;
    }
    const X3Value value = kwargs[index].value;
    if (value.tag == X3_TAG_BOOL) deterministic = value.as.b != 0;
    else if (value.tag == X3_TAG_INT64) deterministic = value.as.i64 != 0;
    else {
      host->raise_class_error(context, "TypeError", "deterministic must be a boolean");
      return X3_STATUS_ERROR;
    }
  }
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  if (connection == nullptr || connection->db == nullptr || connection->closed) {
    host->raise_error(context, state->programming_error_class, "sqlite connection is closed");
    return X3_STATUS_ERROR;
  }
  const char* name = nullptr;
  if (!xlang3_sqlite::require_string(host, context, runtime, args[1],
                                     "create_function() name must be a string", &name)) {
    return X3_STATUS_ERROR;
  }
  if (args[2].tag != X3_TAG_INT64) {
    host->raise_class_error(context, "TypeError", "create_function() narg must be an integer");
    return X3_STATUS_ERROR;
  }
  auto* function = new ScalarFunction{host, runtime, args[3]};
  host->value_retain(function->callable);
  const int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
  const int rc = sqlite3_create_function_v2(
      connection->db, name, static_cast<int>(args[2].as.i64), flags,
      function, invoke_scalar_function, nullptr, nullptr, destroy_scalar_function);
  if (rc != SQLITE_OK) {
    destroy_scalar_function(function);
    raise_sqlite_error(state, context, connection->db, "create_function failed");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status connection_create_function(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  return connection_create_function_kw(
      context, runtime, user_data, args, argc, nullptr, 0, result);
}

// Own native aggregate bridge. Python aggregate classes and their algorithms
// remain Python; SQLite invokes the same ordinary call/descriptor paths.
struct SqlAggregateFunction {
  const X3PackageHost* host;
  X3Runtime* runtime;
  X3Value factory;
  std::atomic<uint32_t> references{1};
};

void release_sql_aggregate(void* raw) {
  auto* function = static_cast<SqlAggregateFunction*>(raw);
  if (function && function->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    const X3Value factory = function->factory;
    function->factory = x3_value_invalid();
    function->host->value_release(factory);
    delete function;
  }
}

struct SqlAggregateLease {
  SqlAggregateFunction* function;
  explicit SqlAggregateLease(SqlAggregateFunction* value) : function(value) {
    function->references.fetch_add(1, std::memory_order_relaxed);
  }
  ~SqlAggregateLease() { release_sql_aggregate(function); }
};

struct SqlOwnedValue {
  const X3PackageHost* host;
  X3Value value = x3_value_invalid();
  explicit SqlOwnedValue(const X3PackageHost* value_host) : host(value_host) {}
  SqlOwnedValue(const SqlOwnedValue&) = delete;
  SqlOwnedValue& operator=(const SqlOwnedValue&) = delete;
  ~SqlOwnedValue() { host->value_release(value); }
};

// Finalization can run from native payload cleanup, without an X3CallContext.
// Preserve an outer pending exception while consuming only callback failures.
struct SqlCallbackExceptions {
  SqlAggregateFunction* function;
  SqlOwnedValue previous;
  explicit SqlCallbackExceptions(SqlAggregateFunction* value)
      : function(value), previous(value->host) {
    function->host->runtime_take_exception(function->runtime, &previous.value);
  }
  ~SqlCallbackExceptions() {
    SqlOwnedValue discarded(function->host);
    function->host->runtime_take_exception(function->runtime, &discarded.value);
    // Release callback errors before restoring the outer exception: release can
    // itself run finalizers. Restore takes its own reference to the saved value.
    function->host->value_release(discarded.value);
    discarded.value = x3_value_invalid();
    function->host->runtime_restore_exception(function->runtime, previous.value);
  }
};

bool sql_instance_of(SqlAggregateFunction* function, X3Value value,
                     const char* builtin_name, bool& matched) {
  SqlOwnedValue builtin(function->host), type_function(function->host), actual_type(function->host),
      predicate(function->host), result(function->host);
  if (function->host->builtin_value(const_cast<X3PackageHost*>(function->host),
                                   builtin_name, &builtin.value) != X3_STATUS_OK ||
      function->host->builtin_value(const_cast<X3PackageHost*>(function->host),
                                   "type", &type_function.value) != X3_STATUS_OK ||
      function->host->builtin_value(const_cast<X3PackageHost*>(function->host),
                                   "issubclass", &predicate.value) != X3_STATUS_OK ||
      function->host->call(function->runtime, type_function.value, &value, 1,
                           &actual_type.value) != X3_STATUS_OK)
    return false;
  // Actual ancestry, not isinstance(value,...): __class__ can be spoofed.
  const X3Value args[] = {actual_type.value, builtin.value};
  if (function->host->call(function->runtime, predicate.value, args, 2,
                           &result.value) != X3_STATUS_OK || result.value.tag != X3_TAG_BOOL)
    return false;
  matched = result.value.as.b != 0;
  return true;
}

void sql_aggregate_error(sqlite3_context* context, SqlAggregateFunction* function,
                         const char* method, bool lookup_failure = false) {
  SqlOwnedValue exception(function->host);
  function->host->runtime_take_exception(function->runtime, &exception.value);
  bool matched = false;
  if (exception.value.tag != X3_TAG_INVALID &&
      sql_instance_of(function, exception.value, "MemoryError", matched) && matched) {
    sqlite3_result_error_nomem(context);
    return;
  }
  if (exception.value.tag != X3_TAG_INVALID &&
      sql_instance_of(function, exception.value, "OverflowError", matched) && matched) {
    sqlite3_result_error_toobig(context);
    return;
  }
  if (std::string_view(method) == "finalize" && exception.value.tag != X3_TAG_INVALID &&
      sql_instance_of(function, exception.value, "AttributeError", matched) && matched)
    lookup_failure = true;
  const std::string message = std::string("user-defined aggregate's '") + method +
      "' method " + (lookup_failure ? "not defined" : "raised error");
  sqlite3_result_error(context, message.c_str(), -1);
}

struct SqlAggregateGroup {
  // SQLite zero-initializes this POD storage. INVALID has tag zero; no C++
  // constructor or container may be placed here without explicit construction.
  X3Value instance;
};

struct SqlAggregateArguments {
  const X3PackageHost* host;
  std::vector<X3Value> values;
  explicit SqlAggregateArguments(const X3PackageHost* value_host) : host(value_host) {}
  ~SqlAggregateArguments() {
    for (const X3Value value : values) host->value_release(value);
  }
};

bool sql_aggregate_arguments(sqlite3_context* context, SqlAggregateFunction* function,
                             int argc, sqlite3_value** argv, SqlAggregateArguments& args) {
  args.values.reserve(static_cast<size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    X3Value value = x3_value_invalid();
    switch (sqlite3_value_type(argv[index])) {
      case SQLITE_NULL: value = x3_value_none(); break;
      case SQLITE_INTEGER: value = x3_value_int64(sqlite3_value_int64(argv[index])); break;
      case SQLITE_FLOAT: value = x3_value_double(sqlite3_value_double(argv[index])); break;
      case SQLITE_TEXT: {
        const char* data = reinterpret_cast<const char*>(sqlite3_value_text(argv[index]));
        const uint64_t size = static_cast<uint64_t>(sqlite3_value_bytes(argv[index]));
        if (!data && sqlite3_errcode(sqlite3_context_db_handle(context)) == SQLITE_NOMEM) {
          sqlite3_result_error_nomem(context);
          return false;
        }
        value = function->host->value_string_utf8(function->runtime, data ? data : "", size);
        break;
      }
      case SQLITE_BLOB: {
        const void* data = sqlite3_value_blob(argv[index]);
        const uint64_t size = static_cast<uint64_t>(sqlite3_value_bytes(argv[index]));
        if (!data && sqlite3_errcode(sqlite3_context_db_handle(context)) == SQLITE_NOMEM) {
          sqlite3_result_error_nomem(context);
          return false;
        }
        value = function->host->value_bytes(function->runtime, data, size);
        break;
      }
      default: value = x3_value_none(); break;
    }
    if (value.tag == X3_TAG_INVALID) {
      sqlite3_result_error_nomem(context);
      return false;
    }
    args.values.push_back(value);
  }
  return true;
}

// The exact scalar path avoids Python calls for every aggregate result. Only
// boxed scalar subclasses need the existing base constructor/method bridge.
bool sql_aggregate_result(sqlite3_context* context, SqlAggregateFunction* function,
                           X3Value output) {
  switch (output.tag) {
    case X3_TAG_NONE: sqlite3_result_null(context); return true;
    case X3_TAG_BOOL: sqlite3_result_int64(context, output.as.b ? 1 : 0); return true;
    case X3_TAG_INT64: sqlite3_result_int64(context, output.as.i64); return true;
    case X3_TAG_UINT64:
      if (output.as.u64 > static_cast<uint64_t>(INT64_MAX)) sqlite3_result_error_toobig(context);
      else sqlite3_result_int64(context, static_cast<sqlite3_int64>(output.as.u64));
      return true;
    case X3_TAG_DOUBLE: sqlite3_result_double(context, output.as.f64); return true;
    default: break;
  }
  SqlOwnedValue normalized(function->host);
  bool is_integer = false;
  if (!sql_instance_of(function, output, "int", is_integer)) return false;
  if (is_integer) {
    SqlOwnedValue int_type(function->host), index(function->host);
    if (function->host->builtin_value(const_cast<X3PackageHost*>(function->host), "int",
                                     &int_type.value) != X3_STATUS_OK ||
        function->host->get_attr(function->runtime, int_type.value, "__index__",
                                &index.value) != X3_STATUS_OK ||
        function->host->call(function->runtime, index.value, &output, 1,
                            &normalized.value) != X3_STATUS_OK) return false;
    if (normalized.value.tag == X3_TAG_INT64) {
      sqlite3_result_int64(context, normalized.value.as.i64);
      return true;
    }
    const char* text = function->host->value_to_cstr(function->runtime, normalized.value);
    if (!text) return false;
    int64_t integer = 0;
    const size_t size = std::strlen(text);
    const auto parsed = std::from_chars(text, text + size, integer);
    if (parsed.ec == std::errc::result_out_of_range) {
      sqlite3_result_error_toobig(context);
      return true;
    }
    if (parsed.ec != std::errc{} || parsed.ptr != text + size) return false;
    sqlite3_result_int64(context, integer);
    return true;
  }
  bool is_float = false;
  if (!sql_instance_of(function, output, "float", is_float)) return false;
  if (is_float) {
    SqlOwnedValue float_type(function->host);
    if (function->host->builtin_value(const_cast<X3PackageHost*>(function->host), "float",
                                     &float_type.value) != X3_STATUS_OK ||
        function->host->call(function->runtime, float_type.value, &output, 1,
                            &normalized.value) != X3_STATUS_OK ||
        normalized.value.tag != X3_TAG_DOUBLE) return false;
    sqlite3_result_double(context, normalized.value.as.f64);
    return true;
  }
  bool is_string = function->host->value_object_kind(output) == X3_OBJECT_KIND_STRING;
  if (!is_string && !sql_instance_of(function, output, "str", is_string)) return false;
  if (is_string) {
    if (function->host->value_object_kind(output) != X3_OBJECT_KIND_STRING) {
      SqlOwnedValue str_type(function->host), str_method(function->host);
      if (function->host->builtin_value(const_cast<X3PackageHost*>(function->host), "str",
                                       &str_type.value) != X3_STATUS_OK ||
          function->host->get_attr(function->runtime, str_type.value, "__str__",
                                  &str_method.value) != X3_STATUS_OK ||
          function->host->call(function->runtime, str_method.value, &output, 1,
                              &normalized.value) != X3_STATUS_OK) return false;
      output = normalized.value;
    }
    const char* data = nullptr;
    uint64_t size = 0;
    if (function->host->value_string_data(function->runtime, output, &data, &size) != X3_STATUS_OK)
      return false;
    // Runtime strings may contain surrogate code points. CPython rejects these
    // when a callback result is encoded to SQLite UTF-8; do not store WTF-8.
    for (uint64_t index = 0; index + 2 < size; ++index) {
      if (static_cast<unsigned char>(data[index]) == 0xed &&
          static_cast<unsigned char>(data[index + 1]) >= 0xa0 &&
          static_cast<unsigned char>(data[index + 1]) <= 0xbf &&
          (static_cast<unsigned char>(data[index + 2]) & 0xc0) == 0x80)
        return false;
    }
    if (size > static_cast<uint64_t>(INT_MAX)) sqlite3_result_error_toobig(context);
    else sqlite3_result_text(context, data, static_cast<int>(size), SQLITE_TRANSIENT);
    return true;
  }
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (function->host->buffer_acquire(function->runtime, output, 0, &buffer, &info) != X3_STATUS_OK) {
    // The SDK's legacy bytes payload accessor also handles native bytes
    // subclasses. Guard actual ancestry before trying that non-exporting path.
    SqlOwnedValue discarded(function->host);
    function->host->runtime_take_exception(function->runtime, &discarded.value);
    bool is_bytes = false;
    if (!sql_instance_of(function, output, "bytes", is_bytes) || !is_bytes) return false;
    const void* data = nullptr;
    uint64_t size = 0;
    if (function->host->value_bytes_data(function->runtime, output, &data, &size) != X3_STATUS_OK)
      return false;
    if (size > static_cast<uint64_t>(INT_MAX)) sqlite3_result_error_toobig(context);
    else if (size == 0) sqlite3_result_zeroblob(context, 0);
    else sqlite3_result_blob(context, data, static_cast<int>(size), SQLITE_TRANSIENT);
    return true;
  }
  if (info.size > static_cast<uint64_t>(INT_MAX)) sqlite3_result_error_toobig(context);
  else if (info.size == 0) sqlite3_result_zeroblob(context, 0);
  else sqlite3_result_blob(context, info.data, static_cast<int>(info.size), SQLITE_TRANSIENT);
  function->host->buffer_release(buffer);
  return true;
}

void sql_aggregate_step(sqlite3_context* context, int argc, sqlite3_value** argv) {
  auto* function = static_cast<SqlAggregateFunction*>(sqlite3_user_data(context));
  SqlAggregateLease lease(function);
  SqlCallbackExceptions exceptions(function);
  try {
    auto* group = static_cast<SqlAggregateGroup*>(sqlite3_aggregate_context(context, sizeof(SqlAggregateGroup)));
    if (!group) { sqlite3_result_error_nomem(context); return; }
    if (group->instance.tag == X3_TAG_INVALID) {
      if (function->host->call(function->runtime, function->factory, nullptr, 0,
                              &group->instance) != X3_STATUS_OK) {
        sql_aggregate_error(context, function, "__init__");
        return;
      }
    }
    // Resolve on every row: Python may replace step or its descriptor between
    // rows. Caching a bound method would also extend the instance's lifetime.
    SqlOwnedValue method(function->host), result(function->host);
    if (function->host->get_attr(function->runtime, group->instance, "step", &method.value) != X3_STATUS_OK) {
      sql_aggregate_error(context, function, "step", true);
      return;
    }
    SqlAggregateArguments args(function->host);
    if (!sql_aggregate_arguments(context, function, argc, argv, args)) return;
    if (function->host->call(function->runtime, method.value, args.values.data(),
                            static_cast<uint32_t>(args.values.size()), &result.value) != X3_STATUS_OK)
      sql_aggregate_error(context, function, "step");
  } catch (const std::bad_alloc&) {
    sqlite3_result_error_nomem(context);
  } catch (...) {
    sqlite3_result_error(context, "aggregate callback failed", -1);
  }
}

void sql_aggregate_final(sqlite3_context* context) {
  auto* function = static_cast<SqlAggregateFunction*>(sqlite3_user_data(context));
  SqlAggregateLease lease(function);
  SqlCallbackExceptions exceptions(function);
  auto* group = static_cast<SqlAggregateGroup*>(sqlite3_aggregate_context(context, 0));
  // Empty input never constructs the aggregate and never calls finalize.
  if (!group || group->instance.tag == X3_TAG_INVALID) return;
  SqlOwnedValue instance(function->host), method(function->host), output(function->host);
  instance.value = group->instance;
  group->instance = x3_value_invalid();
  try {
    // Detach ownership before running user code, including error finalization.
    // The registration lease survives close/replacement from a callback.
    if (function->host->get_attr(function->runtime, instance.value, "finalize", &method.value) != X3_STATUS_OK) {
      sql_aggregate_error(context, function, "finalize", true);
      return;
    }
    const X3Status status = function->host->call(
        function->runtime, method.value, nullptr, 0, &output.value);
    // Release the bound method and aggregate before converting its result.
    // An aggregate finalizer can mutate a returned bytearray; conversion must
    // observe that mutation rather than retaining the instance until afterward.
    function->host->value_release(method.value);
    method.value = x3_value_invalid();
    function->host->value_release(instance.value);
    instance.value = x3_value_invalid();
    if (status != X3_STATUS_OK || !sql_aggregate_result(context, function, output.value))
      sql_aggregate_error(context, function, "finalize");
  } catch (const std::bad_alloc&) {
    sqlite3_result_error_nomem(context);
  } catch (...) {
    sqlite3_result_error(context, "aggregate callback failed", -1);
  }
}

X3Status connection_create_aggregate_kw(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
    uint32_t kwargc, X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (argc < 1 || argc > 4) {
    host->raise_class_error(context, "TypeError", "create_aggregate() requires three arguments");
    return X3_STATUS_ERROR;
  }
  X3Value parameters[3] = {x3_value_invalid(), x3_value_invalid(), x3_value_invalid()};
  const char* names[] = {"name", "n_arg", "aggregate_class"};
  for (uint32_t index = 1; index < argc; ++index) parameters[index - 1] = args[index];
  for (uint32_t index = 0; index < kwargc; ++index) {
    int destination = -1;
    for (int field = 0; field < 3; ++field)
      if (kwargs[index].name && std::string_view(kwargs[index].name) == names[field]) destination = field;
    if (destination < 0 || parameters[destination].tag != X3_TAG_INVALID) {
      host->raise_class_error(context, "TypeError", "invalid or duplicate create_aggregate() argument");
      return X3_STATUS_ERROR;
    }
    parameters[destination] = kwargs[index].value;
  }
  for (const X3Value value : parameters) {
    if (value.tag == X3_TAG_INVALID) {
      host->raise_class_error(context, "TypeError", "missing create_aggregate() argument");
      return X3_STATUS_ERROR;
    }
  }
  if (kwargc != 0) {
    SqlOwnedValue importer(host), module(host), warn(host), category(host), name(host), message(host), ignored(host);
    if (host->builtin_value(host, "__import__", &importer.value) != X3_STATUS_OK ||
        host->builtin_value(host, "DeprecationWarning", &category.value) != X3_STATUS_OK) return X3_STATUS_ERROR;
    name.value = host->value_string(runtime, "warnings");
    if (host->call(runtime, importer.value, &name.value, 1, &module.value) != X3_STATUS_OK ||
        host->get_attr(runtime, module.value, "warn", &warn.value) != X3_STATUS_OK) return X3_STATUS_ERROR;
    message.value = host->value_string(runtime,
        "Passing keyword arguments 'name', 'n_arg' and 'aggregate_class' to "
        "_sqlite3.Connection.create_aggregate() is deprecated. Parameters 'name', "
        "'n_arg' and 'aggregate_class' will become positional-only in Python 3.15.");
    const X3Value warning_args[] = {message.value, category.value};
    const X3KeywordArg level = {"stacklevel", x3_value_int64(2)};
    if (host->call_kw(runtime, warn.value, warning_args, 2, &level, 1, &ignored.value) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  }
  // Own the registration name before index coercion: __index__ may make a
  // nested call which reuses the SDK's thread-local rendered-string storage.
  SqlOwnedValue text(host), str_type(host), str_method(host);
  SqlAggregateFunction checker{host, runtime, x3_value_invalid()};
  bool is_name = false;
  if (!sql_instance_of(&checker, parameters[0], "str", is_name)) return X3_STATUS_ERROR;
  if (!is_name) {
    host->raise_class_error(context, "TypeError", "create_aggregate() name must be str");
    return X3_STATUS_ERROR;
  }
  if (host->value_object_kind(parameters[0]) == X3_OBJECT_KIND_STRING) {
    text.value = parameters[0]; host->value_retain(text.value);
  } else {
    if (host->builtin_value(host, "str", &str_type.value) != X3_STATUS_OK ||
        host->get_attr(runtime, str_type.value, "__str__", &str_method.value) != X3_STATUS_OK ||
        host->call(runtime, str_method.value, &parameters[0], 1, &text.value) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  }
  const char* name_data = nullptr;
  uint64_t name_size = 0;
  if (host->value_string_data(runtime, text.value, &name_data, &name_size) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (std::memchr(name_data, 0, static_cast<size_t>(name_size))) {
    host->raise_class_error(context, "ValueError", "embedded null character");
    return X3_STATUS_ERROR;
  }
  const std::string name(name_data, static_cast<size_t>(name_size));
  SqlOwnedValue importer(host), operator_module(host), index_method(host), module_name(host), narg(host);
  if (host->builtin_value(host, "__import__", &importer.value) != X3_STATUS_OK) return X3_STATUS_ERROR;
  module_name.value = host->value_string(runtime, "operator");
  if (host->call(runtime, importer.value, &module_name.value, 1, &operator_module.value) != X3_STATUS_OK ||
      host->get_attr(runtime, operator_module.value, "index", &index_method.value) != X3_STATUS_OK ||
      host->call(runtime, index_method.value, &parameters[1], 1, &narg.value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (narg.value.tag != X3_TAG_INT64 || narg.value.as.i64 < INT_MIN || narg.value.as.i64 > INT_MAX) {
    host->raise_class_error(context, "OverflowError", "Python int too large to convert to C int");
    return X3_STATUS_ERROR;
  }
  // Revalidate after warning/index user callbacks, which can close the DB.
  auto* connection = xlang3_sqlite::connection_from(host, args[0]);
  if (!connection || !connection->db || connection->closed) {
    host->raise_error(context, state->programming_error_class, "Cannot operate on a closed database.");
    return X3_STATUS_ERROR;
  }
  const int arity = static_cast<int>(narg.value.as.i64);
  const int limit = sqlite3_limit(connection->db, SQLITE_LIMIT_FUNCTION_ARG, -1);
  if (arity < -1 || arity > limit) {
    const std::string message = "'n_arg' must be between -1 and " + std::to_string(limit) +
        ", not " + std::to_string(arity);
    host->raise_error(context, state->programming_error_class, message.c_str());
    return X3_STATUS_ERROR;
  }
  auto* function = new (std::nothrow) SqlAggregateFunction{host, runtime, parameters[2]};
  if (!function) {
    host->raise_class_error(context, "MemoryError", "cannot allocate aggregate registration");
    return X3_STATUS_ERROR;
  }
  host->value_retain(function->factory);
  // SQLite owns the registration on success AND invokes xDestroy on failure.
  // Never release it again in the error branch. None is registered as a
  // non-callable factory, matching 3.14 aggregate behavior rather than deletion.
  const int status = sqlite3_create_function_v2(connection->db, name.c_str(), arity,
      SQLITE_UTF8, function, nullptr, sql_aggregate_step, sql_aggregate_final, release_sql_aggregate);
  if (status != SQLITE_OK) {
    host->raise_error(context, state->operational_error_class, "Error creating aggregate");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status connection_create_aggregate(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  return connection_create_aggregate_kw(context, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status sqlite3_register_adapter(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  if (!check_argc(state->host, context, argc, 2, "sqlite3.register_adapter()")) {
    return X3_STATUS_ERROR;
  }
  (void)runtime;
  (void)args;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status sqlite3_register_converter(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  if (!check_argc(state->host, context, argc, 2, "sqlite3.register_converter()")) {
    return X3_STATUS_ERROR;
  }
  (void)runtime;
  (void)args;
  *result = x3_value_none();
  return X3_STATUS_OK;
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

// Completed statements must release their SQLite resources even while a
// Python cursor remains live. Reset-only cursors can keep a close-v2 zombie DB
// and its registered factories alive; cached column names preserve description.
struct CursorCallLock {
  xlang3_sqlite::CursorHandle* cursor;
  explicit CursorCallLock(xlang3_sqlite::CursorHandle* value) : cursor(value) {
    cursor->locked = true;
  }
  ~CursorCallLock() { cursor->locked = false; }
};

bool check_cursor_unlocked(PackageState* state, X3CallContext* context,
    const xlang3_sqlite::CursorHandle* cursor) {
  if (cursor != nullptr && cursor->locked) {
    state->host->raise_error(context, state->programming_error_class,
        "Recursive use of cursors not allowed.");
    return false;
  }
  return true;
}

void finish_cursor_statement(xlang3_sqlite::CursorHandle* cursor) {
  sqlite3_stmt* statement = cursor->stmt;
  cursor->stmt = nullptr;
  cursor->has_row = false;
  if (statement != nullptr) sqlite3_finalize(statement);
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
  if (!check_cursor_unlocked(state_from(user_data), context, cursor)) return X3_STATUS_ERROR;
  if (cursor != nullptr) {
    CursorCallLock lock(cursor);
    finish_cursor_statement(cursor);
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
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (argc != 2 && argc != 3) {
    host->raise_class_error(context, "TypeError", "Cursor.execute() expected 1 or 2 arguments");
    return X3_STATUS_ERROR;
  }
  auto* cursor = xlang3_sqlite::cursor_from(host, args[0]);
  if (cursor == nullptr || cursor->connection == nullptr || cursor->connection->db == nullptr || cursor->connection->closed) {
    host->raise_error(context, state->programming_error_class, "sqlite cursor is closed");
    return X3_STATUS_ERROR;
  }
  if (!check_cursor_unlocked(state, context, cursor)) return X3_STATUS_ERROR;
  CursorCallLock lock(cursor);
  const char* sql = nullptr;
  if (!xlang3_sqlite::require_string(host, context, runtime, args[1], "Cursor.execute() SQL must be a string", &sql))
    return X3_STATUS_ERROR;
  // value_to_cstr uses a thread-local rendered buffer. Finalizing the previous
  // statement or binding/stepping can reenter Python and overwrite that buffer.
  const std::string owned_sql(sql);
  sql = owned_sql.c_str();
  finish_cursor_statement(cursor);
  // A finalize callback may close this connection. Do not prepare on its old DB.
  if (cursor->connection->db == nullptr || cursor->connection->closed) {
    host->raise_error(context, state->programming_error_class, "sqlite cursor is closed");
    return X3_STATUS_ERROR;
  }
  cursor->column_names.clear();
  cursor->rowcount = -1;
  cursor->write_statement = is_write_sql(sql);
  // Derive operation metadata before any SQLite callback can execute Python.
  const char* statement = sql;
  while (*statement != '\0' && std::isspace(static_cast<unsigned char>(*statement))) ++statement;
  std::string operation;
  while (*statement != '\0' && std::isalpha(static_cast<unsigned char>(*statement)) && operation.size() < 8)
    operation.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*statement++))));
  if (!begin_python_transaction(cursor->connection, sql)) {
    raise_sqlite_error(state, context, cursor->connection->db, "begin transaction failed");
    return X3_STATUS_ERROR;
  }
  if (sqlite3_prepare_v2(cursor->connection->db, sql, -1, &cursor->stmt, nullptr) != SQLITE_OK) {
    raise_sqlite_error(state, context, cursor->connection->db, "prepare failed");
    finish_cursor_statement(cursor);
    return X3_STATUS_ERROR;
  }
  if (cursor->stmt == nullptr) { *result = args[0]; return X3_STATUS_OK; }
  if (argc == 3 && !xlang3_sqlite::bind_params(host, runtime, cursor->stmt, args[2])) {
    raise_sqlite_error(state, context, sqlite3_db_handle(cursor->stmt), "bind failed");
    finish_cursor_statement(cursor);
    return X3_STATUS_ERROR;
  }
  sqlite3* db = sqlite3_db_handle(cursor->stmt);
  cursor->rowcount = cursor->write_statement ? 0 : -1;
  // CPython's native cursor primes the first row in execute. Fetch consumes
  // this row, then advances before returning, rather than leaving SQL active.
  const int rc = sqlite3_step(cursor->stmt);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    raise_sqlite_error(state, context, db, "execute failed");
    finish_cursor_statement(cursor);
    cursor->rowcount = -1;
    return X3_STATUS_ERROR;
  }
  try {
    const int count = sqlite3_column_count(cursor->stmt);
    cursor->column_names.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
      const char* name = sqlite3_column_name(cursor->stmt, index);
      if (name == nullptr) {
        host->raise_class_error(context, "MemoryError", "cannot read SQLite column name");
        finish_cursor_statement(cursor);
        cursor->column_names.clear();
        return X3_STATUS_ERROR;
      }
      cursor->column_names.emplace_back(name);
    }
  } catch (const std::bad_alloc&) {
    finish_cursor_statement(cursor);
    cursor->column_names.clear();
    host->raise_class_error(context, "MemoryError", "cannot cache SQLite column names");
    return X3_STATUS_ERROR;
  }
  cursor->has_row = rc == SQLITE_ROW;
  if (cursor->write_statement && (operation == "insert" || operation == "replace")) {
    cursor->lastrowid = sqlite3_last_insert_rowid(db);
    cursor->has_lastrowid = true;
  }
  if (rc == SQLITE_DONE) {
    if (cursor->write_statement) cursor->rowcount = sqlite3_changes64(db);
    finish_cursor_statement(cursor);
  }
  *result = args[0];
  return X3_STATUS_OK;
}

X3Status connection_execute(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (argc != 2 && argc != 3) {
    host->raise_class_error(context, "TypeError", "Connection.execute() expected 1 or 2 arguments");
    return X3_STATUS_ERROR;
  }
  X3Value cursor = x3_value_invalid();
  if (connection_cursor(context, runtime, user_data, args, 1, &cursor) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  X3Value execute_args[3] = {cursor, args[1], argc == 3 ? args[2] : x3_value_invalid()};
  const X3Status status = cursor_execute(
      context, runtime, user_data, execute_args, argc, result);
  if (status != X3_STATUS_OK) {
    host->value_release(cursor);
  }
  return status;
}

X3Status execute_script_on_connection(
    PackageState* state,
    X3CallContext* context,
    xlang3_sqlite::ConnectionHandle* connection,
    const char* script) {
  if (connection == nullptr || connection->db == nullptr || connection->closed) {
    state->host->raise_error(
        context, state->programming_error_class, "sqlite connection is closed");
    return X3_STATUS_ERROR;
  }
  char* message = nullptr;
  if (!sqlite3_get_autocommit(connection->db)) {
    const int commit_rc = sqlite3_exec(
        connection->db, "COMMIT", nullptr, nullptr, &message);
    if (commit_rc != SQLITE_OK) {
      const std::string text = message != nullptr ? message : sqlite3_errmsg(connection->db);
      if (message != nullptr) sqlite3_free(message);
      state->host->raise_error(context, state->operational_error_class, text.c_str());
      return X3_STATUS_ERROR;
    }
    if (message != nullptr) {
      sqlite3_free(message);
      message = nullptr;
    }
  }
  const int rc = sqlite3_exec(connection->db, script, nullptr, nullptr, &message);
  if (rc != SQLITE_OK) {
    const std::string text = message != nullptr ? message : sqlite3_errmsg(connection->db);
    if (message != nullptr) sqlite3_free(message);
    state->host->raise_error(context, state->operational_error_class, text.c_str());
    return X3_STATUS_ERROR;
  }
  if (message != nullptr) sqlite3_free(message);
  return X3_STATUS_OK;
}

X3Status cursor_executescript(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 2, "Cursor.executescript()")) {
    return X3_STATUS_ERROR;
  }
  auto* cursor = xlang3_sqlite::cursor_from(host, args[0]);
  if (cursor == nullptr || cursor->connection == nullptr) {
    host->raise_error(context, state->programming_error_class, "sqlite cursor is closed");
    return X3_STATUS_ERROR;
  }
  if (!check_cursor_unlocked(state, context, cursor)) return X3_STATUS_ERROR;
  CursorCallLock lock(cursor);
  const char* script = nullptr;
  if (!xlang3_sqlite::require_string(
          host, context, runtime, args[1],
          "Cursor.executescript() SQL must be a string", &script)) {
    return X3_STATUS_ERROR;
  }
  // Finishing the old statement can run Python callbacks and reuse the SDK
  // string buffer. Keep script bytes owned before that cleanup, and detach first.
  const std::string owned_script(script);
  finish_cursor_statement(cursor);
  cursor->rowcount = -1;
  if (execute_script_on_connection(state, context, cursor->connection, owned_script.c_str()) !=
      X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  *result = args[0];
  return X3_STATUS_OK;
}

X3Status connection_executescript(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 2, "Connection.executescript()")) {
    return X3_STATUS_ERROR;
  }
  X3Value cursor = x3_value_invalid();
  if (connection_cursor(context, runtime, user_data, args, 1, &cursor) !=
      X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  X3Value script_args[2] = {cursor, args[1]};
  const X3Status status = cursor_executescript(
      context, runtime, user_data, script_args, 2, result);
  if (status != X3_STATUS_OK) host->value_release(cursor);
  return status;
}

X3Status cursor_executemany(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (argc != 3) {
    host->raise_class_error(
        context, "TypeError", "Cursor.executemany() expected 2 arguments");
    return X3_STATUS_ERROR;
  }
  auto* cursor = xlang3_sqlite::cursor_from(host, args[0]);
  if (cursor == nullptr || cursor->connection == nullptr ||
      cursor->connection->db == nullptr || cursor->connection->closed) {
    host->raise_error(context, state->programming_error_class, "sqlite cursor is closed");
    return X3_STATUS_ERROR;
  }
  if (!check_cursor_unlocked(state, context, cursor)) return X3_STATUS_ERROR;
  uint64_t count = 0;
  if (host->len(runtime, args[2], &count) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  sqlite3_int64 total_changes = 0;
  cursor->has_lastrowid = false;
  for (uint64_t index = 0; index < count; ++index) {
    X3Value key = x3_value_int64(static_cast<int64_t>(index));
    X3Value parameters = x3_value_invalid();
    if (host->get_item(runtime, args[2], key, &parameters) != X3_STATUS_OK) {
      return X3_STATUS_ERROR;
    }
    X3Value execute_args[3] = {args[0], args[1], parameters};
    X3Value ignored_result = x3_value_invalid();
    const X3Status status = cursor_execute(
        context, runtime, user_data, execute_args, 3, &ignored_result);
    host->value_release(parameters);
    if (status != X3_STATUS_OK) {
      return status;
    }
    total_changes += sqlite3_changes64(cursor->connection->db);
  }
  cursor->rowcount = total_changes;
  cursor->has_lastrowid = false;
  *result = args[0];
  return X3_STATUS_OK;
}

X3Status connection_executemany(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (argc != 3) {
    host->raise_class_error(
        context, "TypeError", "Connection.executemany() expected 2 arguments");
    return X3_STATUS_ERROR;
  }
  X3Value cursor = x3_value_invalid();
  if (connection_cursor(context, runtime, user_data, args, 1, &cursor) !=
      X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  X3Value many_args[3] = {cursor, args[1], args[2]};
  const X3Status status = cursor_executemany(
      context, runtime, user_data, many_args, 3, result);
  if (status != X3_STATUS_OK) host->value_release(cursor);
  return status;
}

X3Status cursor_fetchone(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* state = state_from(user_data);
  auto* host = state->host;
  if (!check_argc(host, context, argc, 1, "Cursor.fetchone()")) return X3_STATUS_ERROR;
  auto* cursor = xlang3_sqlite::cursor_from(host, args[0]);
  if (cursor == nullptr || cursor->connection == nullptr || cursor->connection->db == nullptr || cursor->connection->closed) {
    host->raise_error(context, state->programming_error_class, "sqlite cursor is closed");
    return X3_STATUS_ERROR;
  }
  if (!check_cursor_unlocked(state, context, cursor)) return X3_STATUS_ERROR;
  if (cursor->stmt == nullptr || !cursor->has_row) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  // Same-cursor callbacks must not finalize/reuse the raw active statement.
  // Unrelated cursors and connection close remain allowed; stmt pins its DB.
  CursorCallLock lock(cursor);
  // Own the current row before stepping: SQLite's next row/reset can overwrite
  // its column buffers. A lookahead error discards this row, as CPython does.
  X3Value row = xlang3_sqlite::row_list(host, runtime, cursor->stmt);
  sqlite3* db = sqlite3_db_handle(cursor->stmt);
  const int rc = sqlite3_step(cursor->stmt);
  if (rc == SQLITE_DONE) {
    if (cursor->write_statement) cursor->rowcount = sqlite3_changes64(db);
    finish_cursor_statement(cursor);
  } else if (rc != SQLITE_ROW) {
    raise_sqlite_error(state, context, db, "fetchone failed");
    finish_cursor_statement(cursor);
    host->value_release(row);
    return X3_STATUS_ERROR;
  }
  *result = row;
  return X3_STATUS_OK;
}

X3Status cursor_iter(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Cursor.__iter__()")) {
    return X3_STATUS_ERROR;
  }
  *result = args[0];
  return X3_STATUS_OK;
}

X3Status cursor_next(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = state_from(user_data)->host;
  if (!check_argc(host, context, argc, 1, "Cursor.__next__()")) {
    return X3_STATUS_ERROR;
  }
  X3Value row = x3_value_invalid();
  const X3Status status = cursor_fetchone(
      context, runtime, user_data, args, argc, &row);
  if (status != X3_STATUS_OK) return status;
  if (row.tag == X3_TAG_NONE) {
    host->raise_class_error(context, "StopIteration", "");
    return X3_STATUS_ERROR;
  }
  *result = row;
  return X3_STATUS_OK;
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
  X3Value rows = host->value_list(runtime);
  while (true) {
    X3Value row = x3_value_invalid();
    if (cursor_fetchone(context, runtime, user_data, args, 1, &row) != X3_STATUS_OK) {
      host->value_release(rows);
      return X3_STATUS_ERROR;
    }
    if (row.tag == X3_TAG_NONE) { *result = rows; return X3_STATUS_OK; }
    const X3Status status = host->list_append(runtime, rows, row);
    host->value_release(row);
    if (status != X3_STATUS_OK) { host->value_release(rows); return X3_STATUS_ERROR; }
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

void add_function(
    const X3PackageHost* host, X3Module* module, const char* name,
    X3NativeFn callback, PackageState* state,
    X3NativeKeywordFn keyword_callback = nullptr) {
  X3NativeFunctionDef def{};
  def.size = sizeof(def);
  def.name = name;
  def.callback = callback;
  def.keyword_callback = keyword_callback;
  def.user_data = state;
  host->module_add_function(module, &def);
}

void def_method(
    X3NativeFunctionDef& def, const char* name, X3NativeFn callback,
    PackageState* state, X3NativeKeywordFn keyword_callback = nullptr) {
  def.size = sizeof(def);
  def.name = name;
  def.callback = callback;
  def.keyword_callback = keyword_callback;
  def.user_data = state;
}

void add_int(const X3PackageHost* host, X3Module* module, const char* name, int64_t value) {
  host->module_add_value(module, name, x3_value_int64(value));
}

X3Status cursor_description_getter(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = state_from(user_data);
  if (!check_argc(state->host, context, argc, 1, "Cursor.description")) {
    return X3_STATUS_ERROR;
  }
  auto* cursor = xlang3_sqlite::cursor_from(state->host, args[0]);
  if (cursor == nullptr || cursor->column_names.empty()) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  X3Value description = state->host->value_list(runtime);
  X3Value tuple_type = x3_value_invalid();
  if (state->host->builtin_value(state->host, "tuple", &tuple_type) != X3_STATUS_OK) {
    state->host->value_release(description);
    state->host->set_error(context, "tuple type is unavailable");
    return X3_STATUS_ERROR;
  }
  for (const auto& column_name : cursor->column_names) {
    X3Value field = state->host->value_list(runtime);
    X3Value name = state->host->value_string(
        runtime, column_name.c_str());
    state->host->list_append(runtime, field, name);
    state->host->value_release(name);
    for (int metadata = 0; metadata < 6; ++metadata) {
      state->host->list_append(runtime, field, x3_value_none());
    }
    X3Value field_tuple = x3_value_invalid();
    if (state->host->call(runtime, tuple_type, &field, 1, &field_tuple) != X3_STATUS_OK) {
      state->host->value_release(field);
      state->host->value_release(tuple_type);
      state->host->value_release(description);
      return X3_STATUS_ERROR;
    }
    state->host->list_append(runtime, description, field_tuple);
    state->host->value_release(field_tuple);
    state->host->value_release(field);
  }
  X3Value description_tuple = x3_value_invalid();
  if (state->host->call(runtime, tuple_type, &description, 1, &description_tuple) != X3_STATUS_OK) {
    state->host->value_release(tuple_type);
    state->host->value_release(description);
    return X3_STATUS_ERROR;
  }
  state->host->value_release(tuple_type);
  state->host->value_release(description);
  *result = description_tuple;
  return X3_STATUS_OK;
}

X3Status cursor_lastrowid_getter(
    X3CallContext* context, X3Runtime*, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = state_from(user_data);
  if (!check_argc(state->host, context, argc, 1, "Cursor.lastrowid")) {
    return X3_STATUS_ERROR;
  }
  auto* cursor = xlang3_sqlite::cursor_from(state->host, args[0]);
  if (cursor == nullptr || !cursor->has_lastrowid) {
    *result = x3_value_none();
  } else {
    *result = x3_value_int64(cursor->lastrowid);
  }
  return X3_STATUS_OK;
}

X3Status cursor_rowcount_getter(
    X3CallContext* context, X3Runtime*, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = state_from(user_data);
  if (!check_argc(state->host, context, argc, 1, "Cursor.rowcount")) {
    return X3_STATUS_ERROR;
  }
  auto* cursor = xlang3_sqlite::cursor_from(state->host, args[0]);
  *result = x3_value_int64(cursor == nullptr ? -1 : cursor->rowcount);
  return X3_STATUS_OK;
}

bool add_readonly_property(
    PackageState* state, X3Value klass, const char* name, X3NativeFn getter) {
  X3Value property = x3_value_invalid();
  if (state->host->property_create(
          state->host->runtime, name, getter, nullptr, state, &property) != X3_STATUS_OK) {
    return false;
  }
  const bool ok = state->host->class_add_value(klass, name, property) == X3_STATUS_OK;
  state->host->value_release(property);
  return ok;
}

void add_string(const X3PackageHost* host, X3Module* module, const char* name, const char* value) {
  X3Value text = host->value_string(host->runtime, value);
  host->module_add_value(module, name, text);
  host->value_release(text);
}

void add_dbapi_int_constants(const X3PackageHost* host, X3Module* module) {
  add_int(host, module, "threadsafety", 1);
  add_int(host, module, "PARSE_DECLTYPES", 1);
  add_int(host, module, "PARSE_COLNAMES", 2);
  add_int(host, module, "SQLITE_OK", SQLITE_OK);
  add_int(host, module, "SQLITE_ERROR", SQLITE_ERROR);
  add_int(host, module, "SQLITE_INTERNAL", SQLITE_INTERNAL);
  add_int(host, module, "SQLITE_PERM", SQLITE_PERM);
  add_int(host, module, "SQLITE_ABORT", SQLITE_ABORT);
  add_int(host, module, "SQLITE_BUSY", SQLITE_BUSY);
  add_int(host, module, "SQLITE_LOCKED", SQLITE_LOCKED);
  add_int(host, module, "SQLITE_NOMEM", SQLITE_NOMEM);
  add_int(host, module, "SQLITE_READONLY", SQLITE_READONLY);
  add_int(host, module, "SQLITE_INTERRUPT", SQLITE_INTERRUPT);
  add_int(host, module, "SQLITE_IOERR", SQLITE_IOERR);
  add_int(host, module, "SQLITE_CORRUPT", SQLITE_CORRUPT);
  add_int(host, module, "SQLITE_NOTFOUND", SQLITE_NOTFOUND);
  add_int(host, module, "SQLITE_FULL", SQLITE_FULL);
  add_int(host, module, "SQLITE_CANTOPEN", SQLITE_CANTOPEN);
  add_int(host, module, "SQLITE_PROTOCOL", SQLITE_PROTOCOL);
  add_int(host, module, "SQLITE_EMPTY", SQLITE_EMPTY);
  add_int(host, module, "SQLITE_SCHEMA", SQLITE_SCHEMA);
  add_int(host, module, "SQLITE_TOOBIG", SQLITE_TOOBIG);
  add_int(host, module, "SQLITE_CONSTRAINT", SQLITE_CONSTRAINT);
  add_int(host, module, "SQLITE_MISMATCH", SQLITE_MISMATCH);
  add_int(host, module, "SQLITE_MISUSE", SQLITE_MISUSE);
  add_int(host, module, "SQLITE_NOLFS", SQLITE_NOLFS);
  add_int(host, module, "SQLITE_AUTH", SQLITE_AUTH);
  add_int(host, module, "SQLITE_FORMAT", SQLITE_FORMAT);
  add_int(host, module, "SQLITE_RANGE", SQLITE_RANGE);
  add_int(host, module, "SQLITE_NOTADB", SQLITE_NOTADB);
  add_int(host, module, "SQLITE_NOTICE", SQLITE_NOTICE);
  add_int(host, module, "SQLITE_WARNING", SQLITE_WARNING);
  add_int(host, module, "SQLITE_ROW", SQLITE_ROW);
  add_int(host, module, "SQLITE_DONE", SQLITE_DONE);
}

} // namespace

class xlang_sqlite3 {
public:
  BEGIN_PACKAGE(xlang_sqlite3)
  END_PACKAGE
};

namespace {

X3Status register_sqlite_package(X::Package<xlang_sqlite3>* package) {
  auto* host = package->host();
  // Read appended callbacks only after checking the host struct's byte size.
  // Existing packages remain prefix-compatible; this package needs exception
  // transport because SQLite may finalize a callback outside a native call.
  const size_t required = offsetof(X3PackageHost, runtime_restore_exception) +
      sizeof(host->runtime_restore_exception);
  if (host->size < required || !host->runtime_take_exception || !host->runtime_restore_exception)
    return X3_STATUS_ERROR;
  auto* state = new PackageState();
  state->host = host;
  host->package_set_cleanup(host, state, cleanup_package_state);

  X3Module* sqlite3 = package->module();
  X3Module* sqlite = nullptr;
  if (sqlite3 == nullptr || host->add_module(host, "sqlite", &sqlite) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  if (host->module_add_class(sqlite3, "Error", nullptr, 0, &state->error_class) != X3_STATUS_OK ||
      host->module_add_class(sqlite3, "DatabaseError", nullptr, 0, &state->database_error_class) != X3_STATUS_OK ||
      host->module_add_class(sqlite3, "OperationalError", nullptr, 0, &state->operational_error_class) != X3_STATUS_OK ||
      host->module_add_class(sqlite3, "ProgrammingError", nullptr, 0, &state->programming_error_class) != X3_STATUS_OK ||
      host->module_add_class(sqlite3, "DataError", nullptr, 0, &state->data_error_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  if (!set_public_dbapi_module(host, state->error_class) ||
      !set_public_dbapi_module(host, state->database_error_class) ||
      !set_public_dbapi_module(host, state->operational_error_class) ||
      !set_public_dbapi_module(host, state->programming_error_class) ||
      !set_public_dbapi_module(host, state->data_error_class)) {
    return X3_STATUS_ERROR;
  }
  X3Value exception_class = x3_value_invalid();
  if (host->builtin_value(host, "Exception", &exception_class) != X3_STATUS_OK ||
      host->class_set_base(state->error_class, exception_class) != X3_STATUS_OK ||
      host->class_set_base(state->database_error_class, state->error_class) != X3_STATUS_OK ||
      host->class_set_base(state->operational_error_class, state->database_error_class) != X3_STATUS_OK ||
      host->class_set_base(state->programming_error_class, state->database_error_class) != X3_STATUS_OK ||
      host->class_set_base(state->data_error_class, state->database_error_class) != X3_STATUS_OK) {
    host->value_release(exception_class);
    return X3_STATUS_ERROR;
  }
  host->value_release(exception_class);
  host->module_add_value(sqlite, "Error", state->error_class);
  host->module_add_value(sqlite, "DatabaseError", state->database_error_class);
  host->module_add_value(sqlite, "OperationalError", state->operational_error_class);
  host->module_add_value(sqlite, "ProgrammingError", state->programming_error_class);
  host->module_add_value(sqlite, "DataError", state->data_error_class);

  X3NativeFunctionDef connection_methods[12]{};
  def_method(connection_methods[0], "__init__", connection_init, state);
  def_method(connection_methods[1], "cursor", connection_cursor, state);
  def_method(connection_methods[2], "commit", connection_commit, state);
  def_method(connection_methods[3], "rollback", connection_rollback, state);
  def_method(connection_methods[4], "close", connection_close, state);
  def_method(connection_methods[5], "__enter__", connection_enter, state);
  def_method(connection_methods[6], "__exit__", connection_exit, state);
  def_method(connection_methods[7], "execute", connection_execute, state);
  def_method(connection_methods[8], "create_function", connection_create_function, state, connection_create_function_kw);
  def_method(connection_methods[9], "executescript", connection_executescript, state);
  def_method(connection_methods[10], "executemany", connection_executemany, state);
  def_method(connection_methods[11], "create_aggregate", connection_create_aggregate, state, connection_create_aggregate_kw);
  if (host->module_add_class(sqlite3, "Connection", connection_methods, 12, &state->connection_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  if (!set_public_dbapi_module(host, state->connection_class)) return X3_STATUS_ERROR;

  X3NativeFunctionDef cursor_methods[10]{};
  def_method(cursor_methods[0], "execute", cursor_execute, state);
  def_method(cursor_methods[1], "fetchone", cursor_fetchone, state);
  def_method(cursor_methods[2], "fetchall", cursor_fetchall, state);
  def_method(cursor_methods[3], "close", cursor_close, state);
  def_method(cursor_methods[4], "__enter__", cursor_enter, state);
  def_method(cursor_methods[5], "__exit__", cursor_exit, state);
  def_method(cursor_methods[6], "executescript", cursor_executescript, state);
  def_method(cursor_methods[7], "executemany", cursor_executemany, state);
  def_method(cursor_methods[8], "__iter__", cursor_iter, state);
  def_method(cursor_methods[9], "__next__", cursor_next, state);
  if (host->module_add_class(sqlite3, "Cursor", cursor_methods, 10, &state->cursor_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  if (!set_public_dbapi_module(host, state->cursor_class)) return X3_STATUS_ERROR;
  if (!add_readonly_property(
          state, state->cursor_class, "description", cursor_description_getter)) {
    return X3_STATUS_ERROR;
  }
  if (!add_readonly_property(
          state, state->cursor_class, "lastrowid", cursor_lastrowid_getter) ||
      !add_readonly_property(
          state, state->cursor_class, "rowcount", cursor_rowcount_getter)) {
    return X3_STATUS_ERROR;
  }
  if (host->module_add_class(sqlite3, "Row", nullptr, 0, &state->row_class) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  if (!set_public_dbapi_module(host, state->row_class)) return X3_STATUS_ERROR;
  host->module_add_value(sqlite3, "Row", state->row_class);
  host->module_add_value(sqlite, "Connection", state->connection_class);
  host->module_add_value(sqlite, "Cursor", state->cursor_class);
  host->module_add_value(sqlite, "Row", state->row_class);
  add_dbapi_int_constants(host, sqlite3);
  add_dbapi_int_constants(host, sqlite);
  add_string(host, sqlite3, "sqlite_version", sqlite3_libversion());
  add_function(host, sqlite3, "connect", sqlite3_connect, state, sqlite3_connect_kw);
  add_function(host, sqlite3, "register_adapter", sqlite3_register_adapter, state);
  add_function(host, sqlite3, "register_converter", sqlite3_register_converter, state);

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
  add_function(host, sqlite, "connect", sqlite3_connect, state, sqlite3_connect_kw);

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
  add_function(host, sqlite, "register_adapter", sqlite3_register_adapter, state);
  add_function(host, sqlite, "register_converter", sqlite3_register_converter, state);

  host->module_add_value(sqlite, "OK", x3_value_int64(SQLITE_OK));
  host->module_add_value(sqlite, "ROW", x3_value_int64(SQLITE_ROW));
  host->module_add_value(sqlite, "DONE", x3_value_int64(SQLITE_DONE));
  host->module_add_value(sqlite3, "OK", x3_value_int64(SQLITE_OK));
  host->module_add_value(sqlite3, "ROW", x3_value_int64(SQLITE_ROW));
  host->module_add_value(sqlite3, "DONE", x3_value_int64(SQLITE_DONE));
  return X3_STATUS_OK;
}

} // namespace

X3Status register_sqlite_module(X3PackageHost* host, X3Value curModule) {
  auto* object = new xlang_sqlite3();
  // This is the native DB-API boundary consumed by Python 3.14's pure-Python
  // sqlite3 package.  Registering it as sqlite3 would replace that package
  // while sqlite3.dbapi2 imports `_sqlite3`.
  auto* package = new X::Package<xlang_sqlite3>(host, "_sqlite3", object);
  object->__xlang3_host_ = host;
  object->__xlang3_package_ = package;
  package->SetCurrentModule(X::Value(host, curModule, true));
  X3Status status = register_sqlite_package(package);
  if (status != X3_STATUS_OK || !package->RegisterCleanup()) {
    delete package;
    return X3_STATUS_ERROR;
  }
  return X3_STATUS_OK;
}
