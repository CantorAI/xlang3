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

#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/value_hash.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <cstdlib>
#endif

namespace xlang3 {

namespace {

struct TimeModuleState {
  Value struct_time_class;
};

void time_module_state_cleanup(void* data) {
  delete static_cast<TimeModuleState*>(data);
}

bool no_args(uint32_t argc, const char* name, std::string& error) {
  if (argc == 0) {
    return true;
  }
  error = std::string(name) + "() expected no arguments";
  return false;
}

int64_t duration_to_ns(std::chrono::nanoseconds value) {
  return static_cast<int64_t>(value.count());
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  auto* string = value_as_string(value);
  if (string == nullptr) {
    error = std::string(name) + " must be str";
    return false;
  }
  out = string_object_to_string(*string);
  return true;
}

Value make_clock_info(Runtime& runtime, bool adjustable, bool monotonic, double resolution, const std::string& implementation) {
  Value info = Value::instance(Value::class_object("SimpleNamespace", {}));
  std::string ignored;
  object_set_attr(info, "adjustable", Value::boolean(adjustable), ignored);
  object_set_attr(info, "monotonic", Value::boolean(monotonic), ignored);
  object_set_attr(info, "resolution", Value::number(resolution), ignored);
  object_set_attr(info, "implementation", Value::string(implementation), ignored);
  return info;
}

std::tm tm_from_time_t(std::time_t timestamp, bool utc) {
  std::tm out{};
#if defined(_WIN32)
  if (utc) {
    gmtime_s(&out, &timestamp);
  } else {
    localtime_s(&out, &timestamp);
  }
#else
  if (utc) {
    gmtime_r(&timestamp, &out);
  } else {
    localtime_r(&timestamp, &out);
  }
#endif
  return out;
}

std::vector<Value> tm_tuple_items(const std::tm& tm) {
  return {
      Value::int64(static_cast<int64_t>(tm.tm_year + 1900)),
      Value::int64(static_cast<int64_t>(tm.tm_mon + 1)),
      Value::int64(static_cast<int64_t>(tm.tm_mday)),
      Value::int64(static_cast<int64_t>(tm.tm_hour)),
      Value::int64(static_cast<int64_t>(tm.tm_min)),
      Value::int64(static_cast<int64_t>(tm.tm_sec)),
      Value::int64(static_cast<int64_t>((tm.tm_wday + 6) % 7)),
      Value::int64(static_cast<int64_t>(tm.tm_yday + 1)),
      Value::int64(static_cast<int64_t>(tm.tm_isdst)),
  };
}

std::string format_tm(const std::string& format, const std::tm& tm) {
  std::ostringstream stream;
  stream << std::put_time(&tm, format.c_str());
  return stream.str();
}

void set_struct_time_metadata(Value& instance, std::vector<Value> tuple_items, const Value& zone, const Value& gmtoff) {
  std::string ignored;
  object_set_attr(instance, "n_sequence_fields", Value::int64(9), ignored);
  object_set_attr(instance, "n_fields", Value::int64(11), ignored);
  object_set_attr(instance, "n_unnamed_fields", Value::int64(0), ignored);
  object_set_attr(instance, "tm_zone", zone, ignored);
  object_set_attr(instance, "tm_gmtoff", gmtoff, ignored);
  object_set_attr(instance, "_tuple", Value::tuple(std::move(tuple_items)), ignored);
}

int64_t local_offset_seconds(std::time_t timestamp) {
  std::tm local = tm_from_time_t(timestamp, false);
  std::tm utc = tm_from_time_t(timestamp, true);
  local.tm_isdst = -1;
  utc.tm_isdst = -1;
  const std::time_t local_stamp = std::mktime(&local);
  const std::time_t utc_as_local_stamp = std::mktime(&utc);
  if (local_stamp == static_cast<std::time_t>(-1) || utc_as_local_stamp == static_cast<std::time_t>(-1)) {
    return 0;
  }
  return -static_cast<int64_t>(std::difftime(utc_as_local_stamp, local_stamp));
}

Value make_struct_time(const Value& klass, const std::tm& tm, const Value& zone = Value::none(), const Value& gmtoff = Value::none()) {
  static const char* names[] = {
      "tm_year",
      "tm_mon",
      "tm_mday",
      "tm_hour",
      "tm_min",
      "tm_sec",
      "tm_wday",
      "tm_yday",
      "tm_isdst",
  };
  auto items = tm_tuple_items(tm);
  Value instance = Value::instance(klass);
  std::string ignored;
  for (size_t i = 0; i < items.size(); ++i) {
    object_set_attr(instance, names[i], items[i], ignored);
  }
  set_struct_time_metadata(instance, std::move(items), zone, gmtoff);
  return instance;
}

Value make_member_descriptor(const std::string& name) {
  Value descriptor = Value::instance(Value::class_object("member_descriptor", {}));
  std::string ignored;
  object_set_attr(descriptor, "__name__", Value::string(name), ignored);
  return descriptor;
}

Value make_struct_time_from_timestamp(const Value& klass, std::time_t timestamp, bool utc) {
  const std::tm tm = tm_from_time_t(timestamp, utc);
  if (utc) {
    return make_struct_time(klass, tm, Value::string("UTC"), Value::int64(0));
  }
  return make_struct_time(
      klass,
      tm,
      Value::string(format_tm("%Z", tm)),
      Value::int64(local_offset_seconds(timestamp)));
}

bool numeric_time_arg(const Value& value, std::time_t& out, std::string& error) {
  if (value.tag == ValueTag::Int64) {
    out = static_cast<std::time_t>(value.as.i64);
    return true;
  }
  if (value.tag == ValueTag::Double) {
    out = static_cast<std::time_t>(value.as.f64);
    return true;
  }
  error = "timestamp must be int or float";
  return false;
}

bool optional_timestamp(const Value* args, uint32_t argc, const char* name, std::time_t& out, std::string& error) {
  if (argc > 1) {
    error = std::string(name) + "() expected at most one argument";
    return false;
  }
  if (argc == 0 || args[0].tag == ValueTag::None) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    out = static_cast<std::time_t>(std::chrono::duration<double>(now).count());
    return true;
  }
  return numeric_time_arg(args[0], out, error);
}

bool int_from_value(const Value& value, const char* name, int& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = std::string(name) + " must be int";
    return false;
  }
  out = static_cast<int>(value.as.i64);
  return true;
}

bool struct_time_tuple_storage(const Value& self, const char* method, TupleObject*& out, std::string& error) {
  Value tuple_value;
  std::string ignored;
  if (!object_get_attr(self, "_tuple", tuple_value, ignored) || (out = value_as_tuple(tuple_value)) == nullptr) {
    error = std::string(method) + " target has no tuple storage";
    return false;
  }
  return true;
}

bool normalize_struct_time_bound(const Value& value, size_t size, size_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "time.struct_time.index bounds must be int";
    return false;
  }
  int64_t index = value.as.i64;
  if (index < 0) {
    index += static_cast<int64_t>(size);
  }
  if (index < 0) {
    index = 0;
  }
  if (index > static_cast<int64_t>(size)) {
    index = static_cast<int64_t>(size);
  }
  out = static_cast<size_t>(index);
  return true;
}

bool time_struct_time_count(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "time.struct_time.count expected value";
    return false;
  }
  TupleObject* tuple = nullptr;
  if (!struct_time_tuple_storage(args[0], "time.struct_time.count", tuple, error)) {
    return false;
  }
  int64_t count = 0;
  for (const auto& item : tuple->items) {
    if (value_key_equal(item, args[1])) {
      ++count;
    }
  }
  out = Value::int64(count);
  return true;
}

bool time_struct_time_index(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "time.struct_time.index expected value and optional bounds";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  TupleObject* tuple = nullptr;
  if (!struct_time_tuple_storage(args[0], "time.struct_time.index", tuple, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  size_t start = 0;
  size_t stop = tuple->items.size();
  if (argc >= 3 && !normalize_struct_time_bound(args[2], tuple->items.size(), start, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc >= 4 && !normalize_struct_time_bound(args[3], tuple->items.size(), stop, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (start > stop) {
    start = stop;
  }
  for (size_t i = start; i < stop; ++i) {
    if (value_key_equal(tuple->items[i], args[1])) {
      out = Value::int64(static_cast<int64_t>(i));
      return true;
    }
  }
  error = "tuple.index(x): x not in tuple";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool tm_from_sequence_like(const Value& value, std::tm& out, std::string& error) {
  std::vector<Value> items;
  if (auto* tuple = value_as_tuple(value)) {
    items = tuple->items;
  } else if (auto* list = value_as_list(value)) {
    items = list->items;
  } else if (value_as_instance(value) != nullptr) {
    static const char* names[] = {
        "tm_year",
        "tm_mon",
        "tm_mday",
        "tm_hour",
        "tm_min",
        "tm_sec",
        "tm_wday",
        "tm_yday",
        "tm_isdst",
    };
    items.reserve(9);
    for (const char* name : names) {
      Value attr;
      std::string attr_error;
      if (!object_get_attr(value, name, attr, attr_error)) {
        error = "time tuple must have 9 elements";
        return false;
      }
      items.push_back(std::move(attr));
    }
  } else {
    error = "time tuple must be tuple, list, or struct_time";
    return false;
  }
  if (items.size() < 9) {
    error = "time tuple must have 9 elements";
    return false;
  }
  if (items.size() > 11) {
    error = "time.struct_time() takes an at most 11-sequence";
    return false;
  }
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int weekday = 0;
  int yearday = 0;
  int isdst = 0;
  if (!int_from_value(items[0], "tm_year", year, error) ||
      !int_from_value(items[1], "tm_mon", month, error) ||
      !int_from_value(items[2], "tm_mday", day, error) ||
      !int_from_value(items[3], "tm_hour", hour, error) ||
      !int_from_value(items[4], "tm_min", minute, error) ||
      !int_from_value(items[5], "tm_sec", second, error) ||
      !int_from_value(items[6], "tm_wday", weekday, error) ||
      !int_from_value(items[7], "tm_yday", yearday, error) ||
      !int_from_value(items[8], "tm_isdst", isdst, error)) {
    return false;
  }
  out = std::tm{};
  out.tm_year = year - 1900;
  out.tm_mon = month - 1;
  out.tm_mday = day;
  out.tm_hour = hour;
  out.tm_min = minute;
  out.tm_sec = second;
  out.tm_wday = (weekday + 1) % 7;
  out.tm_yday = yearday - 1;
  out.tm_isdst = isdst;
  return true;
}

bool struct_time_constructor_items(
    const Value& value,
    std::vector<Value>& items,
    Value& zone,
    Value& gmtoff,
    bool& sequence_has_zone,
    bool& sequence_has_gmtoff,
    std::string& error) {
  std::vector<Value> raw_items;
  if (auto* tuple = value_as_tuple(value)) {
    raw_items = tuple->items;
  } else if (auto* list = value_as_list(value)) {
    raw_items = list->items;
  } else if (value_as_instance(value) != nullptr) {
    static const char* names[] = {
        "tm_year",
        "tm_mon",
        "tm_mday",
        "tm_hour",
        "tm_min",
        "tm_sec",
        "tm_wday",
        "tm_yday",
        "tm_isdst",
    };
    raw_items.reserve(11);
    for (const char* name : names) {
      Value attr;
      std::string attr_error;
      if (!object_get_attr(value, name, attr, attr_error)) {
        error = "time tuple must have 9 elements";
        return false;
      }
      raw_items.push_back(std::move(attr));
    }
    std::string ignored;
    object_get_attr(value, "tm_zone", zone, ignored);
    object_get_attr(value, "tm_gmtoff", gmtoff, ignored);
    if (zone.tag == ValueTag::Invalid) {
      value_set_none(zone);
    } else {
      sequence_has_zone = true;
    }
    if (gmtoff.tag == ValueTag::Invalid) {
      value_set_none(gmtoff);
    } else {
      sequence_has_gmtoff = true;
    }
  } else {
    error = "time tuple must be tuple, list, or struct_time";
    return false;
  }
  if (raw_items.size() < 9) {
    error = "time tuple must have 9 elements";
    return false;
  }
  if (raw_items.size() > 11) {
    error = "time.struct_time() takes an at most 11-sequence";
    return false;
  }
  items.assign(raw_items.begin(), raw_items.begin() + 9);
  if (raw_items.size() >= 10) {
    value_assign_fast(zone, raw_items[9]);
    sequence_has_zone = true;
  }
  if (raw_items.size() >= 11) {
    value_assign_fast(gmtoff, raw_items[10]);
    sequence_has_gmtoff = true;
  }
  return true;
}

bool struct_time_extra_fields_from_dict(
    const Value& dict_value,
    bool sequence_has_zone,
    bool sequence_has_gmtoff,
    Value& zone,
    Value& gmtoff,
    std::string& error) {
  auto* dict = value_as_dict(dict_value);
  if (dict == nullptr) {
    error = "time.struct_time() optional second argument must be a dict";
    return false;
  }
  for (const auto& entry : dict->entries) {
    auto* key = value_as_string(entry.first);
    if (key == nullptr) {
      error = "time.struct_time() got duplicate or unexpected field name(s)";
      return false;
    }
    const std::string name = string_object_to_string(*key);
    if (name == "tm_zone" && !sequence_has_zone) {
      value_assign_fast(zone, entry.second);
    } else if (name == "tm_gmtoff" && !sequence_has_gmtoff) {
      value_assign_fast(gmtoff, entry.second);
    } else {
      error = "time.struct_time() got duplicate or unexpected field name(s)";
      return false;
    }
  }
  return true;
}

void set_struct_time_attrs(Value& instance, const std::vector<Value>& items, const Value& zone, const Value& gmtoff) {
  static const char* names[] = {
      "tm_year",
      "tm_mon",
      "tm_mday",
      "tm_hour",
      "tm_min",
      "tm_sec",
      "tm_wday",
      "tm_yday",
      "tm_isdst",
  };
  std::string ignored;
  for (size_t i = 0; i < 9 && i < items.size(); ++i) {
    object_set_attr(instance, names[i], items[i], ignored);
  }
  set_struct_time_metadata(instance, items, zone, gmtoff);
}

bool time_struct_time_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "time.struct_time() expected one sequence and optional dict";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value zone = Value::none();
  Value gmtoff = Value::none();
  bool sequence_has_zone = false;
  bool sequence_has_gmtoff = false;
  std::vector<Value> items;
  if (!struct_time_constructor_items(args[1], items, zone, gmtoff, sequence_has_zone, sequence_has_gmtoff, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 3 && !struct_time_extra_fields_from_dict(args[2], sequence_has_zone, sequence_has_gmtoff, zone, gmtoff, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  set_struct_time_attrs(self, items, zone, gmtoff);
  value_set_none(out);
  return true;
}

bool time_time(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.time", error)) {
    return false;
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  out = Value::number(std::chrono::duration<double>(now).count());
  return true;
}

bool time_time_ns(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.time_ns", error)) {
    return false;
  }
  value_set_int64(out, duration_to_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())));
  return true;
}

bool time_monotonic(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.monotonic", error)) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  out = Value::number(std::chrono::duration<double>(now).count());
  return true;
}

bool time_monotonic_ns(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.monotonic_ns", error)) {
    return false;
  }
  value_set_int64(out, duration_to_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())));
  return true;
}

bool time_sleep(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "time.sleep() expected one argument";
    return false;
  }
  double seconds = 0.0;
  if (args[0].tag == ValueTag::Int64) {
    seconds = static_cast<double>(args[0].as.i64);
  } else if (args[0].tag == ValueTag::Double) {
    seconds = args[0].as.f64;
  } else {
    error = "time.sleep() argument must be int or float";
    return false;
  }
  if (seconds < 0.0) {
    error = "sleep length must be non-negative";
    return false;
  }
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  value_set_none(out);
  return true;
}

bool time_process_time(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.process_time", error)) {
    return false;
  }
  out = Value::number(static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC));
  return true;
}

bool time_process_time_ns(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.process_time_ns", error)) {
    return false;
  }
  const auto ticks = static_cast<int64_t>(std::clock());
  value_set_int64(out, static_cast<int64_t>((static_cast<long double>(ticks) * 1000000000.0L) / CLOCKS_PER_SEC));
  return true;
}

bool time_thread_time(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return time_process_time(runtime, args, argc, out, error, user_data);
}

bool time_thread_time_ns(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return time_process_time_ns(runtime, args, argc, out, error, user_data);
}

bool time_get_clock_info(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "time.get_clock_info() expected clock name";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "time.get_clock_info name", name, error)) {
    return false;
  }
  if (name == "time") {
    out = make_clock_info(runtime, true, false, 1e-9, "std::chrono::system_clock");
    return true;
  }
  if (name == "monotonic" || name == "perf_counter") {
    out = make_clock_info(runtime, false, true, 1e-9, "std::chrono::steady_clock");
    return true;
  }
  if (name == "process_time" || name == "thread_time") {
    out = make_clock_info(runtime, false, true, 1.0 / static_cast<double>(CLOCKS_PER_SEC), "std::clock");
    return true;
  }
  error = "unknown clock";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool time_localtime(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  std::time_t timestamp = 0;
  if (!optional_timestamp(args, argc, "time.localtime", timestamp, error)) {
    return false;
  }
  auto* state = static_cast<TimeModuleState*>(user_data);
  out = make_struct_time_from_timestamp(state->struct_time_class, timestamp, false);
  return true;
}

bool time_gmtime(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  std::time_t timestamp = 0;
  if (!optional_timestamp(args, argc, "time.gmtime", timestamp, error)) {
    return false;
  }
  auto* state = static_cast<TimeModuleState*>(user_data);
  out = make_struct_time_from_timestamp(state->struct_time_class, timestamp, true);
  return true;
}

bool time_mktime(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "time.mktime() expected one time tuple";
    return false;
  }
  std::tm tm{};
  if (!tm_from_sequence_like(args[0], tm, error)) {
    return false;
  }
  const std::time_t timestamp = std::mktime(&tm);
  if (timestamp == static_cast<std::time_t>(-1)) {
    error = "mktime argument out of range";
    return false;
  }
  out = Value::number(static_cast<double>(timestamp));
  return true;
}

bool time_strftime(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "time.strftime() expected format and optional time tuple";
    return false;
  }
  std::string format;
  if (!get_string_arg(args[0], "time.strftime format", format, error)) {
    return false;
  }
  std::tm tm{};
  if (argc == 2) {
    if (!tm_from_sequence_like(args[1], tm, error)) {
      return false;
    }
  } else {
    const auto timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm = tm_from_time_t(timestamp, false);
  }
  out = Value::string(format_tm(format, tm));
  return true;
}

bool time_strptime(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "time.strptime() expected string and optional format";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "time.strptime string", text, error)) {
    return false;
  }
  std::string format = "%a %b %d %H:%M:%S %Y";
  if (argc == 2 && !get_string_arg(args[1], "time.strptime format", format, error)) {
    return false;
  }
  std::tm tm{};
  tm.tm_isdst = -1;
  std::istringstream stream(text);
  stream >> std::get_time(&tm, format.c_str());
  if (stream.fail()) {
    error = "time data does not match format";
    return false;
  }
  std::mktime(&tm);
  auto* state = static_cast<TimeModuleState*>(user_data);
  out = make_struct_time(state->struct_time_class, tm);
  return true;
}

bool time_asctime(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "time.asctime() expected optional time tuple";
    return false;
  }
  std::tm tm{};
  if (argc == 1) {
    if (!tm_from_sequence_like(args[0], tm, error)) {
      return false;
    }
  } else {
    const auto timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm = tm_from_time_t(timestamp, false);
  }
  out = Value::string(format_tm("%a %b %d %H:%M:%S %Y", tm));
  return true;
}

bool time_ctime(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  std::time_t timestamp = 0;
  if (!optional_timestamp(args, argc, "time.ctime", timestamp, error)) {
    return false;
  }
  const std::tm tm = tm_from_time_t(timestamp, false);
  out = Value::string(format_tm("%a %b %d %H:%M:%S %Y", tm));
  return true;
}

std::tm local_tm_for_epoch(std::time_t timestamp) {
  return tm_from_time_t(timestamp, false);
}

struct TimezoneInfo {
  int64_t timezone = 0;
  int64_t altzone = 0;
  int64_t daylight = 0;
  std::string standard_name = "UTC";
  std::string daylight_name = "UTC";
};

TimezoneInfo platform_timezone_info() {
  TimezoneInfo info;
#if defined(_WIN32)
  _tzset();
  long timezone_seconds = 0;
  int daylight = 0;
  long dst_bias = 0;
  if (_get_timezone(&timezone_seconds) == 0) {
    info.timezone = static_cast<int64_t>(timezone_seconds);
  }
  if (_get_daylight(&daylight) == 0) {
    info.daylight = daylight != 0 ? 1 : 0;
  }
  info.altzone = info.timezone;
  if (info.daylight && _get_dstbias(&dst_bias) == 0) {
    info.altzone = info.timezone + static_cast<int64_t>(dst_bias);
  }
  for (int index = 0; index < 2; ++index) {
    size_t required = 0;
    if (_get_tzname(&required, nullptr, 0, index) != 0 || required == 0) {
      continue;
    }
    std::string name(required, '\0');
    if (_get_tzname(&required, name.data(), required, index) == 0) {
      if (!name.empty() && name.back() == '\0') {
        name.pop_back();
      }
      if (index == 0) {
        info.standard_name = name;
      } else {
        info.daylight_name = name;
      }
    }
  }
#else
  tzset();
  auto offset_seconds = [](std::time_t timestamp) -> int64_t {
    std::tm local = tm_from_time_t(timestamp, false);
    std::tm utc = tm_from_time_t(timestamp, true);
    local.tm_isdst = -1;
    utc.tm_isdst = -1;
    const std::time_t local_stamp = std::mktime(&local);
    const std::time_t utc_as_local_stamp = std::mktime(&utc);
    if (local_stamp == static_cast<std::time_t>(-1) || utc_as_local_stamp == static_cast<std::time_t>(-1)) {
      return 0;
    }
    return static_cast<int64_t>(std::difftime(utc_as_local_stamp, local_stamp));
  };
  const std::time_t now = std::time(nullptr);
  const std::tm now_local = tm_from_time_t(now, false);
  std::tm january{};
  january.tm_year = now_local.tm_year;
  january.tm_mon = 0;
  january.tm_mday = 1;
  january.tm_hour = 12;
  january.tm_isdst = -1;
  std::tm july = january;
  july.tm_mon = 6;
  const std::time_t january_stamp = std::mktime(&january);
  const std::time_t july_stamp = std::mktime(&july);
  const int64_t january_offset = offset_seconds(january_stamp);
  const int64_t july_offset = offset_seconds(july_stamp);
  const std::string january_name = format_tm("%Z", tm_from_time_t(january_stamp, false));
  const std::string july_name = format_tm("%Z", tm_from_time_t(july_stamp, false));
  info.daylight = january_offset != july_offset ? 1 : 0;
  info.timezone = january_offset >= july_offset ? january_offset : july_offset;
  info.altzone = january_offset < july_offset ? january_offset : july_offset;
  info.standard_name = january_offset >= july_offset ? january_name : july_name;
  info.daylight_name = january_offset < july_offset ? january_name : july_name;
  if (info.standard_name.empty()) {
    info.standard_name = "UTC";
  }
  if (info.daylight_name.empty()) {
    info.daylight_name = info.standard_name;
  }
#endif
  if (!info.daylight) {
    info.altzone = info.timezone;
    info.daylight_name = info.standard_name;
  }
  return info;
}

} // namespace

void register_time_module(Runtime& runtime) {
  auto* state = new TimeModuleState();
  const Value* tuple_base = runtime.find_builtin("tuple");
  state->struct_time_class = Value::class_object(
      "struct_time",
      {
          {"__module__", Value::string("time")},
          {"__init__", runtime.make_native_function("time.struct_time.__init__", time_struct_time_init)},
          {"count", runtime.make_native_function("time.struct_time.count", time_struct_time_count)},
          {"index", runtime.make_native_function("time.struct_time.index", time_struct_time_index)},
          {"n_sequence_fields", Value::int64(9)},
          {"n_fields", Value::int64(11)},
          {"n_unnamed_fields", Value::int64(0)},
          {"tm_zone", make_member_descriptor("tm_zone")},
          {"tm_gmtoff", make_member_descriptor("tm_gmtoff")},
      },
      tuple_base != nullptr ? *tuple_base : Value::invalid());
  runtime.register_native_package_cleanup(state, time_module_state_cleanup);

  const TimezoneInfo timezone = platform_timezone_info();

  NativeModuleBuilder builder(runtime, "time");
  builder.function("time", time_time)
      .function("time_ns", time_time_ns)
      .function("monotonic", time_monotonic)
      .function("monotonic_ns", time_monotonic_ns)
      .function("perf_counter", time_monotonic)
      .function("perf_counter_ns", time_monotonic_ns)
      .function("process_time", time_process_time)
      .function("process_time_ns", time_process_time_ns)
      .function("thread_time", time_thread_time)
      .function("thread_time_ns", time_thread_time_ns)
      .function("get_clock_info", time_get_clock_info)
      .function("sleep", time_sleep)
      .value("localtime", runtime.make_native_function("time.localtime", time_localtime, state))
      .value("gmtime", runtime.make_native_function("time.gmtime", time_gmtime, state))
      .function("mktime", time_mktime)
      .function("strftime", time_strftime)
      .value("strptime", runtime.make_native_function("time.strptime", time_strptime, state))
      .function("asctime", time_asctime)
      .function("ctime", time_ctime)
      .value("struct_time", state->struct_time_class)
      .value("timezone", Value::int64(timezone.timezone))
      .value("altzone", Value::int64(timezone.altzone))
      .value("daylight", Value::int64(timezone.daylight))
      .value("tzname", Value::tuple({Value::string(timezone.standard_name), Value::string(timezone.daylight_name)}));
  runtime.register_module("time", builder.finish());
}

} // namespace xlang3
