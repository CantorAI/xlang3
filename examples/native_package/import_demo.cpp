// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// SPDX-License-Identifier: Apache-2.0

#include "xlang3/xlang3.h"

#include <exception>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: xlang3_import_demo <demo-directory> [yaml-directory]\n";
    return 2;
  }

  try {
    X::Runtime runtime;
    if (argc == 3) {
      runtime.AddImportRoot(argv[2]);
    }
    runtime.AddImportRoot(argv[1]);
    X::Module demo(runtime, "demo");

    X::Value answer;
    if (!demo.fn("answer").Call({}, answer)) {
      throw std::runtime_error(runtime.LastError());
    }
    std::cout << answer.ToInt64() << '\n';

    X::Value parsed;
    if (!demo.fn("parse_yaml").Call({X::Value(runtime, "answer: 42")}, parsed)) {
      throw std::runtime_error(runtime.LastError());
    }
    std::cout << parsed.ToString() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
