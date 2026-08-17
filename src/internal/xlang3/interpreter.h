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

#include "xlang3/compiler.h"
#include "xlang3/ir.h"
#include "xlang3/runtime.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3 {

struct GeneratorObject;

struct RuntimeResult {
  Value value;
  std::vector<std::string> errors;
};

struct CallArgsView {
  const Value* leading = nullptr;
  uint32_t leading_count = 0;
  const Value* registers = nullptr;
  const std::vector<uint32_t>* register_args = nullptr;
  const std::vector<ir::CallKeywordArg>* keyword_args = nullptr;
  uint32_t star_arg = UINT32_MAX;
  uint32_t kw_star_arg = UINT32_MAX;

  XLANG3_HOT_INLINE size_t size() const {
    return static_cast<size_t>(leading_count) + (register_args == nullptr ? 0 : register_args->size());
  }

  XLANG3_HOT_INLINE bool has_keywords() const {
    return keyword_args != nullptr && !keyword_args->empty();
  }

  XLANG3_HOT_INLINE bool has_expansion() const {
    return star_arg != UINT32_MAX || kw_star_arg != UINT32_MAX;
  }

  XLANG3_HOT_INLINE const Value& get(size_t index) const {
    if (index < leading_count) {
      return leading[index];
    }
    return registers[(*register_args)[index - leading_count]];
  }
};

class Interpreter {
public:
  explicit Interpreter(Runtime& runtime);
  RuntimeResult run(const ir::Module& module);
  RuntimeResult run(std::shared_ptr<const ir::Module> module);
  RuntimeResult run_module(const ir::Module& module, Value globals_module);
  RuntimeResult run_module(
      const ir::Module& module,
      Value globals_module,
      std::shared_ptr<const ir::Module> module_owner);
  RuntimeResult run_function_value(FunctionObject* function, CallArgsView args);
  RuntimeResult resume_generator(GeneratorObject& generator, Value& out, bool& done);

private:
  RuntimeResult run_function(
      const ir::Module& module,
      uint32_t function_id,
      CallArgsView args,
      const std::vector<Value>& closure,
      const std::vector<Value>& defaults,
      Value globals_module,
      std::shared_ptr<const ir::Module> module_owner,
      GeneratorObject* generator);

  Runtime& runtime_;
  std::unordered_map<std::string, Value> globals_;
  uint64_t globals_version_ = 0;
};

} // namespace xlang3
