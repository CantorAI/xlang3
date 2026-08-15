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
#include "xlang3/cpp/xlang.h"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: native_package_tests <native-package-dir>\n";
    return 2;
  }

  try {
    X::Runtime runtime;
    runtime.AddImportRoot(argv[1]);

    X::Package sample(runtime, "sample", "x3sample");
    if (sample["answer"].ToInt64() != 42) {
      std::cerr << "bad native module constant\n";
      return 1;
    }

    auto add = sample.fn("add");
    auto result = add(20, 22);
    if (result.ToInt64() != 42) {
      std::cerr << "bad native function result\n";
      return 1;
    }

    X::Package root(runtime, "x3sample");
    if (root["sample"]["answer"].ToInt64() != 42) {
      std::cerr << "bad package root module attr\n";
      return 1;
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
  return 0;
}
