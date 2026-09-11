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

#include "xlang_vm_inline_support.h"

#include "xlang3/generator.h"
#include "xlang3/module_object.h"

namespace xlang3 {

namespace {
thread_local uint32_t g_nested_interpreter_depth = 0;

struct NestedInterpreterGuard {
  NestedInterpreterGuard() { ++g_nested_interpreter_depth; }
  ~NestedInterpreterGuard() { --g_nested_interpreter_depth; }
};
} // namespace

Interpreter::Interpreter(Runtime& runtime) : runtime_(runtime) {}

bool runtime_call_builtin_constructor(Runtime& runtime, const ClassObject& klass,
    const Value* args, uint32_t argc,
    const std::vector<std::pair<std::string, Value>>& kwargs,
    bool& handled, Value& out, std::string& error) {
  handled = false;
  auto constructor = xlang_vm_find_builtin_constructor(klass.name);
  if (constructor != XlangVMBuiltinConstructor::Unknown &&
      !xlang_vm_class_is_builtin_module_class(klass)) {
    constructor = XlangVMBuiltinConstructor::Unknown;
  }
  if (constructor == XlangVMBuiltinConstructor::Unknown) {
    constructor = xlang_vm_find_inherited_builtin_constructor(klass);
  }
  if (constructor == XlangVMBuiltinConstructor::Unknown) {
    return true;
  }
  handled = true;
  CallArgsView view;
  view.leading = args;
  view.leading_count = argc;
  std::vector<Value> keyword_values;
  std::vector<ir::CallKeywordArg> keyword_specs;
  keyword_values.reserve(kwargs.size());
  keyword_specs.reserve(kwargs.size());
  for (const auto& item : kwargs) {
    keyword_specs.push_back({item.first, static_cast<uint32_t>(keyword_values.size())});
    keyword_values.push_back(item.second);
  }
  if (!kwargs.empty()) {
    view.registers = keyword_values.data();
    view.keyword_args = &keyword_specs;
  }
  XlangRuntimeExecutionGuard lock;
  XlangVMBuiltinConstructorError detail;
  if (call_builtin_type_constructor(runtime, klass, view, lock, out, detail)) return true;
  error = detail.message.empty() ? "builtin constructor failed" : detail.message;
  Value pending;
  if (runtime.take_pending_exception(pending)) runtime.set_pending_exception(std::move(pending));
  else runtime.raise_class_error(detail.type, error);
  return false;
}

RuntimeResult Interpreter::run(const ir::Module& module) {
  auto globals_module = Value::module("__main__");
  std::string ignored;
  module_set_attr(globals_module, "__spec__", Value::none(), ignored);
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
  std::string ignored;
  module_set_attr(globals_module, "__spec__", Value::none(), ignored);
  return run_module(*module_owner, std::move(globals_module), module_owner);
}

RuntimeResult Interpreter::run_module(const ir::Module& module, Value globals_module) {
  return run_module(module, std::move(globals_module), nullptr);
}

RuntimeResult Interpreter::run_module(
    const ir::Module& module,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner) {
  return run_module(module, std::move(globals_module), std::move(module_owner), true);
}

RuntimeResult Interpreter::run_module(
    const ir::Module& module,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner,
    bool register_in_runtime) {
  RuntimeResult result;
  // Calls made by native helpers re-enter the interpreter recursively and
  // therefore consume the host C stack.  Keep that path below Windows' stack
  // ceiling even when Python temporarily raises its visible recursion limit.
  constexpr uint32_t kSafeNestedInterpreterLimit = 256;
  const uint32_t effective_recursion_limit = std::min(
      static_cast<uint32_t>(runtime_.recursion_limit()), kSafeNestedInterpreterLimit);
  if (g_nested_interpreter_depth >= effective_recursion_limit) {
    result.exception = runtime_.make_exception(
        "RecursionError", "maximum recursion depth exceeded");
    result.errors.push_back("maximum recursion depth exceeded");
    return result;
  }
  NestedInterpreterGuard nested_guard;
  if (auto* globals = value_as_module(globals_module)) {
    std::string error;
    if (!module_ensure_attr_slots(globals_module, module.global_slots, error)) {
      result.errors.push_back(error);
      return result;
    }
    Value existing;
    auto name = globals->name.empty() ? "__main__" : globals->name;
    if (!module_set_attr(globals_module, "__name__", Value::string(name), error)) {
      result.errors.push_back(error);
      return result;
    }
    const Value module_doc =
        module.entry < module.functions.size() && !module.functions[module.entry].doc.empty()
            ? Value::string(module.functions[module.entry].doc)
            : Value::none();
    if (!module_set_attr(globals_module, "__doc__", module_doc, error)) {
      result.errors.push_back(error);
      return result;
    }
    const bool synthetic_source_file =
        module.source_file.size() >= 2 && module.source_file.front() == '<' &&
        module.source_file.back() == '>';
    if (!module.source_file.empty() && !synthetic_source_file &&
        (!module_get_attr(globals_module, "__file__", existing, error) || existing.tag == ValueTag::Invalid)) {
      error.clear();
      if (!module_set_attr(globals_module, "__file__", Value::string(module.source_file), error)) {
        result.errors.push_back(error);
        return result;
      }
    }
    if (!module_get_attr(globals_module, "__package__", existing, error) || existing.tag == ValueTag::Invalid) {
      error.clear();
      if (!module_set_attr(globals_module, "__package__", Value::string(""), error)) {
        result.errors.push_back(error);
        return result;
      }
    }
    if (!module_get_attr(globals_module, "__annotations__", existing, error) || existing.tag == ValueTag::Invalid) {
      error.clear();
      if (!module_set_attr(globals_module, "__annotations__", Value::dict({}), error)) {
        result.errors.push_back(error);
        return result;
      }
    }
    if (!module_get_attr(globals_module, "__builtins__", existing, error) || existing.tag == ValueTag::Invalid) {
      error.clear();
      Value builtins;
      if (mapping_get_item(
              runtime_.module_registry_dict(), Value::string("builtins"), builtins, error)) {
        if (!module_set_attr(globals_module, "__builtins__", builtins, error)) {
          result.errors.push_back(error);
          return result;
        }
      } else {
        error.clear();
      }
    }
    if (register_in_runtime) {
      runtime_.register_module(name, globals_module);
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
  result = run_function(
      *function->module,
      function->function_id,
      args,
      function->closure,
      function->defaults,
      function->globals_module,
      function->module,
      nullptr);
  if (!result.errors.empty() && result.exception.tag == ValueTag::Invalid) {
    Value pending;
    if (runtime_.take_pending_exception(pending)) {
      value_assign_fast(result.exception, pending);
      runtime_.set_pending_exception(std::move(pending));
    } else if (runtime_.active_exception().tag != ValueTag::Invalid) {
      value_assign_fast(result.exception, runtime_.active_exception());
    }
  }
  return result;
}

RuntimeResult Interpreter::resume_paused(std::shared_ptr<RuntimeDebugPauseState> pause_state) {
  RuntimeResult result;
  if (pause_state == nullptr || pause_state->frame_count == 0 || pause_state->frames.empty()) {
    result.errors.push_back("invalid paused debug state");
    return result;
  }
  const auto& entry = pause_state->frames[0];
  if (entry.module == nullptr || entry.fn == nullptr) {
    result.errors.push_back("invalid paused debug frame");
    return result;
  }
  static const std::vector<Value> empty_closure;
  static const std::vector<Value> empty_defaults;
  return run_function(
      *entry.module,
      entry.function_id,
      {},
      entry.closure == nullptr ? empty_closure : *entry.closure,
      empty_defaults,
      entry.globals_module,
      entry.module_owner,
      nullptr,
      std::move(pause_state));
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
