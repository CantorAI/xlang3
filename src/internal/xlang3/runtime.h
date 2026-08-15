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
#pragma once

#include "xlang3/value.h"

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3 {

class Runtime {
public:
  explicit Runtime(std::ostream& out);

  std::ostream& out() { return out_; }

  void register_builtin(std::string name, Value value);
  void register_native_builtin(std::string name, NativeFunctionCallback callback);
  const Value* find_builtin(const std::string& name) const;
  Value make_native_function(std::string name, NativeFunctionCallback callback);
  void register_module(std::string name, Value module);
  bool import_module(const std::string& name, Value& out, std::string& error);

private:
  std::ostream& out_;
  uint32_t next_native_id_ = 1;
  std::unordered_map<std::string, Value> builtins_;
  std::unordered_map<std::string, Value> modules_;
};

} // namespace xlang3
