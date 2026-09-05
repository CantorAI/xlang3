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

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: xlang1_compat_package_tests <old-style-package-dir>\n";
    return 2;
  }

  try {
    X::Runtime runtime;
    runtime.AddImportRoot(argv[1]);

    X::Module package(runtime, "xlang1_compat_sample");

    require(package["add"](20, 22).ToInt64() == 42, "old AddFunc<int,int> call failed");
    require(package["make_list"]()[1].ToString() == "two", "old List return failed");
    require(package["make_dict"]().GetItem("answer").ToInt64() == 42, "old Dict return failed");
    require(package["operator_style"]().ToString() == "ok", "old X::Value operators failed");
    X::Value reference_dict = runtime.Dict();
    X::Value reference_list = runtime.List();
    X::Value returned_dict = package["set_item"](reference_dict, reference_list);
    require(returned_dict == reference_dict, "module Value reference lost dictionary identity");
    require(reference_dict["item"] == reference_list, "module const Value reference lost list identity");
    require(package["read_value"](42).ToInt64() == 42, "const module method reference binding failed");
    X::Value counter = package["Counter"]();
    require(counter["add"](7).ToInt64() == 7, "counter construction failed");
    X::Value returned_list = counter["append_total"](reference_list);
    require(returned_list == reference_list, "class Value reference lost list identity");
    require(reference_list.Size() == 1 && reference_dict["item"][0].ToInt64() == 7,
            "class Value reference mutation did not reach caller");
    require(counter["read_value"](42).ToInt64() == 42, "const class method reference binding failed");
    X::Value list = runtime.List();
    list += X::Value(runtime, 1);
    list += X::Value(runtime, 2);
    require(list[0] == X::Value(runtime, 1), "runtime wrapper list += or == failed");
    require((X::Value(runtime, 20) + X::Value(runtime, 22)).ToInt64() == 42, "runtime wrapper operator+ failed");
    require(package["bytes_size"]("abcdef").ToInt64() == -1, "string must not be treated as binary");
    require(package["value_bytes_roundtrip"]().ToString() == "ok", "X::Value ToBytes/FromBytes roundtrip failed");
    require(package["stream_roundtrip"]().ToString() == "ok", "X::Value stream roundtrip failed");
    const std::string name = package["name"].ToString();
    if (name != "compat") {
      throw std::runtime_error("old property getter failed: got '" + name + "'");
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
  return 0;
}
