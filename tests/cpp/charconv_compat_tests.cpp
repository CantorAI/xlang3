// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// Licensed under the Apache License, Version 2.0.
#include "xlang3/charconv_compat.h"
#include <cerrno>
#include <clocale>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

int main() {
  struct Case { const char* text; std::errc error; size_t consumed; double value; };
  const Case cases[] = {
      {"1.25", {}, 4, 1.25}, {"-0.0", {}, 4, -0.0},
      {"1e-3", {}, 4, .001}, {"12tail", {}, 2, 12},
      {"1e+", {}, 1, 1}, {"0x1p2", {}, 1, 0}, {"-0x1", {}, 2, -0.0},
      {"+1.25", std::errc::invalid_argument, 0, 0},
      {" 1.25", std::errc::invalid_argument, 0, 0},
      {"\t1.25", std::errc::invalid_argument, 0, 0},
      {"", std::errc::invalid_argument, 0, 0},
      {"1e400", std::errc::result_out_of_range, 5, 0},
      {"1e-400", std::errc::result_out_of_range, 6, 0},
      {"5e-324", {}, 6, std::numeric_limits<double>::denorm_min()},
      {"1.7976931348623157e308", {}, 22, std::numeric_limits<double>::max()},
      {"inf", {}, 3, std::numeric_limits<double>::infinity()},
      {"-inf", {}, 4, -std::numeric_limits<double>::infinity()},
      {"nan", {}, 3, std::numeric_limits<double>::quiet_NaN()},
  };
  const std::string original = std::setlocale(LC_NUMERIC, nullptr);
  for (const char* locale : {"C", "fr_FR.UTF-8", "de_DE.UTF-8", "French_France.1252"}) {
    if (!std::setlocale(LC_NUMERIC, locale)) continue;
    for (const auto& item : cases) {
      const std::string text(item.text);
      double value = 17.5;
      errno = EDOM;
      auto result = xlang3::compat::from_chars_double(text.data(), text.data() + text.size(), value);
      bool correct = result.ec == item.error && size_t(result.ptr - text.data()) == item.consumed;
      if (item.error != std::errc{}) {
#ifdef __APPLE__
        correct = correct && value == 17.5;
#else
        // Preserve the existing platform implementation, including MSVC's
        // assignment of infinity on overflow. Callers reject the error.
        double native = 17.5;
        std::from_chars(text.data(), text.data() + text.size(), native, std::chars_format::general);
        correct = correct && value == native;
#endif
      }
      else if (std::isnan(item.value)) correct = correct && std::isnan(value);
      else correct = correct && value == item.value && (value != 0 || std::signbit(value) == std::signbit(item.value));
#ifdef __APPLE__
      correct = correct && errno == EDOM;
#endif
      if (!correct) {
        std::cerr << "float conversion failed: " << locale << " / " << text
                  << " error=" << int(result.ec) << " consumed=" << (result.ptr - text.data())
                  << " value=" << value << '\n';
        std::setlocale(LC_NUMERIC, original.c_str());
        return 1;
      }
    }
  }
  std::setlocale(LC_NUMERIC, original.c_str());
  std::cout << "Float conversion grammar, locale, and boundaries pass\n";
}
