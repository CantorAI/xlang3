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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/value_hash.h"

#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <limits>
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
  std::string standard_timezone_name = "UTC";
  std::string daylight_timezone_name = "UTC";
  bool has_daylight_timezone = false;
};

void time_module_state_cleanup(void* data) {
  delete static_cast<TimeModuleState*>(data);
}

bool no_args(Runtime& runtime, uint32_t argc, const char* name, std::string& error) {
  if (argc == 0) {
    return true;
  }
  error = std::string(name) + "() takes no arguments (" + std::to_string(argc) + " given)";
  runtime.raise_class_error("TypeError", error);
  return false;
}

Value time_native_function(
    Runtime& runtime,
    const std::string& qualified_name,
    const std::string& function_name,
    NativeFunctionCallback callback,
    const std::string& doc,
    void* user_data = nullptr,
    NativeKeywordFunctionCallback keyword_callback = nullptr,
    const std::string& qualname_override = "") {
  Value function = runtime.make_native_function(qualified_name, callback, user_data, nullptr, nullptr, false, keyword_callback);
  if (auto* native = value_as_native_function(function)) {
    native->attrs_dict = new Value(Value::dict({
        {Value::string("__module__"), Value::string("time")},
        {Value::string("__name__"), Value::string(function_name)},
        {Value::string("__qualname__"), Value::string(qualname_override.empty() ? function_name : qualname_override)},
        {Value::string("__doc__"), Value::string(doc)},
    }));
  }
  return function;
}

std::string time_type_name(const Value& value) {
  switch (value.tag) {
  case ValueTag::None:
    return "NoneType";
  case ValueTag::Bool:
    return "bool";
  case ValueTag::Int64:
    return "int";
  case ValueTag::Double:
    return "float";
  case ValueTag::Object:
    if (value_as_string(value) != nullptr) {
      return "str";
    }
    if (value_as_list(value) != nullptr) {
      return "list";
    }
    if (value_as_tuple(value) != nullptr) {
      return "tuple";
    }
    if (value_as_dict(value) != nullptr) {
      return "dict";
    }
    return "object";
  case ValueTag::Invalid:
  default:
    return "object";
  }
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
  Value info = Value::instance(Value::class_object(
      "SimpleNamespace",
      {{"__module__", Value::string("types")},
       {"__qualname__", Value::string("SimpleNamespace")},
       {"__doc__", Value::string("A simple attribute-based namespace.")}}));
  std::string ignored;
  object_set_attr(info, "adjustable", Value::boolean(adjustable), ignored);
  object_set_attr(info, "monotonic", Value::boolean(monotonic), ignored);
  object_set_attr(info, "resolution", Value::number(resolution), ignored);
  object_set_attr(info, "implementation", Value::string(implementation), ignored);
  object_set_attr(
      info,
      "__xlang3_string_value__",
      Value::string(
          "namespace(implementation='" + implementation + "', monotonic=" +
          std::string(monotonic ? "True" : "False") + ", adjustable=" +
          std::string(adjustable ? "True" : "False") + ", resolution=" +
          value_to_string(Value::number(resolution)) + ")"),
      ignored);
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

bool is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int day_of_year_zero_based(int year, int month_one_based, int day) {
  static const int days_before_month[] = {
      0,
      0,
      31,
      59,
      90,
      120,
      151,
      181,
      212,
      243,
      273,
      304,
      334,
  };
  int yday = days_before_month[month_one_based] + day - 1;
  if (month_one_based > 2 && is_leap_year(year)) {
    ++yday;
  }
  return yday;
}

int days_in_month(int year, int month_one_based) {
  static const int days_in_month_common[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month_one_based < 1 || month_one_based > 12) {
    return 0;
  }
  if (month_one_based == 2 && is_leap_year(year)) {
    return 29;
  }
  return days_in_month_common[month_one_based - 1];
}

bool valid_strptime_month_day(const std::tm& tm, bool explicit_year) {
  const int year = tm.tm_year + 1900;
  const int month = tm.tm_mon + 1;
  const int max_day = days_in_month(year, month);
  if (tm.tm_mday >= 1 && tm.tm_mday <= max_day) {
    return true;
  }
  return !explicit_year && year == 1900 && month == 2 && tm.tm_mday == 29;
}

bool month_day_from_yday(int year, int yday_one_based, int& month, int& day) {
  if (yday_one_based < 1 || yday_one_based > (is_leap_year(year) ? 366 : 365)) {
    return false;
  }
  static const int days_in_month_common[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int remaining = yday_one_based;
  for (int index = 0; index < 12; ++index) {
    int days = days_in_month_common[index];
    if (index == 1 && is_leap_year(year)) {
      ++days;
    }
    if (remaining <= days) {
      month = index + 1;
      day = remaining;
      return true;
    }
    remaining -= days;
  }
  return false;
}

int days_in_year(int year) {
  return is_leap_year(year) ? 366 : 365;
}

int ordinal_from_date(int year, int month_one_based, int day) {
  int ordinal = 0;
  if (year >= 1900) {
    for (int current = 1900; current < year; ++current) {
      ordinal += days_in_year(current);
    }
  } else {
    for (int current = year; current < 1900; ++current) {
      ordinal -= days_in_year(current);
    }
  }
  return ordinal + day_of_year_zero_based(year, month_one_based, day);
}

void date_from_ordinal(int ordinal, int& year, int& month, int& day) {
  year = 1900;
  if (ordinal >= 0) {
    while (ordinal >= days_in_year(year)) {
      ordinal -= days_in_year(year);
      ++year;
    }
  } else {
    do {
      --year;
      ordinal += days_in_year(year);
    } while (ordinal < 0);
  }
  month_day_from_yday(year, ordinal + 1, month, day);
}

int c_weekday_from_date(int year, int month_one_based, int day) {
  static const int month_offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month_one_based < 1 || month_one_based > 12) {
    return 0;
  }
  if (month_one_based < 3) {
    --year;
  }
  return (year + year / 4 - year / 100 + year / 400 + month_offsets[month_one_based - 1] + day) % 7;
}

bool iso_week_date_to_calendar(int iso_year, int iso_week, int iso_weekday, std::tm& tm) {
  if (iso_week < 1 || iso_week > 53 || iso_weekday < 1 || iso_weekday > 7) {
    return false;
  }
  const int jan4_ordinal = ordinal_from_date(iso_year, 1, 4);
  const int jan4_monday_weekday = (c_weekday_from_date(iso_year, 1, 4) + 6) % 7;
  const int week1_monday_ordinal = jan4_ordinal - jan4_monday_weekday;
  const int target_ordinal = week1_monday_ordinal + (iso_week - 1) * 7 + (iso_weekday - 1);
  int year = 0;
  int month = 0;
  int day = 0;
  date_from_ordinal(target_ordinal, year, month, day);
  const int target_iso_year = [&]() {
    const int weekday_monday = (c_weekday_from_date(year, month, day) + 6) % 7;
    int thursday_year = 0;
    int thursday_month = 0;
    int thursday_day = 0;
    date_from_ordinal(target_ordinal + (3 - weekday_monday), thursday_year, thursday_month, thursday_day);
    return thursday_year;
  }();
  if (target_iso_year != iso_year) {
    return false;
  }
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  return true;
}

int iso_weekday_from_monday_weekday(int monday_weekday) {
  return monday_weekday == 6 ? 7 : monday_weekday + 1;
}

std::string ascii_lower(std::string text) {
  for (char& ch : text) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return text;
}

bool strptime_format_has_explicit_year(const std::string& format) {
  for (size_t i = 0; i + 1 < format.size(); ++i) {
    if (format[i] != '%') {
      continue;
    }
    const char directive = format[++i];
    if (directive == 'Y' || directive == 'y' || directive == 'G' || directive == 'c' || directive == 'x') {
      return true;
    }
  }
  return false;
}

bool validate_strptime_format_directives(
    const std::string& format,
    bool& unsupported_directive,
    char& unsupported_directive_char,
    bool& stray_percent) {
  unsupported_directive = false;
  unsupported_directive_char = '\0';
  stray_percent = false;
  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%') {
      continue;
    }
    if (++i >= format.size()) {
      stray_percent = true;
      return false;
    }
    switch (format[i]) {
      case '%':
      case 'Y':
      case 'y':
      case 'G':
      case 'V':
      case 'm':
      case 'd':
      case 'e':
      case 'H':
      case 'k':
      case 'I':
      case 'l':
      case 'p':
      case 'P':
      case 'M':
      case 'S':
      case 'f':
      case 'j':
      case 'U':
      case 'W':
      case 'w':
      case 'u':
      case 'a':
      case 'A':
      case 'b':
      case 'h':
      case 'B':
      case 'X':
      case 'R':
      case 'T':
      case 'r':
      case 'x':
      case 'c':
      case 'z':
      case 'Z':
        break;
      default:
        unsupported_directive = true;
        unsupported_directive_char = format[i];
        return false;
    }
  }
  return true;
}

bool parse_fixed_digits(const std::string& text, size_t& pos, size_t min_digits, size_t max_digits, int& out) {
  const size_t start = pos;
  int value = 0;
  size_t count = 0;
  while (pos < text.size() && count < max_digits && std::isdigit(static_cast<unsigned char>(text[pos]))) {
    value = value * 10 + (text[pos] - '0');
    ++pos;
    ++count;
  }
  if (count < min_digits) {
    pos = start;
    return false;
  }
  out = value;
  return true;
}

bool consume_fractional_seconds(const std::string& text, size_t& pos) {
  const size_t start = pos;
  size_t count = 0;
  while (pos < text.size() && count < 6 && std::isdigit(static_cast<unsigned char>(text[pos]))) {
    ++pos;
    ++count;
  }
  if (count == 0) {
    pos = start;
    return false;
  }
  return true;
}

bool consume_case_word(const std::string& text, size_t& pos, const std::vector<const char*>& words, int& out) {
  const std::string tail = ascii_lower(text.substr(pos));
  for (size_t i = 0; i < words.size(); ++i) {
    const std::string word = ascii_lower(words[i]);
    if (tail.rfind(word, 0) == 0) {
      pos += word.size();
      out = static_cast<int>(i);
      return true;
    }
  }
  return false;
}

bool consume_strptime_spaces(const std::string& text, size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  return true;
}

bool consume_required_strptime_spaces(const std::string& text, size_t& pos) {
  if (pos >= text.size() || !std::isspace(static_cast<unsigned char>(text[pos]))) {
    return false;
  }
  return consume_strptime_spaces(text, pos);
}

bool consume_strptime_literal(const std::string& text, size_t& pos, char expected) {
  if (pos >= text.size() || text[pos] != expected) {
    return false;
  }
  ++pos;
  return true;
}

void consume_optional_strptime_hour_space(const std::string& text, size_t& pos) {
  if (pos < text.size() && text[pos] == ' ') {
    ++pos;
  }
}

bool parse_timezone_offset(const std::string& text, size_t& pos, Value& gmtoff) {
  if (pos < text.size() && text[pos] == 'Z') {
    ++pos;
    gmtoff = Value::int64(0);
    return true;
  }
  if (pos >= text.size() || (text[pos] != '+' && text[pos] != '-')) {
    return false;
  }
  const int sign = text[pos] == '-' ? -1 : 1;
  ++pos;
  int hour = 0;
  int minute = 0;
  if (!parse_fixed_digits(text, pos, 2, 2, hour)) {
    return false;
  }
  const bool minute_has_colon = pos < text.size() && text[pos] == ':';
  if (pos < text.size() && text[pos] == ':') {
    ++pos;
  }
  if (!parse_fixed_digits(text, pos, 2, 2, minute)) {
    return false;
  }
  int second = 0;
  if (pos < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == ':')) {
    const bool second_has_colon = text[pos] == ':';
    if (second_has_colon != minute_has_colon) {
      return false;
    }
    if (second_has_colon) {
      ++pos;
    }
    if (!parse_fixed_digits(text, pos, 2, 2, second)) {
      return false;
    }
    if (pos < text.size() && text[pos] == '.') {
      ++pos;
      size_t fraction_digits = 0;
      while (pos < text.size() && fraction_digits < 6 && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        ++pos;
        ++fraction_digits;
      }
      if (fraction_digits == 0) {
        return false;
      }
      if (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        return false;
      }
    }
  }
  if (minute > 59 || second > 59) {
    return false;
  }
  gmtoff = Value::int64(sign * static_cast<int64_t>(hour * 3600 + minute * 60 + second));
  return true;
}

bool parse_timezone_name(
    const std::string& text,
    size_t& pos,
    Value& zone,
    int& isdst,
    const TimeModuleState* state) {
  const size_t start = pos;
  const std::string tail = ascii_lower(text.substr(pos));
  if (tail.rfind("utc", 0) == 0) {
    pos += 3;
    zone = Value::string(text.substr(start, 3));
    isdst = 0;
    return true;
  }
  if (tail.rfind("gmt", 0) == 0) {
    pos += 3;
    zone = Value::string(text.substr(start, 3));
    isdst = 0;
    return true;
  }
  if (state != nullptr) {
    const std::string standard = ascii_lower(state->standard_timezone_name);
    if (!standard.empty() && tail.rfind(standard, 0) == 0) {
      pos += state->standard_timezone_name.size();
      zone = Value::string(text.substr(start, state->standard_timezone_name.size()));
      isdst = 0;
      return true;
    }
    const std::string daylight = ascii_lower(state->daylight_timezone_name);
    if (!daylight.empty() && tail.rfind(daylight, 0) == 0) {
      pos += state->daylight_timezone_name.size();
      zone = Value::string(text.substr(start, state->daylight_timezone_name.size()));
      isdst = state->has_daylight_timezone ? 1 : 0;
      return true;
    }
  }
  return false;
}

enum class StrptimeFailureReason {
  None,
  IsoWeekWithCalendarYear,
  IsoWeekMissingIsoYear,
  IsoYearIncomplete,
  IsoYearWithOrdinalDay,
  InvalidIsoWeek,
  YearOutOfRange,
};

bool parse_strptime_directives(
    const std::string& text,
    const std::string& format,
    std::tm& tm,
    Value& zone,
    Value& gmtoff,
    bool& explicit_year,
    bool& unsupported_directive,
    char& unsupported_directive_char,
    bool& stray_percent,
    StrptimeFailureReason& failure_reason,
    const TimeModuleState* state = nullptr) {
  size_t text_pos = 0;
  int parsed_yday = -1;
  int parsed_hour12 = -1;
  int parsed_meridiem = -1;
  int parsed_week_sunday = -1;
  int parsed_week_monday = -1;
  int parsed_weekday_sunday = -1;
  int parsed_weekday_monday = -1;
  int parsed_iso_year = -1;
  int parsed_iso_week = -1;
  int parsed_iso_weekday = -1;
  bool saw_month_day = false;
  bool saw_weekday = false;
  bool saw_calendar_year = false;
  explicit_year = false;
  unsupported_directive = false;
  unsupported_directive_char = '\0';
  stray_percent = false;
  failure_reason = StrptimeFailureReason::None;
  for (size_t format_pos = 0; format_pos < format.size(); ++format_pos) {
    const char fmt = format[format_pos];
    if (std::isspace(static_cast<unsigned char>(fmt))) {
      if (text_pos >= text.size() || !std::isspace(static_cast<unsigned char>(text[text_pos]))) {
        return false;
      }
      do {
        ++text_pos;
      } while (text_pos < text.size() && std::isspace(static_cast<unsigned char>(text[text_pos])));
      while (format_pos + 1 < format.size() &&
             std::isspace(static_cast<unsigned char>(format[format_pos + 1]))) {
        ++format_pos;
      }
      continue;
    }
    if (fmt != '%') {
      if (text_pos >= text.size() || text[text_pos] != fmt) {
        return false;
      }
      ++text_pos;
      continue;
    }
    if (++format_pos >= format.size()) {
      stray_percent = true;
      return false;
    }
    int value = 0;
    switch (format[format_pos]) {
      case '%':
        if (text_pos >= text.size() || text[text_pos] != '%') {
          return false;
        }
        ++text_pos;
        break;
      case 'Y':
        if (!parse_fixed_digits(text, text_pos, 4, 4, value)) {
          return false;
        }
        if (value < 1) {
          failure_reason = StrptimeFailureReason::YearOutOfRange;
          return false;
        }
        tm.tm_year = value - 1900;
        saw_calendar_year = true;
        explicit_year = true;
        break;
      case 'y':
        if (!parse_fixed_digits(text, text_pos, 2, 2, value)) {
          return false;
        }
        tm.tm_year = (value <= 68 ? value + 2000 : value + 1900) - 1900;
        saw_calendar_year = true;
        explicit_year = true;
        break;
      case 'G':
        if (!parse_fixed_digits(text, text_pos, 4, 4, value)) {
          return false;
        }
        if (value < 1) {
          failure_reason = StrptimeFailureReason::YearOutOfRange;
          return false;
        }
        parsed_iso_year = value;
        explicit_year = true;
        break;
      case 'V':
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 53) {
          return false;
        }
        parsed_iso_week = value;
        break;
      case 'm':
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 12) {
          return false;
        }
        tm.tm_mon = value - 1;
        saw_month_day = true;
        break;
      case 'd':
        if (text_pos < text.size() && text[text_pos] == ' ') {
          ++text_pos;
        }
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 31) {
          return false;
        }
        tm.tm_mday = value;
        saw_month_day = true;
        break;
      case 'e':
        if (text_pos < text.size() && text[text_pos] == ' ') {
          ++text_pos;
        }
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 31) {
          return false;
        }
        tm.tm_mday = value;
        saw_month_day = true;
        break;
      case 'H':
        consume_optional_strptime_hour_space(text, text_pos);
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 23) {
          return false;
        }
        tm.tm_hour = value;
        break;
      case 'k':
        if (text_pos < text.size() && text[text_pos] == ' ') {
          ++text_pos;
        }
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 23) {
          return false;
        }
        tm.tm_hour = value;
        break;
      case 'I':
        consume_optional_strptime_hour_space(text, text_pos);
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 12) {
          return false;
        }
        parsed_hour12 = value;
        break;
      case 'l':
        if (text_pos < text.size() && text[text_pos] == ' ') {
          ++text_pos;
        }
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 12) {
          return false;
        }
        parsed_hour12 = value;
        break;
      case 'p':
        if (!consume_case_word(text, text_pos, {"AM", "PM"}, value)) {
          return false;
        }
        parsed_meridiem = value;
        break;
      case 'P':
        if (!consume_case_word(text, text_pos, {"am", "pm"}, value)) {
          return false;
        }
        parsed_meridiem = value;
        break;
      case 'M':
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 59) {
          return false;
        }
        tm.tm_min = value;
        break;
      case 'S':
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 61) {
          return false;
        }
        tm.tm_sec = value;
        break;
      case 'f':
        if (!consume_fractional_seconds(text, text_pos)) {
          return false;
        }
        break;
      case 'j':
        if (!parse_fixed_digits(text, text_pos, 1, 3, value) || value < 1 || value > 366) {
          return false;
        }
        parsed_yday = value;
        break;
      case 'U':
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 53) {
          return false;
        }
        parsed_week_sunday = value;
        break;
      case 'W':
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 53) {
          return false;
        }
        parsed_week_monday = value;
        break;
      case 'w':
        if (!parse_fixed_digits(text, text_pos, 1, 1, value) || value > 6) {
          return false;
        }
        parsed_weekday_sunday = value;
        parsed_weekday_monday = value == 0 ? 6 : value - 1;
        parsed_iso_weekday = iso_weekday_from_monday_weekday(parsed_weekday_monday);
        tm.tm_wday = value;
        saw_weekday = true;
        break;
      case 'u':
        if (!parse_fixed_digits(text, text_pos, 1, 1, value) || value < 1 || value > 7) {
          return false;
        }
        parsed_weekday_sunday = value == 7 ? 0 : value;
        parsed_weekday_monday = value - 1;
        parsed_iso_weekday = value;
        tm.tm_wday = value == 7 ? 0 : value;
        saw_weekday = true;
        break;
      case 'a':
        if (!consume_case_word(text, text_pos, {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}, value)) {
          return false;
        }
        tm.tm_wday = (value + 1) % 7;
        parsed_weekday_sunday = tm.tm_wday;
        parsed_weekday_monday = value;
        parsed_iso_weekday = value + 1;
        saw_weekday = true;
        break;
      case 'A':
        if (!consume_case_word(text, text_pos, {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"}, value)) {
          return false;
        }
        tm.tm_wday = (value + 1) % 7;
        parsed_weekday_sunday = tm.tm_wday;
        parsed_weekday_monday = value;
        parsed_iso_weekday = value + 1;
        saw_weekday = true;
        break;
      case 'b':
      case 'h':
        if (!consume_case_word(text, text_pos, {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}, value)) {
          return false;
        }
        tm.tm_mon = value;
        saw_month_day = true;
        break;
      case 'B':
        if (!consume_case_word(text, text_pos, {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"}, value)) {
          return false;
        }
        tm.tm_mon = value;
        saw_month_day = true;
        break;
      case 'X':
        consume_optional_strptime_hour_space(text, text_pos);
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 23) {
          return false;
        }
        tm.tm_hour = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 59) {
          return false;
        }
        tm.tm_min = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 61) {
          return false;
        }
        tm.tm_sec = value;
        break;
      case 'R':
        consume_optional_strptime_hour_space(text, text_pos);
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 23) {
          return false;
        }
        tm.tm_hour = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 59) {
          return false;
        }
        tm.tm_min = value;
        break;
      case 'T':
        consume_optional_strptime_hour_space(text, text_pos);
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 23) {
          return false;
        }
        tm.tm_hour = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 59) {
          return false;
        }
        tm.tm_min = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 61) {
          return false;
        }
        tm.tm_sec = value;
        break;
      case 'r':
        consume_optional_strptime_hour_space(text, text_pos);
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 12) {
          return false;
        }
        parsed_hour12 = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 59) {
          return false;
        }
        tm.tm_min = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 61) {
          return false;
        }
        tm.tm_sec = value;
        if (text_pos >= text.size() || !std::isspace(static_cast<unsigned char>(text[text_pos]))) {
          return false;
        }
        consume_strptime_spaces(text, text_pos);
        if (!consume_case_word(text, text_pos, {"AM", "PM"}, value)) {
          return false;
        }
        parsed_meridiem = value;
        break;
      case 'x':
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 12) {
          return false;
        }
        tm.tm_mon = value - 1;
        if (!consume_strptime_literal(text, text_pos, '/') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 31) {
          return false;
        }
        tm.tm_mday = value;
        if (!consume_strptime_literal(text, text_pos, '/') ||
            !parse_fixed_digits(text, text_pos, 2, 2, value)) {
          return false;
        }
        tm.tm_year = (value <= 68 ? value + 2000 : value + 1900) - 1900;
        saw_calendar_year = true;
        explicit_year = true;
        saw_month_day = true;
        break;
      case 'c':
        if (!consume_case_word(text, text_pos, {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}, value)) {
          return false;
        }
        tm.tm_wday = (value + 1) % 7;
        parsed_weekday_sunday = tm.tm_wday;
        parsed_weekday_monday = value;
        parsed_iso_weekday = value + 1;
        saw_weekday = true;
        if (!consume_required_strptime_spaces(text, text_pos)) {
          return false;
        }
        if (!consume_case_word(text, text_pos, {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}, value)) {
          return false;
        }
        tm.tm_mon = value;
        saw_month_day = true;
        if (!consume_required_strptime_spaces(text, text_pos)) {
          return false;
        }
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value < 1 || value > 31) {
          return false;
        }
        tm.tm_mday = value;
        if (!consume_required_strptime_spaces(text, text_pos)) {
          return false;
        }
        consume_optional_strptime_hour_space(text, text_pos);
        if (!parse_fixed_digits(text, text_pos, 1, 2, value) || value > 23) {
          return false;
        }
        tm.tm_hour = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 59) {
          return false;
        }
        tm.tm_min = value;
        if (!consume_strptime_literal(text, text_pos, ':') ||
            !parse_fixed_digits(text, text_pos, 1, 2, value) || value > 61) {
          return false;
        }
        tm.tm_sec = value;
        if (!consume_required_strptime_spaces(text, text_pos)) {
          return false;
        }
        if (!parse_fixed_digits(text, text_pos, 4, 4, value)) {
          return false;
        }
        tm.tm_year = value - 1900;
        saw_calendar_year = true;
        explicit_year = true;
        break;
      case 'z':
        if (!parse_timezone_offset(text, text_pos, gmtoff)) {
          return false;
        }
        break;
      case 'Z':
        if (!parse_timezone_name(text, text_pos, zone, tm.tm_isdst, state)) {
          return false;
        }
        break;
      default:
        unsupported_directive = true;
        unsupported_directive_char = format[format_pos];
        return false;
    }
  }
  if (text_pos != text.size()) {
    return false;
  }
  if (parsed_iso_week != -1 && saw_calendar_year) {
    failure_reason = StrptimeFailureReason::IsoWeekWithCalendarYear;
    return false;
  }
  if (parsed_iso_year != -1 && parsed_yday != -1) {
    failure_reason = StrptimeFailureReason::IsoYearWithOrdinalDay;
    return false;
  }
  if (parsed_iso_year == -1 && parsed_iso_week != -1) {
    failure_reason = StrptimeFailureReason::IsoWeekMissingIsoYear;
    return false;
  }
  if (parsed_iso_year != -1 || parsed_iso_week != -1) {
    if (parsed_iso_year == -1 || parsed_iso_week == -1 || parsed_iso_weekday == -1) {
      failure_reason = StrptimeFailureReason::IsoYearIncomplete;
      return false;
    }
    if (!iso_week_date_to_calendar(parsed_iso_year, parsed_iso_week, parsed_iso_weekday, tm)) {
      failure_reason = StrptimeFailureReason::InvalidIsoWeek;
      return false;
    }
    saw_month_day = true;
    parsed_yday = day_of_year_zero_based(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) + 1;
  }
  if (parsed_yday != -1) {
    int month = 0;
    int day = 0;
    const int year = tm.tm_year + 1900;
    if (!month_day_from_yday(year, parsed_yday, month, day)) {
      if (parsed_yday != 366 || is_leap_year(year)) {
        return false;
      }
      tm.tm_year = year + 1 - 1900;
      month = 1;
      day = 1;
    }
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
  }
  if (!saw_month_day && parsed_yday == -1 &&
      ((parsed_week_sunday != -1 && parsed_weekday_sunday != -1) ||
       (parsed_week_monday != -1 && parsed_weekday_monday != -1))) {
    const int year = tm.tm_year + 1900;
    const int jan1_c_weekday = c_weekday_from_date(year, 1, 1);
    int yday = 0;
    if (parsed_week_sunday != -1 && parsed_weekday_sunday != -1) {
      const int first_sunday = (7 - jan1_c_weekday) % 7;
      yday = parsed_week_sunday == 0
          ? parsed_weekday_sunday - jan1_c_weekday
          : first_sunday + (parsed_week_sunday - 1) * 7 + parsed_weekday_sunday;
    } else {
      const int jan1_monday_weekday = (jan1_c_weekday + 6) % 7;
      const int first_monday = (7 - jan1_monday_weekday) % 7;
      yday = parsed_week_monday == 0
          ? parsed_weekday_monday - jan1_monday_weekday
          : first_monday + (parsed_week_monday - 1) * 7 + parsed_weekday_monday;
    }
    int month = 0;
    int day = 0;
    const int year_days = is_leap_year(year) ? 366 : 365;
    if (yday < 0) {
      const int previous_year = year - 1;
      const int previous_year_days = is_leap_year(previous_year) ? 366 : 365;
      if (!month_day_from_yday(previous_year, previous_year_days + yday + 1, month, day)) {
        return false;
      }
      tm.tm_year = previous_year - 1900;
      parsed_yday = previous_year_days + yday + 1;
    } else if (yday >= year_days) {
      if (!month_day_from_yday(year + 1, yday - year_days + 1, month, day)) {
        return false;
      }
      tm.tm_year = year + 1 - 1900;
      parsed_yday = yday + 1;
    } else {
      if (!month_day_from_yday(year, yday + 1, month, day)) {
        return false;
      }
      parsed_yday = yday + 1;
    }
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
  }
  if (parsed_hour12 != -1) {
    tm.tm_hour = parsed_hour12 % 12;
    if (parsed_meridiem == 1) {
      tm.tm_hour += 12;
    }
  }
  if (!saw_weekday) {
    tm.tm_wday = c_weekday_from_date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  }
  tm.tm_yday = parsed_yday != -1 ? parsed_yday - 1 : day_of_year_zero_based(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return true;
}

bool normalize_strftime_format(const std::string& format, std::string& out, std::string& error) {
  out.clear();
  out.reserve(format.size());
  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%') {
      out.push_back(format[i]);
      continue;
    }
    if (++i >= format.size()) {
      error = "Invalid format string";
      return false;
    }
    const char directive = format[i];
    switch (directive) {
      case 'c':
        out += "%a %b %e %H:%M:%S %Y";
        break;
      case 'r':
        out += "%I:%M:%S %p";
        break;
#if defined(_WIN32)
      case 'f':
      case 'k':
      case 'l':
      case 'P':
      case 'q':
      case 'Q':
      case 's':
        error = "Invalid format string";
        return false;
#endif
      default:
        out.push_back('%');
        out.push_back(directive);
        break;
    }
  }
  return true;
}

std::string format_tm(const std::string& format, const std::tm& tm) {
  std::ostringstream stream;
  stream << std::put_time(&tm, format.c_str());
  return stream.str();
}

std::string format_asctime_tm(const std::tm& tm) {
  static constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const int wday = tm.tm_wday >= 0 && tm.tm_wday < 7 ? tm.tm_wday : 0;
  const int month = tm.tm_mon >= 0 && tm.tm_mon < 12 ? tm.tm_mon : 0;
  std::ostringstream stream;
  stream << kWeekdays[wday] << ' ' << kMonths[month] << ' '
         << std::setw(2) << tm.tm_mday << ' '
         << std::setfill('0') << std::setw(2) << tm.tm_hour << ':'
         << std::setw(2) << tm.tm_min << ':' << std::setw(2) << tm.tm_sec
         << std::setfill(' ') << ' ' << (tm.tm_year + 1900);
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

Value make_member_descriptor(const std::string& owner_name, const std::string& name, uint32_t index) {
  return slot_descriptor(owner_name, name, index);
}

Value struct_time_match_args() {
  return Value::tuple({
      Value::string("tm_year"),
      Value::string("tm_mon"),
      Value::string("tm_mday"),
      Value::string("tm_hour"),
      Value::string("tm_min"),
      Value::string("tm_sec"),
      Value::string("tm_wday"),
      Value::string("tm_yday"),
      Value::string("tm_isdst"),
  });
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
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? static_cast<std::time_t>(1) : static_cast<std::time_t>(0);
    return true;
  }
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

bool optional_timestamp(Runtime& runtime, const Value* args, uint32_t argc, const char* name, std::time_t& out, std::string& error) {
  if (argc > 1) {
    error = std::string(name) + "() takes at most 1 argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 0 || args[0].tag == ValueTag::None) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    out = static_cast<std::time_t>(std::chrono::duration<double>(now).count());
    return true;
  }
  if (args[0].tag == ValueTag::Double) {
    const double seconds = args[0].as.f64;
    if (std::isnan(seconds)) {
      error = "Invalid value NaN (not a number)";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (!std::isfinite(seconds) ||
        seconds < static_cast<double>(std::numeric_limits<std::time_t>::min()) ||
        seconds > static_cast<double>(std::numeric_limits<std::time_t>::max())) {
      error = "timestamp out of range for platform time_t";
      runtime.raise_class_error("OverflowError", error);
      return false;
    }
  }
  if (!numeric_time_arg(args[0], out, error)) {
    error = "'" + time_type_name(args[0]) + "' object cannot be interpreted as an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool int_from_value(const Value& value, const char* name, int& out, std::string& error) {
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  if (value.tag != ValueTag::Int64) {
    (void)name;
    error = "'" + time_type_name(value) + "' object cannot be interpreted as an integer";
    return false;
  }
  out = static_cast<int>(value.as.i64);
  return true;
}

bool struct_time_tuple_storage(const Value& self, const char* method, TupleObject*& out, std::string& error) {
  Value tuple_value;
  std::string ignored;
  if (!object_get_attr(self, "_tuple", tuple_value, ignored) || (out = value_as_tuple(tuple_value)) == nullptr) {
    error = "descriptor '" + std::string(method) + "' for 'tuple' objects doesn't apply to a '" + time_type_name(self) + "' object";
    return false;
  }
  return true;
}

bool normalize_struct_time_bound(const Value& value, size_t size, size_t& out, std::string& error) {
  int64_t index = 0;
  if (value.tag == ValueTag::Bool) {
    index = value.as.b ? 1 : 0;
  } else if (value.tag == ValueTag::Int64) {
    index = value.as.i64;
  } else {
    error = "time.struct_time.index bounds must be int";
    return false;
  }
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

bool time_struct_time_count(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "unbound method tuple.count() needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc != 2) {
    error = "tuple.count() takes exactly one argument (" + std::to_string(argc - 1) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  TupleObject* tuple = nullptr;
  if (!struct_time_tuple_storage(args[0], "count", tuple, error)) {
    runtime.raise_class_error("TypeError", error);
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

bool time_struct_time_count_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return true;
  }
  error = "tuple.count() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool time_struct_time_getnewargs(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "unbound method tuple.__getnewargs__() needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc != 1) {
    error = "tuple.__getnewargs__() takes no arguments (" + std::to_string(argc - 1) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  TupleObject* tuple = nullptr;
  if (!struct_time_tuple_storage(args[0], "__getnewargs__", tuple, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::tuple({Value::tuple(tuple->items)});
  return true;
}

bool time_struct_time_getnewargs_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "tuple.__getnewargs__() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool struct_time_reduce_payload(Runtime& runtime, const Value& self, const char* method, Value& out, std::string& error) {
  TupleObject* tuple = nullptr;
  if (!struct_time_tuple_storage(self, method, tuple, error)) {
    error = "descriptor '" + std::string(method) + "' for 'time.struct_time' objects doesn't apply to a '" + time_type_name(self) + "' object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* instance = value_as_instance(self);
  if (instance == nullptr) {
    error = "descriptor '" + std::string(method) + "' for 'time.struct_time' objects doesn't apply to a '" + time_type_name(self) + "' object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value zone;
  Value gmtoff;
  std::string ignored;
  if (!object_get_attr(self, "tm_zone", zone, ignored)) {
    zone = Value::none();
  }
  if (!object_get_attr(self, "tm_gmtoff", gmtoff, ignored)) {
    gmtoff = Value::none();
  }
  out = Value::tuple({
      instance->klass,
      Value::tuple({
          Value::tuple(tuple->items),
          Value::dict({
              {Value::string("tm_zone"), zone},
              {Value::string("tm_gmtoff"), gmtoff},
          }),
      }),
  });
  return true;
}

bool time_struct_time_reduce(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "unbound method struct_time.__reduce__() needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc != 1) {
    error = "struct_time.__reduce__() takes no arguments (" + std::to_string(argc - 1) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return struct_time_reduce_payload(runtime, args[0], "__reduce__", out, error);
}

bool time_struct_time_reduce_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "struct_time.__reduce__() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool time_struct_time_reduce_ex(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "unbound method object.__reduce_ex__() needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc != 2) {
    error = "object.__reduce_ex__() takes exactly one argument (" + std::to_string(argc - 1) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int protocol = 0;
  if (!int_from_value(args[1], "protocol", protocol, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return struct_time_reduce_payload(runtime, args[0], "__reduce_ex__", out, error);
}

bool time_struct_time_reduce_ex_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "object.__reduce_ex__() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool time_struct_time_index(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "unbound method tuple.index() needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc < 2) {
    error = "index expected at least 1 argument, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 4) {
    error = "index expected at most 3 arguments, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  TupleObject* tuple = nullptr;
  if (!struct_time_tuple_storage(args[0], "index", tuple, error)) {
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

bool time_struct_time_index_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return true;
  }
  error = "tuple.index() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

std::string struct_time_field_repr(const Value& value) {
  if (auto* string = value_as_string(value)) {
    std::string text;
    text.push_back('\'');
    for (char ch : string_object_view(*string)) {
      if (ch == '\'' || ch == '\\') {
        text.push_back('\\');
      }
      if (ch == '\n') {
        text += "\\n";
      } else if (ch == '\r') {
        text += "\\r";
      } else if (ch == '\t') {
        text += "\\t";
      } else {
        text.push_back(ch);
      }
    }
    text.push_back('\'');
    return text;
  }
  return value_to_string(value);
}

bool time_struct_time_repr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "descriptor '__repr__' of 'time.struct_time' object needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc != 1) {
    error = "expected 0 arguments, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
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
  TupleObject* tuple = nullptr;
  if (!struct_time_tuple_storage(args[0], "__repr__", tuple, error)) {
    error = "descriptor '__repr__' requires a 'time.struct_time' object but received a '" + time_type_name(args[0]) + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string text = "time.struct_time(";
  for (size_t i = 0; i < 9 && i < tuple->items.size(); ++i) {
    if (i != 0) {
      text += ", ";
    }
    text += names[i];
    text += "=";
    text += struct_time_field_repr(tuple->items[i]);
  }
  text += ")";
  out = Value::string(std::move(text));
  return true;
}

bool time_struct_time_repr_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return true;
  }
  error = "wrapper __repr__() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
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
    error = "constructor requires a sequence";
    return false;
  }
  if (raw_items.size() < 9) {
    error = "time.struct_time() takes an at least 9-sequence (" + std::to_string(raw_items.size()) + "-sequence given)";
    return false;
  }
  if (raw_items.size() > 11) {
    error = "time.struct_time() takes an at most 11-sequence (" + std::to_string(raw_items.size()) + "-sequence given)";
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
    Runtime& runtime,
    const Value& dict_value,
    bool sequence_has_zone,
    bool sequence_has_gmtoff,
    Value& zone,
    Value& gmtoff,
    std::string& error) {
  static constexpr const char* kDuplicateOrUnexpectedFields =
      "time.struct_time() got duplicate or unexpected field name(s)";
  auto* dict = value_as_dict(dict_value);
  if (dict == nullptr) {
    if (auto* instance = value_as_instance(dict_value)) {
      dict = value_as_dict(instance->mapping_storage);
    }
  }

  auto apply_entry = [&](const Value& raw_key, const Value& value) -> bool {
    auto* key = value_as_string(raw_key);
    if (key == nullptr) {
      error = kDuplicateOrUnexpectedFields;
      return false;
    }
    const std::string name = string_object_to_string(*key);
    if (name == "tm_zone" && !sequence_has_zone) {
      value_assign_fast(zone, value);
    } else if (name == "tm_gmtoff" && !sequence_has_gmtoff) {
      value_assign_fast(gmtoff, value);
    } else {
      error = kDuplicateOrUnexpectedFields;
      return false;
    }
    return true;
  };

  if (dict != nullptr) {
    for (const auto& entry : dict->entries) {
      if (!apply_entry(entry.first, entry.second)) {
        return false;
      }
    }
    return true;
  }

  const Value* dict_type = runtime.find_builtin("dict");
  const Value* isinstance_fn = runtime.find_builtin("isinstance");
  bool is_dict_type = false;
  if (dict_type != nullptr && isinstance_fn != nullptr) {
    Value isinstance_args[2] = {dict_value, *dict_type};
    Value isinstance_result;
    if (runtime_call_callable(runtime, *isinstance_fn, isinstance_args, 2, isinstance_result, error)) {
      is_dict_type = value_truthy(isinstance_result);
    } else {
      return false;
    }
  }
  if (!is_dict_type) {
    error = "time.struct_time() takes a dict as second arg, if any";
    return false;
  }

  Value key_iterator;
  std::string iter_error;
  if (sequence_get_iter(dict_value, key_iterator, iter_error)) {
    for (;;) {
      bool done = false;
      Value key;
      if (!sequence_iter_next(key_iterator, done, key, error)) {
        return false;
      }
      if (done) {
        return true;
      }
      Value value;
      if (!sequence_get_item(dict_value, key, value, error)) {
        return false;
      }
      if (!apply_entry(key, value)) {
        return false;
      }
    }
  }

  Value items_method;
  std::string attr_error;
  if (!attribute_get(dict_value, "items", items_method, attr_error)) {
    error = "time.struct_time() takes a dict as second arg, if any";
    return false;
  }
  Value items_view;
  if (!runtime_call_callable(runtime, items_method, nullptr, 0, items_view, error)) {
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(items_view, iterator, error)) {
    Value receiver_arg = dict_value;
    if (!runtime_call_callable(runtime, items_method, &receiver_arg, 1, items_view, error)) {
      return false;
    }
    if (!sequence_get_iter(items_view, iterator, error)) {
      return false;
    }
  }
  for (;;) {
    bool done = false;
    Value pair;
    if (!sequence_iter_next(iterator, done, pair, error)) {
      return false;
    }
    if (done) {
      break;
    }
    auto* tuple = value_as_tuple(pair);
    if (tuple == nullptr || tuple->items.size() != 2) {
      error = kDuplicateOrUnexpectedFields;
      return false;
    }
    if (!apply_entry(tuple->items[0], tuple->items[1])) {
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

bool time_struct_time_new(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "time.struct_time.__new__(): not enough arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "time.struct_time.__new__() argument 1 must be type, not " + time_type_name(args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc < 2) {
    error = "structseq() missing required argument 'sequence' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 3) {
    error = "structseq() takes at most 2 arguments (" + std::to_string(argc - 1) + " given)";
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
  if (argc == 3 && !struct_time_extra_fields_from_dict(runtime, args[2], sequence_has_zone, sequence_has_gmtoff, zone, gmtoff, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::instance(args[0]);
  set_struct_time_attrs(out, items, zone, gmtoff);
  return true;
}

bool time_struct_time_new_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "time.struct_time.__new__(): not enough arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "time.struct_time.__new__() argument 1 must be type, not " + time_type_name(args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const uint32_t positional_argc = argc - 1;
  if (kwargc > 2) {
    error = "structseq() takes at most 2 keyword arguments (" + std::to_string(kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (positional_argc + kwargc > 2) {
    error = "structseq() takes at most 2 arguments (" + std::to_string(positional_argc + kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  const Value* sequence = argc >= 2 ? &args[1] : nullptr;
  const Value* fields = argc >= 3 ? &args[2] : nullptr;
  std::string unexpected_keyword;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name == "sequence") {
      if (sequence != nullptr) {
        error = "argument for structseq() given by name ('sequence') and position (1)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      sequence = kwargs[i].value;
      continue;
    }
    if (name == "dict") {
      if (fields != nullptr) {
        error = "argument for structseq() given by name ('dict') and position (2)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      fields = kwargs[i].value;
      continue;
    }
    if (unexpected_keyword.empty()) {
      unexpected_keyword = name;
    }
  }

  if (sequence == nullptr) {
    error = "structseq() missing required argument 'sequence' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!unexpected_keyword.empty()) {
    error = "structseq() got an unexpected keyword argument '" + unexpected_keyword + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  std::vector<Value> bound_args;
  bound_args.reserve(fields == nullptr ? 2 : 3);
  bound_args.push_back(args[0]);
  bound_args.push_back(*sequence);
  if (fields != nullptr) {
    bound_args.push_back(*fields);
  }
  return time_struct_time_new(runtime, bound_args.data(), static_cast<uint32_t>(bound_args.size()), out, error, nullptr);
}

bool time_struct_time_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "structseq() missing required argument 'sequence' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 3) {
    error = "structseq() takes at most 2 arguments (" + std::to_string(argc - 1) + " given)";
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
  if (argc == 3 && !struct_time_extra_fields_from_dict(runtime, args[2], sequence_has_zone, sequence_has_gmtoff, zone, gmtoff, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  set_struct_time_attrs(self, items, zone, gmtoff);
  value_set_none(out);
  return true;
}

bool time_struct_time_init_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out, std::string& error, void*) {
  const uint32_t positional_argc = argc > 0 ? argc - 1 : 0;
  if (kwargc > 2) {
    error = "structseq() takes at most 2 keyword arguments (" + std::to_string(kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (positional_argc + kwargc > 2) {
    error = "structseq() takes at most 2 arguments (" + std::to_string(positional_argc + kwargc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  const Value* sequence = argc >= 2 ? &args[1] : nullptr;
  const Value* fields = argc >= 3 ? &args[2] : nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string& name = kwargs[i].name;
    if (name == "sequence") {
      if (sequence != nullptr) {
        error = "argument for structseq() given by name ('sequence') and position (1)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      sequence = kwargs[i].value;
      continue;
    }
    if (name == "dict") {
      if (fields != nullptr) {
        error = "argument for structseq() given by name ('dict') and position (2)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      fields = kwargs[i].value;
      continue;
    }
    error = "structseq() got an unexpected keyword argument '" + name + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  if (sequence == nullptr) {
    error = "structseq() missing required argument 'sequence' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  std::vector<Value> bound_args;
  bound_args.reserve(fields == nullptr ? 2 : 3);
  bound_args.push_back(argc > 0 ? args[0] : Value::none());
  bound_args.push_back(*sequence);
  if (fields != nullptr) {
    bound_args.push_back(*fields);
  }
  return time_struct_time_init(runtime, bound_args.data(), static_cast<uint32_t>(bound_args.size()), out, error, nullptr);
}

bool time_time(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(runtime, argc, "time.time", error)) {
    return false;
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  out = Value::number(std::chrono::duration<double>(now).count());
  return true;
}

bool time_time_ns(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(runtime, argc, "time.time_ns", error)) {
    return false;
  }
  value_set_int64(out, duration_to_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())));
  return true;
}

bool time_monotonic(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(runtime, argc, "time.monotonic", error)) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  out = Value::number(std::chrono::duration<double>(now).count());
  return true;
}

bool time_monotonic_ns(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(runtime, argc, "time.monotonic_ns", error)) {
    return false;
  }
  value_set_int64(out, duration_to_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())));
  return true;
}

bool time_perf_counter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!no_args(runtime, argc, "time.perf_counter", error)) {
    return false;
  }
  return time_monotonic(runtime, args, 0, out, error, user_data);
}

bool time_perf_counter_ns(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!no_args(runtime, argc, "time.perf_counter_ns", error)) {
    return false;
  }
  return time_monotonic_ns(runtime, args, 0, out, error, user_data);
}

bool time_sleep(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "time.sleep() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  double seconds = 0.0;
  if (args[0].tag == ValueTag::Bool) {
    seconds = args[0].as.b ? 1.0 : 0.0;
  } else if (args[0].tag == ValueTag::Int64) {
    seconds = static_cast<double>(args[0].as.i64);
  } else if (args[0].tag == ValueTag::Double) {
    seconds = args[0].as.f64;
  } else {
    error = "'" + time_type_name(args[0]) + "' object cannot be interpreted as an integer or float";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (seconds < 0.0) {
    error = "sleep length must be non-negative";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  value_set_none(out);
  return true;
}

bool time_process_time(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(runtime, argc, "time.process_time", error)) {
    return false;
  }
  out = Value::number(static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC));
  return true;
}

bool time_process_time_ns(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(runtime, argc, "time.process_time_ns", error)) {
    return false;
  }
  const auto ticks = static_cast<int64_t>(std::clock());
  value_set_int64(out, static_cast<int64_t>((static_cast<long double>(ticks) * 1000000000.0L) / CLOCKS_PER_SEC));
  return true;
}

bool time_thread_time(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!no_args(runtime, argc, "time.thread_time", error)) {
    return false;
  }
  return time_process_time(runtime, args, 0, out, error, user_data);
}

bool time_thread_time_ns(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!no_args(runtime, argc, "time.thread_time_ns", error)) {
    return false;
  }
  return time_process_time_ns(runtime, args, 0, out, error, user_data);
}

bool time_get_clock_info(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "get_clock_info() takes exactly 1 argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string name;
  auto* name_string = value_as_string(args[0]);
  if (name_string == nullptr) {
    error = "get_clock_info() argument 1 must be str, not " + time_type_name(args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  name = string_object_to_string(*name_string);
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

bool time_localtime(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  std::time_t timestamp = 0;
  if (!optional_timestamp(runtime, args, argc, "localtime", timestamp, error)) {
    return false;
  }
  auto* state = static_cast<TimeModuleState*>(user_data);
  out = make_struct_time_from_timestamp(state->struct_time_class, timestamp, false);
  return true;
}

bool time_gmtime(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  std::time_t timestamp = 0;
  if (!optional_timestamp(runtime, args, argc, "gmtime", timestamp, error)) {
    return false;
  }
  auto* state = static_cast<TimeModuleState*>(user_data);
  out = make_struct_time_from_timestamp(state->struct_time_class, timestamp, true);
  return true;
}

bool time_mktime(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "time.mktime() takes exactly one argument (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::tm tm{};
  if (!tm_from_sequence_like(args[0], tm, error)) {
    if (value_as_tuple(args[0]) == nullptr && value_as_list(args[0]) == nullptr && value_as_instance(args[0]) == nullptr) {
      error = "Tuple or struct_time argument required";
    } else if (error.find("object cannot be interpreted as an integer") == std::string::npos) {
      error = "mktime(): illegal time tuple argument";
    }
    runtime.raise_class_error("TypeError", error);
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

bool time_strftime(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "strftime() takes at least 1 argument (0 given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 2) {
    error = "strftime() takes at most 2 arguments (" + std::to_string(argc) + " given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string format;
  auto* format_string = value_as_string(args[0]);
  if (format_string == nullptr) {
    error = "strftime() argument 1 must be str, not " + time_type_name(args[0]);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  format = string_object_to_string(*format_string);
  std::tm tm{};
  if (argc == 2) {
    if (!tm_from_sequence_like(args[1], tm, error)) {
      if (value_as_tuple(args[1]) == nullptr && value_as_list(args[1]) == nullptr && value_as_instance(args[1]) == nullptr) {
        error = "Tuple or struct_time argument required";
      } else if (error.find("object cannot be interpreted as an integer") == std::string::npos) {
        error = "strftime(): illegal time tuple argument";
      }
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  } else {
    const auto timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm = tm_from_time_t(timestamp, false);
  }
  std::string normalized_format;
  if (!normalize_strftime_format(format, normalized_format, error)) {
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::string(format_tm(normalized_format, tm));
  return true;
}

bool time_strptime(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1) {
    error = "_strptime_time() missing 1 required positional argument: 'data_string'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 2) {
    error = "_strptime_time() takes from 1 to 2 positional arguments but " + std::to_string(argc) + " were given";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string text;
  auto* text_string = value_as_string(args[0]);
  if (text_string == nullptr) {
    error = "strptime() argument 0 must be str, not <class '" + time_type_name(args[0]) + "'>";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  text = string_object_to_string(*text_string);
  std::string format = "%a %b %d %H:%M:%S %Y";
  if (argc == 2) {
    auto* format_string = value_as_string(args[1]);
    if (format_string == nullptr) {
      error = "strptime() argument 1 must be str, not <class '" + time_type_name(args[1]) + "'>";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    format = string_object_to_string(*format_string);
  }
  std::tm tm{};
  tm.tm_year = 0;
  tm.tm_mon = 0;
  tm.tm_mday = 1;
  tm.tm_isdst = -1;
  Value zone = Value::none();
  Value gmtoff = Value::none();
  bool explicit_year = false;
  bool unsupported_directive = false;
  char unsupported_directive_char = '\0';
  bool stray_percent = false;
  StrptimeFailureReason failure_reason = StrptimeFailureReason::None;
  if (!validate_strptime_format_directives(format, unsupported_directive, unsupported_directive_char, stray_percent)) {
    if (stray_percent) {
      error = "stray % in format '" + format + "'";
    } else {
      error = std::string("'") + unsupported_directive_char + "' is a bad directive in format '" + format + "'";
    }
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto* state = static_cast<TimeModuleState*>(user_data);
  if (!parse_strptime_directives(text, format, tm, zone, gmtoff, explicit_year, unsupported_directive, unsupported_directive_char, stray_percent, failure_reason, state)) {
    if (stray_percent) {
      error = "stray % in format '" + format + "'";
    } else if (unsupported_directive) {
      error = std::string("'") + unsupported_directive_char + "' is a bad directive in format '" + format + "'";
    } else if (failure_reason == StrptimeFailureReason::IsoWeekWithCalendarYear) {
      error = "ISO week directive '%V' is incompatible with the year directive '%Y'. Use the ISO year '%G' instead.";
    } else if (failure_reason == StrptimeFailureReason::IsoWeekMissingIsoYear) {
      error = "ISO week directive '%V' must be used with the ISO year directive '%G' and a weekday directive ('%A', '%a', '%w', or '%u').";
    } else if (failure_reason == StrptimeFailureReason::IsoYearIncomplete) {
      error = "ISO year directive '%G' must be used with the ISO week directive '%V' and a weekday directive ('%A', '%a', '%w', or '%u').";
    } else if (failure_reason == StrptimeFailureReason::IsoYearWithOrdinalDay) {
      error = "Day of the year directive '%j' is not compatible with ISO year directive '%G'. Use '%Y' instead.";
    } else if (failure_reason == StrptimeFailureReason::InvalidIsoWeek) {
      error = "Invalid week: 53";
    } else if (failure_reason == StrptimeFailureReason::YearOutOfRange) {
      error = "year must be in 1..9999, not 0";
    } else {
      error = "time data does not match format";
    }
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (!valid_strptime_month_day(tm, explicit_year)) {
    error = "day is out of range for month";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = make_struct_time(state->struct_time_class, tm, zone, gmtoff);
  return true;
}

bool time_strptime_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  if (kwargc > 0) {
    error = "strptime() takes no keyword arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  error = "_strptime_time() missing 1 required positional argument: 'data_string'";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool reject_time_keywords(Runtime& runtime, uint32_t kwargc, const char* display_name, std::string& error) {
  if (kwargc == 0) {
    return true;
  }
  error = std::string(display_name) + "() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool time_reject_keywords_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void* user_data) {
  const char* display_name = static_cast<const char*>(user_data);
  return reject_time_keywords(runtime, kwargc, display_name == nullptr ? "time function" : display_name, error);
}

bool time_sleep_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!reject_time_keywords(runtime, kwargc, "time.sleep", error)) {
    return false;
  }
  return time_sleep(runtime, args, argc, out, error, user_data);
}

bool time_localtime_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!reject_time_keywords(runtime, kwargc, "localtime", error)) {
    return false;
  }
  return time_localtime(runtime, args, argc, out, error, user_data);
}

bool time_gmtime_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!reject_time_keywords(runtime, kwargc, "gmtime", error)) {
    return false;
  }
  return time_gmtime(runtime, args, argc, out, error, user_data);
}

bool time_mktime_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!reject_time_keywords(runtime, kwargc, "time.mktime", error)) {
    return false;
  }
  return time_mktime(runtime, args, argc, out, error, user_data);
}

bool time_strftime_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!reject_time_keywords(runtime, kwargc, "strftime", error)) {
    return false;
  }
  return time_strftime(runtime, args, argc, out, error, user_data);
}

bool time_asctime(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "asctime expected at most 1 argument, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::tm tm{};
  if (argc == 0) {
    tm = tm_from_time_t(std::time(nullptr), false);
  } else {
    if (!tm_from_sequence_like(args[0], tm, error)) {
      if (value_as_tuple(args[0]) == nullptr && value_as_list(args[0]) == nullptr && value_as_instance(args[0]) == nullptr) {
        error = "Tuple or struct_time argument required";
      } else if (error.find("object cannot be interpreted as an integer") == std::string::npos) {
        error = "asctime(): illegal time tuple argument";
      }
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  out = Value::string(format_asctime_tm(tm));
  return true;
}

bool time_asctime_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!reject_time_keywords(runtime, kwargc, "asctime", error)) {
    return false;
  }
  return time_asctime(runtime, args, argc, out, error, user_data);
}

bool time_ctime(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  std::time_t timestamp = 0;
  if (!optional_timestamp(runtime, args, argc, "ctime", timestamp, error)) {
    return false;
  }
  const std::tm tm = tm_from_time_t(timestamp, false);
  out = Value::string(format_asctime_tm(tm));
  return true;
}

bool time_ctime_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!reject_time_keywords(runtime, kwargc, "ctime", error)) {
    return false;
  }
  return time_ctime(runtime, args, argc, out, error, user_data);
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
  Value struct_time_getnewargs = runtime.make_native_function("time.struct_time.__getnewargs__", time_struct_time_getnewargs, nullptr, nullptr, nullptr, false, time_struct_time_getnewargs_kw);
  if (auto* native = value_as_native_function(struct_time_getnewargs)) {
    native->attrs_dict = new Value(Value::dict({
        {Value::string("__name__"), Value::string("__getnewargs__")},
        {Value::string("__qualname__"), Value::string("tuple.__getnewargs__")},
        {Value::string("__doc__"), Value::none()},
    }));
  }
  Value struct_time_reduce = runtime.make_native_function("time.struct_time.__reduce__", time_struct_time_reduce, nullptr, nullptr, nullptr, false, time_struct_time_reduce_kw);
  if (auto* native = value_as_native_function(struct_time_reduce)) {
    native->attrs_dict = new Value(Value::dict({
        {Value::string("__name__"), Value::string("__reduce__")},
        {Value::string("__qualname__"), Value::string("struct_time.__reduce__")},
        {Value::string("__doc__"), Value::none()},
    }));
  }
  Value struct_time_reduce_ex = runtime.make_native_function("time.struct_time.__reduce_ex__", time_struct_time_reduce_ex, nullptr, nullptr, nullptr, false, time_struct_time_reduce_ex_kw);
  if (auto* native = value_as_native_function(struct_time_reduce_ex)) {
    native->attrs_dict = new Value(Value::dict({
        {Value::string("__name__"), Value::string("__reduce_ex__")},
        {Value::string("__qualname__"), Value::string("object.__reduce_ex__")},
        {Value::string("__doc__"), Value::string("Helper for pickle.")},
    }));
  }
  state->struct_time_class = Value::class_object(
      "struct_time",
      {
          {"__module__", Value::string("time")},
          {"__qualname__", Value::string("struct_time")},
          {"__doc__", Value::string("The time value as returned by gmtime(), localtime(), and strptime(), and accepted by asctime(), mktime() and strftime().")},
          {"__new__", Value::static_method(time_native_function(runtime, "time.struct_time.__new__", "__new__", time_struct_time_new, "Create a new struct_time object.", nullptr, time_struct_time_new_kw, "struct_time.__new__"))},
          {"__init__", runtime.make_native_function("time.struct_time.__init__", time_struct_time_init, nullptr, nullptr, nullptr, false, time_struct_time_init_kw)},
          {"__repr__", runtime.make_native_function("time.struct_time.__repr__", time_struct_time_repr, nullptr, nullptr, nullptr, false, time_struct_time_repr_kw)},
          {"__getnewargs__", struct_time_getnewargs},
          {"__reduce__", struct_time_reduce},
          {"__reduce_ex__", struct_time_reduce_ex},
          {"count", runtime.make_native_function("time.struct_time.count", time_struct_time_count, nullptr, nullptr, nullptr, false, time_struct_time_count_kw)},
          {"index", runtime.make_native_function("time.struct_time.index", time_struct_time_index, nullptr, nullptr, nullptr, false, time_struct_time_index_kw)},
          {"n_sequence_fields", Value::int64(9)},
          {"n_fields", Value::int64(11)},
          {"n_unnamed_fields", Value::int64(0)},
          {"tm_year", make_member_descriptor("time.struct_time", "tm_year", 0)},
          {"tm_mon", make_member_descriptor("time.struct_time", "tm_mon", 1)},
          {"tm_mday", make_member_descriptor("time.struct_time", "tm_mday", 2)},
          {"tm_hour", make_member_descriptor("time.struct_time", "tm_hour", 3)},
          {"tm_min", make_member_descriptor("time.struct_time", "tm_min", 4)},
          {"tm_sec", make_member_descriptor("time.struct_time", "tm_sec", 5)},
          {"tm_wday", make_member_descriptor("time.struct_time", "tm_wday", 6)},
          {"tm_yday", make_member_descriptor("time.struct_time", "tm_yday", 7)},
          {"tm_isdst", make_member_descriptor("time.struct_time", "tm_isdst", 8)},
          {"tm_zone", make_member_descriptor("time.struct_time", "tm_zone", 9)},
          {"tm_gmtoff", make_member_descriptor("time.struct_time", "tm_gmtoff", 10)},
          {"__match_args__", struct_time_match_args()},
      },
      tuple_base != nullptr ? *tuple_base : Value::invalid());
  runtime.register_native_package_cleanup(state, time_module_state_cleanup);

  const TimezoneInfo timezone = platform_timezone_info();
  state->standard_timezone_name = timezone.standard_name;
  state->daylight_timezone_name = timezone.daylight_name;
  state->has_daylight_timezone = timezone.daylight != 0;

  NativeModuleBuilder builder(runtime, "time");
  builder
      .value("time", time_native_function(
                         runtime, "time.time", "time", time_time,
                         "Return the current time in seconds since the Epoch.", const_cast<char*>("time.time"), time_reject_keywords_kw))
      .value("time_ns", time_native_function(
                            runtime, "time.time_ns", "time_ns", time_time_ns,
                            "Return the current time in nanoseconds since the Epoch.", const_cast<char*>("time.time_ns"), time_reject_keywords_kw))
      .value("monotonic", time_native_function(
                              runtime, "time.monotonic", "monotonic", time_monotonic,
                              "Monotonic clock, cannot go backward.", const_cast<char*>("time.monotonic"), time_reject_keywords_kw))
      .value("monotonic_ns", time_native_function(
                                 runtime, "time.monotonic_ns", "monotonic_ns", time_monotonic_ns,
                                 "Monotonic clock, cannot go backward, as nanoseconds.", const_cast<char*>("time.monotonic_ns"), time_reject_keywords_kw))
      .value("perf_counter", time_native_function(
                                 runtime, "time.perf_counter", "perf_counter", time_perf_counter,
                                 "Performance counter for benchmarking.", const_cast<char*>("time.perf_counter"), time_reject_keywords_kw))
      .value("perf_counter_ns", time_native_function(
                                    runtime, "time.perf_counter_ns", "perf_counter_ns", time_perf_counter_ns,
                                    "Performance counter for benchmarking, as nanoseconds.", const_cast<char*>("time.perf_counter_ns"), time_reject_keywords_kw))
      .value("process_time", time_native_function(
                                 runtime, "time.process_time", "process_time", time_process_time,
                                 "Process time for profiling.", const_cast<char*>("time.process_time"), time_reject_keywords_kw))
      .value("process_time_ns", time_native_function(
                                    runtime, "time.process_time_ns", "process_time_ns", time_process_time_ns,
                                    "Process time for profiling, as nanoseconds.", const_cast<char*>("time.process_time_ns"), time_reject_keywords_kw))
      .value("thread_time", time_native_function(
                                runtime, "time.thread_time", "thread_time", time_thread_time,
                                "Thread time for profiling.", const_cast<char*>("time.thread_time"), time_reject_keywords_kw))
      .value("thread_time_ns", time_native_function(
                                   runtime, "time.thread_time_ns", "thread_time_ns", time_thread_time_ns,
                                   "Thread time for profiling, as nanoseconds.", const_cast<char*>("time.thread_time_ns"), time_reject_keywords_kw))
      .value("get_clock_info", time_native_function(
                                   runtime, "time.get_clock_info", "get_clock_info", time_get_clock_info,
                                   "Get information about the specified clock.", const_cast<char*>("get_clock_info"), time_reject_keywords_kw))
      .value("sleep", time_native_function(
                          runtime, "time.sleep", "sleep", time_sleep,
                          "Delay execution for a given number of seconds.", nullptr, time_sleep_kw))
      .value("localtime", time_native_function(
                              runtime, "time.localtime", "localtime", time_localtime,
                              "Convert seconds since the Epoch to local time.", state, time_localtime_kw))
      .value("gmtime", time_native_function(
                           runtime, "time.gmtime", "gmtime", time_gmtime,
                           "Convert seconds since the Epoch to UTC.", state, time_gmtime_kw))
      .value("mktime", time_native_function(
                           runtime, "time.mktime", "mktime", time_mktime,
                           "Convert a time tuple in local time to seconds since the Epoch.", nullptr, time_mktime_kw))
      .value("strftime", time_native_function(
                             runtime, "time.strftime", "strftime", time_strftime,
                             "Format a time tuple according to a format specification.", nullptr, time_strftime_kw))
      .value("strptime", time_native_function(
                             runtime, "time.strptime", "strptime", time_strptime,
                             "Parse a string to a time tuple according to a format specification.", state, time_strptime_kw))
      .value("asctime", time_native_function(
                            runtime, "time.asctime", "asctime", time_asctime,
                            "Convert a time tuple to a string.", nullptr, time_asctime_kw))
      .value("ctime", time_native_function(
                         runtime, "time.ctime", "ctime", time_ctime,
                         "Convert a time in seconds since the Epoch to a string in local time.", nullptr, time_ctime_kw))
      .value("struct_time", state->struct_time_class)
      .value("_STRUCT_TM_ITEMS", Value::int64(11))
      .value("timezone", Value::int64(timezone.timezone))
      .value("altzone", Value::int64(timezone.altzone))
      .value("daylight", Value::int64(timezone.daylight))
      .value("tzname", Value::tuple({Value::string(timezone.standard_name), Value::string(timezone.daylight_name)}));
  runtime.register_module("time", builder.finish());
}

} // namespace xlang3
