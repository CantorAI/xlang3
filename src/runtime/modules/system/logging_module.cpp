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
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

constexpr int64_t kDebug = 10;
constexpr int64_t kInfo = 20;
constexpr int64_t kWarning = 30;
constexpr int64_t kError = 40;
constexpr int64_t kCritical = 50;

struct LoggingState {
  int64_t root_level = kWarning;
  Value logger_class;
};

void logging_state_cleanup(void* data) {
  delete static_cast<LoggingState*>(data);
}

bool string_from_value(const Value& value, std::string& out) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  out = value_to_string(value);
  return true;
}

std::string level_name(int64_t level) {
  if (level >= kCritical) {
    return "CRITICAL";
  }
  if (level >= kError) {
    return "ERROR";
  }
  if (level >= kWarning) {
    return "WARNING";
  }
  if (level >= kInfo) {
    return "INFO";
  }
  return "DEBUG";
}

bool level_from_name(const std::string& name, int64_t& level) {
  if (name == "CRITICAL" || name == "FATAL") {
    level = kCritical;
    return true;
  }
  if (name == "ERROR") {
    level = kError;
    return true;
  }
  if (name == "WARNING" || name == "WARN") {
    level = kWarning;
    return true;
  }
  if (name == "INFO") {
    level = kInfo;
    return true;
  }
  if (name == "DEBUG") {
    level = kDebug;
    return true;
  }
  return false;
}

bool get_logger_name(const Value& logger, std::string& name) {
  std::string ignored;
  Value value;
  if (object_get_attr(logger, "name", value, ignored)) {
    return string_from_value(value, name);
  }
  name = "root";
  return true;
}

bool get_logger_level(const Value& logger, int64_t fallback, int64_t& level) {
  std::string ignored;
  Value value;
  if (object_get_attr(logger, "level", value, ignored) && value.tag == ValueTag::Int64) {
    level = value.as.i64;
    return true;
  }
  level = fallback;
  return true;
}

std::string format_message(const Value* args, uint32_t start, uint32_t argc) {
  if (start >= argc) {
    return "";
  }
  std::string message = value_to_string(args[start]);
  for (uint32_t i = start + 1; i < argc; ++i) {
    message += " ";
    message += value_to_string(args[i]);
  }
  return message;
}

bool emit_log(Runtime& runtime, LoggingState& state, const Value* args, uint32_t argc, uint32_t message_start,
    int64_t level, const std::string& logger_name, Value& out, std::string& error) {
  int64_t effective_level = state.root_level;
  if (message_start == 1 && argc > 0) {
    get_logger_level(args[0], state.root_level, effective_level);
  }
  if (level >= effective_level) {
    const std::string line = level_name(level) + ":" + logger_name + ":" + format_message(args, message_start, argc) + "\n";
    runtime.write_output(line.c_str(), line.size());
  }
  value_set_none(out);
  return true;
}

bool logging_basic_config(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<LoggingState*>(user_data);
  if (state == nullptr) {
    error = "logging state is unavailable";
    return false;
  }
  if (argc > 1) {
    error = "logging.basicConfig() expected optional level";
    return false;
  }
  if (argc == 1) {
    if (args[0].tag != ValueTag::Int64) {
      error = "logging.basicConfig() level must be int";
      return false;
    }
    state->root_level = args[0].as.i64;
  }
  value_set_none(out);
  return true;
}

bool logging_get_logger(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<LoggingState*>(user_data);
  if (state == nullptr) {
    error = "logging state is unavailable";
    return false;
  }
  if (argc > 1) {
    error = "logging.getLogger() expected optional name";
    return false;
  }
  std::string name = "root";
  if (argc == 1) {
    string_from_value(args[0], name);
  }
  out = Value::instance(state->logger_class);
  object_set_attr(out, "name", Value::string(name), error);
  object_set_attr(out, "level", Value::int64(state->root_level), error);
  object_set_attr(out, "__xlang3_string_value__", Value::string("<Logger " + name + ">"), error);
  return true;
}

bool logging_set_level(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "Logger.setLevel() expected level";
    return false;
  }
  Value self = args[0];
  if (!object_set_attr(self, "level", args[1], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool logging_get_effective_level(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Logger.getEffectiveLevel() expected no arguments";
    return false;
  }
  int64_t level = kWarning;
  get_logger_level(args[0], kWarning, level);
  value_set_int64(out, level);
  return true;
}

bool logging_is_enabled_for(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "Logger.isEnabledFor() expected level";
    return false;
  }
  int64_t level = kWarning;
  get_logger_level(args[0], kWarning, level);
  value_set_bool(out, args[1].as.i64 >= level);
  return true;
}

bool logging_noop_method(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool logging_get_level_name(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "logging.getLevelName() expected level";
    return false;
  }
  if (args[0].tag == ValueTag::Int64) {
    out = Value::string(level_name(args[0].as.i64));
    return true;
  }
  std::string name;
  string_from_value(args[0], name);
  int64_t level = 0;
  if (level_from_name(name, level)) {
    value_set_int64(out, level);
  } else {
    out = Value::string("Level " + name);
  }
  return true;
}

bool logging_log_root(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data,
    int64_t level) {
  auto* state = static_cast<LoggingState*>(user_data);
  if (state == nullptr) {
    error = "logging state is unavailable";
    return false;
  }
  if (argc < 1) {
    error = "logging call expected message";
    return false;
  }
  return emit_log(runtime, *state, args, argc, 0, level, "root", out, error);
}

bool logging_log_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error,
    void* user_data, int64_t level) {
  auto* state = static_cast<LoggingState*>(user_data);
  if (state == nullptr) {
    error = "logging state is unavailable";
    return false;
  }
  if (argc < 2) {
    error = "Logger log method expected message";
    return false;
  }
  std::string name;
  get_logger_name(args[0], name);
  return emit_log(runtime, *state, args, argc, 1, level, name, out, error);
}

bool logging_debug(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_root(runtime, args, argc, out, error, user_data, kDebug);
}

bool logging_info(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_root(runtime, args, argc, out, error, user_data, kInfo);
}

bool logging_warning(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_root(runtime, args, argc, out, error, user_data, kWarning);
}

bool logging_error(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_root(runtime, args, argc, out, error, user_data, kError);
}

bool logging_critical(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_root(runtime, args, argc, out, error, user_data, kCritical);
}

bool logger_debug(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_method(runtime, args, argc, out, error, user_data, kDebug);
}

bool logger_info(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_method(runtime, args, argc, out, error, user_data, kInfo);
}

bool logger_warning(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_method(runtime, args, argc, out, error, user_data, kWarning);
}

bool logger_error(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_method(runtime, args, argc, out, error, user_data, kError);
}

bool logger_critical(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return logging_log_method(runtime, args, argc, out, error, user_data, kCritical);
}

Value make_logger_class(Runtime& runtime, LoggingState* state) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"setLevel", runtime.make_native_function("logging.Logger.setLevel", logging_set_level)});
  attrs.push_back({"getEffectiveLevel", runtime.make_native_function("logging.Logger.getEffectiveLevel", logging_get_effective_level)});
  attrs.push_back({"isEnabledFor", runtime.make_native_function("logging.Logger.isEnabledFor", logging_is_enabled_for)});
  attrs.push_back({"addHandler", runtime.make_native_function("logging.Logger.addHandler", logging_noop_method)});
  attrs.push_back({"removeHandler", runtime.make_native_function("logging.Logger.removeHandler", logging_noop_method)});
  attrs.push_back({"debug", runtime.make_native_function("logging.Logger.debug", logger_debug, state)});
  attrs.push_back({"info", runtime.make_native_function("logging.Logger.info", logger_info, state)});
  attrs.push_back({"warning", runtime.make_native_function("logging.Logger.warning", logger_warning, state)});
  attrs.push_back({"error", runtime.make_native_function("logging.Logger.error", logger_error, state)});
  attrs.push_back({"critical", runtime.make_native_function("logging.Logger.critical", logger_critical, state)});
  attrs.push_back({"exception", runtime.make_native_function("logging.Logger.exception", logger_error, state)});
  return Value::class_object("Logger", std::move(attrs));
}

Value make_noop_class(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function(std::string("logging.") + name + ".__init__", logging_noop_method)});
  attrs.push_back({"setLevel", runtime.make_native_function(std::string("logging.") + name + ".setLevel", logging_noop_method)});
  attrs.push_back({"setFormatter", runtime.make_native_function(std::string("logging.") + name + ".setFormatter", logging_noop_method)});
  attrs.push_back({"emit", runtime.make_native_function(std::string("logging.") + name + ".emit", logging_noop_method)});
  attrs.push_back({"close", runtime.make_native_function(std::string("logging.") + name + ".close", logging_noop_method)});
  attrs.push_back({"format", runtime.make_native_function(std::string("logging.") + name + ".format", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1) {
      error = "format() expected self";
      return false;
    }
    out = argc >= 2 ? Value::string(value_to_string(args[1])) : Value::string("");
    return true;
  })});
  return Value::class_object(name, std::move(attrs));
}

} // namespace

void register_logging_module(Runtime& runtime) {
  auto* state = new LoggingState();
  runtime.register_native_package_cleanup(state, logging_state_cleanup);
  state->logger_class = make_logger_class(runtime, state);

  NativeModuleBuilder builder(runtime, "logging");
  builder.value("DEBUG", Value::int64(kDebug))
      .value("INFO", Value::int64(kInfo))
      .value("WARNING", Value::int64(kWarning))
      .value("WARN", Value::int64(kWarning))
      .value("ERROR", Value::int64(kError))
      .value("CRITICAL", Value::int64(kCritical))
      .value("Logger", state->logger_class)
      .value("Handler", make_noop_class(runtime, "Handler"))
      .value("StreamHandler", make_noop_class(runtime, "StreamHandler"))
      .value("NullHandler", make_noop_class(runtime, "NullHandler"))
      .value("Formatter", make_noop_class(runtime, "Formatter"))
      .value("root", Value::string("root"))
      .value("lastResort", Value::none())
      .value("raiseExceptions", Value::boolean(true))
      .value("basicConfig", runtime.make_native_function("logging.basicConfig", logging_basic_config, state))
      .value("getLogger", runtime.make_native_function("logging.getLogger", logging_get_logger, state))
      .value("getLevelName", runtime.make_native_function("logging.getLevelName", logging_get_level_name))
      .value("debug", runtime.make_native_function("logging.debug", logging_debug, state))
      .value("info", runtime.make_native_function("logging.info", logging_info, state))
      .value("warning", runtime.make_native_function("logging.warning", logging_warning, state))
      .value("warn", runtime.make_native_function("logging.warn", logging_warning, state))
      .value("error", runtime.make_native_function("logging.error", logging_error, state))
      .value("critical", runtime.make_native_function("logging.critical", logging_critical, state))
      .value("fatal", runtime.make_native_function("logging.fatal", logging_critical, state))
      .value("exception", runtime.make_native_function("logging.exception", logging_error, state));
  runtime.register_module("logging", builder.finish());
}

} // namespace xlang3
