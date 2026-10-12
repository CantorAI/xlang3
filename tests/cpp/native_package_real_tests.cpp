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
#include "xlang3/xlang3.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <system_error>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: native_package_real_tests <native-package-dir>\n";
    return 2;
  }

  std::string stage = "unicode package staging";
  try {
    const auto source = std::filesystem::u8path(argv[1]);
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      std::filesystem::u8path("xlang3-native-packages-\xe5\xb7\xa5\xe4\xbd\x9c-" + std::to_string(suffix));
    std::filesystem::create_directories(root);
    struct RemoveTree {
      std::filesystem::path path;
      ~RemoveTree() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } remove_tree{root};
    for (const auto& item : std::filesystem::directory_iterator(source)) {
      const auto name = item.path().filename().string();
      if (name.rfind("xlang_json.x3pkg.", 0) == 0 ||
          name.rfind("xlang_sqlite3.x3pkg.", 0) == 0) {
        std::filesystem::copy_file(item.path(), root / item.path().filename(),
                                   std::filesystem::copy_options::overwrite_existing);
      }
    }
    if (!std::filesystem::exists(root / "xlang_json.x3pkg.dll") &&
        !std::filesystem::exists(root / "xlang_json.x3pkg.so") &&
        !std::filesystem::exists(root / "xlang_json.x3pkg.dylib")) {
      throw std::runtime_error("could not stage the native JSON package fixture");
    }
    stage = "UTF-8 import root registration";
    const auto encoded_root = root.u8string();
    const std::string import_root(reinterpret_cast<const char*>(encoded_root.data()), encoded_root.size());

    X::Runtime runtime;
    runtime.AddImportRoot(import_root);

    stage = "native JSON package import";
    X::Module json(runtime, "json", "xlang_json");
    if (json["__xlang3_package__"].ToString() != "xlang_json" ||
        json["__xlang3_abi__"].ToString() != "10") {
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

    stage = "native SQLite package import";
    X::Module sqlite_native(runtime, "_sqlite3");
    if (sqlite_native["__xlang3_package__"].ToString() != "xlang_sqlite3" ||
        sqlite_native["__xlang3_abi__"].ToString() != "10") {
      std::cerr << "bad sqlite package metadata\n";
      return 1;
    }
    X::Module sqlite3(runtime, "sqlite3");
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
    if (cursor.ToString() != "<sqlite3.Cursor object>") {
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

    bool saw_missing_module = false;
    try {
      X::Module missing(runtime, "xlang3_missing_native_package_probe");
    } catch (const std::exception& ex) {
      const std::string message = ex.what();
      saw_missing_module = message.find(
          "No module named 'xlang3_missing_native_package_probe'") !=
          std::string::npos;
    }
    if (!saw_missing_module) {
      std::cerr << "bad missing module error\n";
      return 1;
    }
  } catch (const std::exception& ex) {
    std::cerr << stage << ": " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
