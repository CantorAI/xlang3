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
#include "yaml_convert.h"

#include "xlang3/xlang3.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {

class xlang_yaml {
public:
  X::Value loads(X::Value text) {
    if (!text.IsString()) {
      throw X::Error("yaml.loads() argument must be a string");
    }
    try {
      return X::Value(Host(), xlang3_yaml::yaml_to_value(Host(), Host()->runtime, YAML::Load(text.ToString(false))), false);
    } catch (const std::exception& ex) {
      throw X::Error(std::string("yaml.loads() parse error: ") + ex.what());
    }
  }

  X::Value saves(X::Value value) {
    const char* error = nullptr;
    YAML::Node node;
    if (!xlang3_yaml::value_to_yaml(Host(), Host()->runtime, value.raw(), node, &error)) {
      throw X::Error(error == nullptr ? "yaml.saves() conversion failed" : error);
    }
    YAML::Emitter emitter;
    emitter << node;
    return X::Value::String(Host(), emitter.c_str());
  }

  X::Value load(std::string path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      throw X::Error("yaml.load() cannot open " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loads(X::Value::String(Host(), buffer.str()));
  }

  bool save(X::Value value, std::string path) {
    X::Value text = saves(value);
    std::ofstream file(path, std::ios::binary);
    if (!file) {
      throw X::Error("yaml.save() cannot open " + path);
    }
    file << text.ToString(false);
    return true;
  }

  BEGIN_PACKAGE(xlang_yaml)
    APISET().AddFunc<1>("loads", &xlang_yaml::loads);
    APISET().AddFunc<1>("saves", &xlang_yaml::saves);
    APISET().AddFunc<1>("load", &xlang_yaml::load);
    APISET().AddFunc<2>("save", &xlang_yaml::save);
  END_PACKAGE
};

} // namespace

X3Status register_yaml_module(X3PackageHost* host, X3Value curModule) {
  xlang_yaml::BuildAPI();
  return xlang_yaml::APISET().Create(host, "yaml", curModule);
}
