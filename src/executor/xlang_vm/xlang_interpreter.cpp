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
#include "xlang3/interpreter.h"

#include "xlang3/generator.h"
#include "xlang3/module_object.h"

namespace xlang3 {

Interpreter::Interpreter(Runtime& runtime) : runtime_(runtime) {}

RuntimeResult Interpreter::run(const ir::Module& module) {
  auto globals_module = Value::module("__main__");
  return run_module(module, std::move(globals_module), nullptr);
}

RuntimeResult Interpreter::run(std::shared_ptr<const ir::Module> module) {
  RuntimeResult result;
  if (module == nullptr) {
    result.errors.push_back("invalid module");
    return result;
  }
  auto module_owner = std::move(module);
  auto globals_module = Value::module("__main__");
  return run_module(*module_owner, std::move(globals_module), module_owner);
}

RuntimeResult Interpreter::run_module(const ir::Module& module, Value globals_module) {
  return run_module(module, std::move(globals_module), nullptr);
}

RuntimeResult Interpreter::run_module(
    const ir::Module& module,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner) {
  RuntimeResult result;
  if (auto* globals = value_as_module(globals_module)) {
    std::string error;
    Value existing_name;
    if (!module_get_attr(globals_module, "__name__", existing_name, error)) {
      error.clear();
      if (!module_set_attr(globals_module, "__name__", Value::string(globals->name.empty() ? "__main__" : globals->name), error)) {
        result.errors.push_back(error);
        return result;
      }
    }
    if (!module_ensure_attr_slots(globals_module, module.global_slots, error)) {
      result.errors.push_back(error);
      return result;
    }
  }
  static const std::vector<Value> empty_closure;
  static const std::vector<Value> empty_defaults;
  return run_function(
      module,
      module.entry,
      {},
      empty_closure,
      empty_defaults,
      std::move(globals_module),
      std::move(module_owner),
      nullptr);
}

RuntimeResult Interpreter::run_function_value(FunctionObject* function, CallArgsView args) {
  RuntimeResult result;
  if (function == nullptr || function->module == nullptr) {
    result.errors.push_back("function has no module");
    return result;
  }
  return run_function(
      *function->module,
      function->function_id,
      args,
      function->closure,
      function->defaults,
      function->globals_module,
      function->module,
      nullptr);
}

RuntimeResult Interpreter::resume_generator(GeneratorObject& generator, Value& out, bool& done) {
  RuntimeResult result;
  auto* function = value_as_function(generator.function);
  if (function == nullptr || function->module == nullptr) {
    result.errors.push_back("function has no module");
    return result;
  }
  CallArgsView args;
  args.leading = generator.args.data();
  args.leading_count = static_cast<uint32_t>(generator.args.size());
  result = run_function(
      *function->module,
      function->function_id,
      args,
      function->closure,
      function->defaults,
      function->globals_module,
      function->module,
      &generator);
  if (!result.errors.empty()) {
    return result;
  }
  value_assign_fast(out, result.value);
  done = generator.done;
  return result;
}

} // namespace xlang3
