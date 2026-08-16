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

namespace {

X::Value get_item(const X::Value& object, const X::Value& key) {
  X3Value result = x3_value_invalid();
  if (x3_get_item(object.runtime(), object.raw(), key.raw(), &result) != X3_STATUS_OK) {
    const char* error = x3_runtime_last_error(object.runtime());
    throw std::runtime_error(error == nullptr ? "x3_get_item failed" : error);
  }
  return X::Value(object.runtime(), result);
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: native_package_real_tests <native-package-dir>\n";
    return 2;
  }

  try {
    X::Runtime runtime;
    runtime.AddImportRoot(argv[1]);

    X::Package json(runtime, "json", "xlang_json");
    auto data = json.fn("loads")("{\"name\":\"xlang3\",\"items\":[1,2,3]}");
    if (get_item(data, X::Value(runtime, "name")).ToString() != "xlang3") {
      std::cerr << "bad json name\n";
      return 1;
    }
    auto items = get_item(data, X::Value(runtime, "items"));
    if (get_item(items, X::Value(runtime, 1)).ToInt64() != 2) {
      std::cerr << "bad json list item\n";
      return 1;
    }

    X::Package sqlite3(runtime, "sqlite3");
    if (sqlite3["OK"].ToInt64() != 0) {
      std::cerr << "bad sqlite fallback import\n";
      return 1;
    }
    auto error = sqlite3["OperationalError"]("sdk sqlite error");
    if (error.ToString() != "sdk sqlite error") {
      std::cerr << "bad sqlite exception construction\n";
      return 1;
    }
    auto conn = sqlite3.fn("connect")(":memory:");
    auto cursor = conn["cursor"]();
    if (cursor.ToString() != "<Cursor object>") {
      std::cerr << "bad bound cursor method call\n";
      return 1;
    }
    cursor["close"]();
    conn["close"]();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
  return 0;
}
