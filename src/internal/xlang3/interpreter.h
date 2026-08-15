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

#include "xlang3/ir.h"
#include "xlang3/runtime.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3 {

struct RuntimeResult {
  Value value;
  std::vector<std::string> errors;
};

class Interpreter {
public:
  explicit Interpreter(Runtime& runtime);
  RuntimeResult run(const ir::Module& module);
  RuntimeResult run_module(const ir::Module& module, Value globals_module);
  RuntimeResult run_module(
      const ir::Module& module,
      Value globals_module,
      std::shared_ptr<const ir::Module> module_owner);

private:
  RuntimeResult run_function(
      const ir::Module& module,
      uint32_t function_id,
      const std::vector<Value>& args,
      const std::vector<Value>& closure,
      Value globals_module,
      std::shared_ptr<const ir::Module> module_owner);

  Runtime& runtime_;
  std::unordered_map<std::string, Value> globals_;
};

} // namespace xlang3
