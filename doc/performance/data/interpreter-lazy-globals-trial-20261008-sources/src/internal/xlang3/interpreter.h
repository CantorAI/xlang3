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
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3 {

struct GeneratorObject;
struct ClassObject;
struct RuntimeDebugPauseState;

struct RuntimeResult {
  Value value;
  Value exception;
  std::vector<std::string> errors;
  bool paused = false;
  RuntimePauseReason pause_reason = RuntimePauseReason::None;
  std::string pause_file;
  uint32_t pause_line = 0;
  uint32_t selected_frame = 0;
  Value pause_frame;
  std::shared_ptr<RuntimeDebugPauseState> pause_state;
};

struct CallArgsView {
  const Value* leading = nullptr;
  uint32_t leading_count = 0;
  const Value* registers = nullptr;
  const std::vector<uint32_t>* register_args = nullptr;
  const std::vector<ir::CallKeywordArg>* keyword_args = nullptr;
  uint32_t star_arg = UINT32_MAX;
  uint32_t kw_star_arg = UINT32_MAX;
  const std::vector<uint32_t>* star_args = nullptr;
  const std::vector<uint32_t>* kw_star_args = nullptr;
  const Value* live_keyword_defaults = nullptr;
  const Value* captured_builtins = nullptr;

  XLANG3_HOT_INLINE CallArgsView with_keyword_defaults(const FunctionObject& function) const {
    CallArgsView view = *this;
    view.live_keyword_defaults = &function.kwdefaults_dict;
    view.captured_builtins = &function.builtins;
    return view;
  }

  XLANG3_HOT_INLINE size_t size() const {
    return static_cast<size_t>(leading_count) + (register_args == nullptr ? 0 : register_args->size());
  }

  XLANG3_HOT_INLINE bool has_keywords() const {
    return keyword_args != nullptr && !keyword_args->empty();
  }

  XLANG3_HOT_INLINE bool has_expansion() const {
    return star_arg != UINT32_MAX ||
           kw_star_arg != UINT32_MAX ||
           (star_args != nullptr && !star_args->empty()) ||
           (kw_star_args != nullptr && !kw_star_args->empty());
  }

  XLANG3_HOT_INLINE const Value& get(size_t index) const {
    if (index < leading_count) {
      return leading[index];
    }
    return registers[(*register_args)[index - leading_count]];
  }
};

// Embedding calls share the VM's exact builtin constructor implementation.
bool runtime_call_builtin_constructor(Runtime& runtime, const ClassObject& klass,
    const Value* args, uint32_t argc,
    const std::vector<std::pair<std::string, Value>>& kwargs,
    bool& handled, Value& out, std::string& error);

// Normal Python callbacks carry their real module/dict namespace. MSVC's
// empty unordered_map still allocates a list sentinel and initial buckets,
// so keep the legacy per-Interpreter fallback absent until its first store.
// Never share this state with another Interpreter or reset it at frame pop:
// direct/SDK IR can reuse the same fallback namespace across activations.
class InterpreterFallbackGlobals {
public:
  const Value* find(const std::string& name) const {
    if (!storage_) return nullptr;
    const auto found = storage_->find(name);
    return found == storage_->end() ? nullptr : &found->second;
  }

  Value& operator[](const std::string& name) {
    if (!storage_) storage_.emplace();
    return (*storage_)[name];
  }

  Value take(const std::string& name) {
    if (!storage_) return Value::invalid();
    const auto found = storage_->find(name);
    if (found == storage_->end()) return Value::invalid();
    // Move the Python owner out before erasing. The caller publishes its new
    // globals version before releasing this owner and allowing finalizers.
    Value removed = std::move(found->second);
    storage_->erase(found);
    return removed;
  }

  bool materialized() const { return storage_.has_value(); }

private:
  std::optional<std::unordered_map<std::string, Value>> storage_;
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
  RuntimeResult run_module(
      const ir::Module& module,
      Value globals_module,
      std::shared_ptr<const ir::Module> module_owner,
      bool register_in_runtime);
  RuntimeResult run_function_value(FunctionObject* function, CallArgsView args);
  RuntimeResult resume_paused(std::shared_ptr<RuntimeDebugPauseState> pause_state);
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
      GeneratorObject* generator,
      std::shared_ptr<RuntimeDebugPauseState> pause_state = nullptr);

  Runtime& runtime_;
  InterpreterFallbackGlobals globals_;
  uint64_t globals_version_ = 0;
};

} // namespace xlang3
