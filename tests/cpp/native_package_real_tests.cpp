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
    std::cerr << "usage: native_package_real_tests <native-package-dir>\n";
    return 2;
  }

  try {
    X::Runtime runtime;
    runtime.AddImportRoot(argv[1]);

    X::Package json(runtime, "json", "xlang_json");
    if (json["__xlang3_package__"].ToString() != "xlang_json" ||
        json["__xlang3_abi__"].ToString() != "8") {
      std::cerr << "bad json package metadata\n";
      return 1;
    }
    auto data = json.fn("loads")("{\"name\":\"xlang3\",\"items\":[1,2,3]}");
    if (data.GetItem("name").ToString() != "xlang3") {
      std::cerr << "bad json name\n";
      return 1;
    }
    auto items = data.GetItem("items");
    if (items.Len() != 3) {
      std::cerr << "bad json list length\n";
      return 1;
    }
    if (items[1].ToInt64() != 2) {
      std::cerr << "bad json list item\n";
      return 1;
    }

    X::Package sqlite3(runtime, "sqlite3");
    if (sqlite3["__xlang3_package__"].ToString() != "xlang_sqlite3" ||
        sqlite3["__xlang3_abi__"].ToString() != "8") {
      std::cerr << "bad sqlite package metadata\n";
      return 1;
    }
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
    cursor["execute"]("CREATE TABLE sdk(value INTEGER)");
    auto params = runtime.List();
    params.Append(5);
    cursor["execute"]("INSERT INTO sdk(value) VALUES (?)", params);
    cursor["execute"]("SELECT value FROM sdk");
    auto row = cursor["fetchone"]();
    if (row[0].ToInt64() != 5) {
      std::cerr << "bad sqlite parameter round trip\n";
      return 1;
    }
    cursor["close"]();
    conn["close"]();

    bool saw_missing_diagnostics = false;
    try {
      X::Package missing(runtime, "xlang3_missing_native_package_probe");
    } catch (const std::exception& ex) {
      const std::string message = ex.what();
      saw_missing_diagnostics =
          message.find("native package candidates tried") != std::string::npos &&
          message.find("xlang3_missing_native_package_probe") != std::string::npos &&
          message.find(".x3pkg") != std::string::npos;
    }
    if (!saw_missing_diagnostics) {
      std::cerr << "bad missing native package diagnostics\n";
      return 1;
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
  return 0;
}
