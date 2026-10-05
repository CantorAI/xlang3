// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// Licensed under the Apache License, Version 2.0.
#pragma once

#include <charconv>
#ifdef __APPLE__
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <xlocale.h>
#endif

namespace xlang3::compat {
namespace detail {
template <typename Number>
auto parse_double(const char* first, const char* last, Number& number, int)
    -> decltype(std::from_chars(first, last, number, std::chars_format::general)) {
  return std::from_chars(first, last, number, std::chars_format::general);
}

#ifdef __APPLE__
inline std::from_chars_result parse_double(
    const char* first, const char* last, double& number, long) {
  // Unlike strtod, from_chars skips no whitespace and accepts no leading '+'.
  if (first == last || *first == '+' || *first == ' ' || *first == '\t' ||
      *first == '\n' || *first == '\r' || *first == '\f' || *first == '\v')
    return {first, std::errc::invalid_argument};
  const char* digits = first + (*first == '-');
  // General-format from_chars consumes the zero, not a hexadecimal prefix.
  if (last - digits >= 2 && digits[0] == '0' &&
      (digits[1] == 'x' || digits[1] == 'X')) {
    number = first == digits ? 0.0 : -0.0;
    return {digits + 1, std::errc{}};
  }
  const int saved_errno = errno;
  static locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
  if (c_locale == nullptr) {
    errno = saved_errno;
    return {first, std::errc::invalid_argument};
  }
  const std::string token(first, last);
  char* end = nullptr;
  errno = 0;
  const double parsed = strtod_l(token.c_str(), &end, c_locale);
  const int conversion_errno = errno;
  errno = saved_errno;
  if (end == token.c_str()) return {first, std::errc::invalid_argument};
  const char* consumed = first + (end - token.c_str());
  // A representable subnormal can set ERANGE in libc but is valid here.
  if (conversion_errno == ERANGE && (parsed == 0.0 || !std::isfinite(parsed)))
    return {consumed, std::errc::result_out_of_range};
  number = parsed;
  return {consumed, std::errc{}};
}
#endif
} // namespace detail

inline std::from_chars_result from_chars_double(
    const char* first, const char* last, double& number) {
  return detail::parse_double(first, last, number, 0);
}
} // namespace xlang3::compat
