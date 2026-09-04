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
#include "json_convert.h"

#include "xlang3/xlang3.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {

class xlang_json {
public:
  X::Value loads(X::Value text) {
    if (!text.IsString()) {
      throw X::Error("json.loads() argument must be a string");
    }
    try {
      return X::Value(Host(), xlang3_json::json_to_value(Host(), Host()->runtime, xlang3_json::Json::parse(text.ToString(false))), false);
    } catch (const std::exception& ex) {
      throw X::Error(std::string("json.loads() parse error: ") + ex.what());
    }
  }

  X::Value dumps(X::Value value) {
    const char* error = nullptr;
    xlang3_json::Json json;
    if (!xlang3_json::value_to_json(Host(), Host()->runtime, value.raw(), json, &error)) {
      throw X::Error(error == nullptr ? "json.dumps() conversion failed" : error);
    }
    return X::Value::String(Host(), json.dump());
  }

  X::Value load(std::string path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      throw X::Error("json.load() cannot open " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loads(X::Value::String(Host(), buffer.str()));
  }

  bool save(X::Value value, std::string path) {
    X::Value text = dumps(value);
    std::ofstream file(path, std::ios::binary);
    if (!file) {
      throw X::Error("json.save() cannot open " + path);
    }
    file << text.ToString(false);
    return true;
  }

  BEGIN_PACKAGE(xlang_json)
    APISET().AddFunc<1>("loads", &xlang_json::loads);
    APISET().AddFunc<1>("dumps", &xlang_json::dumps);
    APISET().AddFunc<1>("load", &xlang_json::load);
    APISET().AddFunc<2>("save", &xlang_json::save);
  END_PACKAGE
};

} // namespace

X3Status register_json_module(X3PackageHost* host, X3Value curModule) {
  xlang_json::BuildAPI();
  return xlang_json::APISET().Create(host, "json", curModule);
}
