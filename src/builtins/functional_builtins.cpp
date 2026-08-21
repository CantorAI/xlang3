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
#include "xlang3/builtins.h"

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sequence.h"
#include "xlang3/sema.h"

#include <cctype>
#include <cmath>
#include <memory>
#include <set>
#include <algorithm>
#include <string>
#include <vector>

namespace xlang3 {

namespace {

bool raise_type_error(Runtime& runtime, std::string message, std::string& error) {
  error = std::move(message);
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool value_to_source_text(Runtime& runtime, const Value& value, std::string& out, std::string& error) {
  auto* string = value_as_string(value);
  if (string == nullptr) {
    return raise_type_error(runtime, "source must be str or code object", error);
  }
  out = string_object_to_string(*string);
  return true;
}

bool eval_source_starts_with_statement_keyword(const std::string& source) {
  size_t pos = 0;
  while (pos < source.size() && std::isspace(static_cast<unsigned char>(source[pos])) != 0) {
    ++pos;
  }
  const size_t start = pos;
  while (pos < source.size() &&
         (std::isalpha(static_cast<unsigned char>(source[pos])) != 0 || source[pos] == '_')) {
    ++pos;
  }
  if (pos == start) {
    return false;
  }
  const std::string keyword = source.substr(start, pos - start);
  return keyword == "if" || keyword == "for" || keyword == "while" || keyword == "def" ||
         keyword == "class" || keyword == "try" || keyword == "except" || keyword == "finally" ||
         keyword == "with" || keyword == "import" || keyword == "from" || keyword == "return" ||
         keyword == "raise" || keyword == "pass" || keyword == "break" || keyword == "continue" ||
         keyword == "del" || keyword == "global" || keyword == "nonlocal";
}

bool compile_source_to_code(
    Runtime& runtime,
    const std::string& source,
    const std::string& mode,
    Value& out,
    std::string& error) {
  if (mode == "eval") {
    if (eval_source_starts_with_statement_keyword(source)) {
      error = "invalid syntax";
      runtime.raise_class_error("SyntaxError", error);
      return false;
    }
    auto parsed_expr = parse_expression_source(source);
    if (!parsed_expr.errors.empty()) {
      error = parsed_expr.errors.front();
      runtime.raise_class_error("SyntaxError", error);
      return false;
    }
    ast::Module eval_ast;
    eval_ast.body.push_back(std::make_unique<ast::ReturnStmt>(std::move(parsed_expr.expression)));
    auto lowered = lower_to_ir(eval_ast);
    if (!lowered.errors.empty()) {
      error = lowered.errors.front();
      runtime.raise_class_error("SyntaxError", error);
      return false;
    }
    auto module = std::make_shared<ir::Module>(std::move(lowered.module));
    out = Value::code(module, module->entry, mode);
    return true;
  } else if (mode != "exec" && mode != "single") {
    return raise_type_error(runtime, "compile() mode must be 'exec', 'eval', or 'single'", error);
  }

  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    error = parsed.errors.front();
    runtime.raise_class_error("SyntaxError", error);
    return false;
  }
  auto lowered = lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    error = lowered.errors.front();
    runtime.raise_class_error("SyntaxError", error);
    return false;
  }
  auto module = std::make_shared<ir::Module>(std::move(lowered.module));
  out = Value::code(module, module->entry, mode == "single" ? "exec" : mode);
  return true;
}

bool run_code_object(Runtime& runtime, CodeObject& code, Value globals_module, Value& out, std::string& error) {
  if (code.module == nullptr || code.function_id >= code.module->functions.size()) {
    error = "invalid code object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  Interpreter interpreter(runtime);
  Value target_globals = std::move(globals_module);
  RuntimeResult result = interpreter.run_module(*code.module, target_globals, code.module);
  if (!result.errors.empty()) {
    error = result.errors.front();
    return false;
  }
  if (code.mode == "eval") {
    value_assign_fast(out, result.value);
    return true;
  }
  value_set_none(out);
  return true;
}

bool collect_iterable(Runtime& runtime, const Value& iterable, std::vector<Value>& out, std::string& error) {
  Value iterator;
  if (!sequence_get_iter(iterable, iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      return true;
    }
    out.push_back(std::move(item));
  }
}

bool builtin_identity(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "_identity expected 1 argument, got " + std::to_string(argc);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool builtin_callable(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "callable() expected 1 argument";
    return false;
  }
  out = Value::boolean(
      value_as_function(args[0]) != nullptr ||
      value_as_native_function(args[0]) != nullptr ||
      value_as_bound_method(args[0]) != nullptr ||
      value_as_class(args[0]) != nullptr);
  return true;
}

bool builtin_enumerate(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    return raise_type_error(runtime, "enumerate() expected 1 or 2 arguments", error);
  }
  int64_t index = 0;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      return raise_type_error(runtime, "enumerate() start must be int", error);
    }
    index = args[1].as.i64;
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = functional_enumerate_iterator(std::move(iterator), index);
  return true;
}

bool builtin_map(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2) {
    return raise_type_error(runtime, "map() expected at least 2 arguments", error);
  }
  std::vector<Value> iterators;
  iterators.reserve(argc - 1);
  for (uint32_t i = 1; i < argc; ++i) {
    Value iterator;
    if (!sequence_get_iter(args[i], iterator, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    iterators.push_back(std::move(iterator));
  }
  out = functional_map_iterator(&runtime, args[0], std::move(iterators));
  return true;
}

bool builtin_filter(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    return raise_type_error(runtime, "filter() expected 2 arguments", error);
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = functional_filter_iterator(&runtime, args[0], std::move(iterator));
  return true;
}

bool builtin_zip(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc == 0) {
    out = functional_zip_iterator({});
    return true;
  }
  std::vector<Value> iterators;
  iterators.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    Value iterator;
    if (!sequence_get_iter(args[i], iterator, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    iterators.push_back(std::move(iterator));
  }
  out = functional_zip_iterator(std::move(iterators));
  return true;
}

bool builtin_sum(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    return raise_type_error(runtime, "sum() expected 1 or 2 arguments", error);
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value total = argc == 2 ? args[1] : Value::int64(0);
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      value_assign_fast(out, total);
      return true;
    }
    Value next_total;
    if (!value_add(total, item, next_total, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    total = std::move(next_total);
  }
}

bool minmax_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    bool want_min) {
  if (argc == 0) {
    return raise_type_error(runtime, want_min ? "min() expected at least 1 argument" : "max() expected at least 1 argument", error);
  }
  std::vector<Value> values;
  if (argc == 1) {
    if (!collect_iterable(runtime, args[0], values, error)) {
      return false;
    }
  } else {
    values.reserve(argc);
    for (uint32_t i = 0; i < argc; ++i) {
      values.push_back(args[i]);
    }
  }
  if (values.empty()) {
    runtime.raise_class_error("ValueError", want_min ? "min() arg is an empty sequence" : "max() arg is an empty sequence");
    error = want_min ? "min() arg is an empty sequence" : "max() arg is an empty sequence";
    return false;
  }
  size_t best = 0;
  for (size_t i = 1; i < values.size(); ++i) {
    Value comparison;
    if (!value_compare(want_min ? "<" : ">", values[i], values[best], comparison, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (value_truthy(comparison)) {
      best = i;
    }
  }
  value_assign_fast(out, values[best]);
  return true;
}

bool builtin_min(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return minmax_common(runtime, args, argc, out, error, true);
}

bool builtin_max(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return minmax_common(runtime, args, argc, out, error, false);
}

bool builtin_abs(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "abs() expected 1 argument", error);
  }
  if (args[0].tag == ValueTag::Int64) {
    out = Value::int64(args[0].as.i64 < 0 ? -args[0].as.i64 : args[0].as.i64);
    return true;
  }
  if (args[0].tag == ValueTag::Double) {
    out = Value::number(std::fabs(args[0].as.f64));
    return true;
  }
  return raise_type_error(runtime, "bad operand type for abs()", error);
}

bool builtin_round(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    return raise_type_error(runtime, "round() expected 1 or 2 arguments", error);
  }
  if (args[0].tag != ValueTag::Int64 && args[0].tag != ValueTag::Double) {
    return raise_type_error(runtime, "type does not define __round__ method", error);
  }
  int64_t digits = 0;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      return raise_type_error(runtime, "round() ndigits must be int", error);
    }
    digits = args[1].as.i64;
  }
  if (args[0].tag == ValueTag::Int64) {
    out = args[0];
    return true;
  }
  if (argc == 1) {
    out = Value::int64(static_cast<int64_t>(std::nearbyint(args[0].as.f64)));
    return true;
  }
  const double factor = std::pow(10.0, static_cast<double>(digits));
  out = Value::number(std::nearbyint(args[0].as.f64 * factor) / factor);
  return true;
}

bool builtin_getattr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    return raise_type_error(runtime, "getattr() expected 2 or 3 arguments", error);
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    return raise_type_error(runtime, "getattr(): attribute name must be string", error);
  }
  std::string attr_error;
  if (attribute_get(args[0], string_object_to_string(*name), out, attr_error)) {
    return true;
  }
  if (argc == 3) {
    value_assign_fast(out, args[2]);
    return true;
  }
  error = std::move(attr_error);
  runtime.raise_class_error("AttributeError", error);
  return false;
}

bool builtin_setattr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 3) {
    return raise_type_error(runtime, "setattr() expected 3 arguments", error);
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    return raise_type_error(runtime, "setattr(): attribute name must be string", error);
  }
  Value target = args[0];
  if (!attribute_set(target, string_object_to_string(*name), args[2], error)) {
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_hasattr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    return raise_type_error(runtime, "hasattr() expected 2 arguments", error);
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    return raise_type_error(runtime, "hasattr(): attribute name must be string", error);
  }
  Value ignored;
  std::string attr_error;
  out = Value::boolean(attribute_get(args[0], string_object_to_string(*name), ignored, attr_error));
  return true;
}

Value names_to_list(const std::set<std::string>& names) {
  std::vector<Value> values;
  values.reserve(names.size());
  for (const auto& name : names) {
    values.push_back(Value::string(name));
  }
  return Value::list(std::move(values));
}

Value module_attrs_to_dict(const ModuleObject& module) {
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(module.name_to_slot.size());
  for (const auto& entry : module.name_to_slot) {
    if (entry.second < module.slots.size()) {
      entries.push_back({Value::string(entry.first), module.slots[entry.second]});
    }
  }
  return Value::dict(std::move(entries));
}

bool builtin_dir(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "dir() expected 1 argument", error);
  }
  std::set<std::string> names;
  if (auto* module = value_as_module(args[0])) {
    for (const auto& entry : module->name_to_slot) {
      names.insert(entry.first);
    }
    out = names_to_list(names);
    return true;
  }
  if (auto* klass = value_as_class(args[0])) {
    names.insert("__bases__");
    names.insert("__mro__");
    names.insert("__name__");
    for (const auto& attr : klass->attrs) {
      names.insert(attr.first);
    }
    for (const auto& slot : klass->instance_slot_names) {
      names.insert(slot);
    }
    out = names_to_list(names);
    return true;
  }
  if (auto* instance = value_as_instance(args[0])) {
    names.insert("__class__");
    for (const auto& attr : instance->attrs) {
      names.insert(attr.first);
    }
    if (auto* klass = value_as_class(instance->klass)) {
      for (const auto& attr : klass->attrs) {
        names.insert(attr.first);
      }
      for (const auto& slot : klass->instance_slot_names) {
        names.insert(slot);
      }
    }
    out = names_to_list(names);
    return true;
  }
  out = Value::list({});
  return true;
}

bool builtin_vars(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "vars() expected 1 argument", error);
  }
  std::vector<std::pair<Value, Value>> entries;
  if (auto* module = value_as_module(args[0])) {
    out = module_attrs_to_dict(*module);
    return true;
  }
  if (auto* klass = value_as_class(args[0])) {
    entries.reserve(klass->attrs.size());
    for (const auto& attr : klass->attrs) {
      entries.push_back({Value::string(attr.first), attr.second});
    }
    out = Value::dict(std::move(entries));
    return true;
  }
  if (auto* instance = value_as_instance(args[0])) {
    entries.reserve(instance->attrs.size());
    for (const auto& attr : instance->attrs) {
      entries.push_back({Value::string(attr.first), attr.second});
    }
    out = Value::dict(std::move(entries));
    return true;
  }
  error = "vars() argument must have __dict__ attribute";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool builtin_globals(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    return raise_type_error(runtime, "globals() expected no arguments", error);
  }
  auto* module = value_as_module(runtime.current_globals_module());
  if (module == nullptr) {
    error = "globals() has no active module";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  out = module_attrs_to_dict(*module);
  return true;
}

bool builtin_locals(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    return raise_type_error(runtime, "locals() expected no arguments", error);
  }
  out = runtime.current_locals_snapshot();
  return true;
}

bool builtin_compile(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 3) {
    return raise_type_error(runtime, "compile() expected at least 3 arguments", error);
  }
  std::string source;
  if (!value_to_source_text(runtime, args[0], source, error)) {
    return false;
  }
  auto* mode = value_as_string(args[2]);
  if (mode == nullptr) {
    return raise_type_error(runtime, "compile() mode must be str", error);
  }
  return compile_source_to_code(runtime, source, string_object_to_string(*mode), out, error);
}

bool builtin_eval(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3) {
    return raise_type_error(runtime, "eval() expected 1 to 3 arguments", error);
  }
  if (argc >= 2 && value_as_module(args[1]) == nullptr && args[1].tag != ValueTag::None) {
    return raise_type_error(runtime, "eval() globals must be a module in this XLang3 phase", error);
  }
  if (argc == 3 && args[2].tag != ValueTag::None) {
    return raise_type_error(runtime, "eval() explicit locals are not supported yet", error);
  }

  Value code_value;
  if (auto* code = value_as_code(args[0])) {
    (void)code;
    value_assign_fast(code_value, args[0]);
  } else {
    std::string source;
    if (!value_to_source_text(runtime, args[0], source, error)) {
      return false;
    }
    if (!compile_source_to_code(runtime, source, "eval", code_value, error)) {
      return false;
    }
  }

  Value globals_module = argc >= 2 && value_as_module(args[1]) != nullptr ? args[1] : runtime.current_globals_module();
  if (value_as_module(globals_module) == nullptr) {
    error = "eval() has no active globals module";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  auto* code = value_as_code(code_value);
  if (code == nullptr) {
    return raise_type_error(runtime, "eval() expected str or code object", error);
  }
  if (code->mode != "eval") {
    return raise_type_error(runtime, "eval() code object must be compiled with mode 'eval'", error);
  }
  return run_code_object(runtime, *code, std::move(globals_module), out, error);
}

bool builtin_exec(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3) {
    return raise_type_error(runtime, "exec() expected 1 to 3 arguments", error);
  }
  if (argc >= 2 && value_as_module(args[1]) == nullptr && args[1].tag != ValueTag::None) {
    return raise_type_error(runtime, "exec() globals must be a module in this XLang3 phase", error);
  }
  if (argc == 3 && args[2].tag != ValueTag::None) {
    return raise_type_error(runtime, "exec() explicit locals are not supported yet", error);
  }

  Value code_value;
  if (auto* code = value_as_code(args[0])) {
    (void)code;
    value_assign_fast(code_value, args[0]);
  } else {
    std::string source;
    if (!value_to_source_text(runtime, args[0], source, error)) {
      return false;
    }
    if (!compile_source_to_code(runtime, source, "exec", code_value, error)) {
      return false;
    }
  }

  Value globals_module = argc >= 2 && value_as_module(args[1]) != nullptr ? args[1] : runtime.current_globals_module();
  if (value_as_module(globals_module) == nullptr) {
    error = "exec() has no active globals module";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  auto* code = value_as_code(code_value);
  if (code == nullptr) {
    return raise_type_error(runtime, "exec() expected str or code object", error);
  }
  if (code->mode == "eval") {
    return raise_type_error(runtime, "exec() code object must not be compiled with mode 'eval'", error);
  }
  return run_code_object(runtime, *code, std::move(globals_module), out, error);
}

} // namespace

void register_functional_builtins(Runtime& runtime) {
  runtime.register_native_builtin("_identity", builtin_identity);
  runtime.register_native_builtin("callable", builtin_callable);
  runtime.register_native_builtin("enumerate", builtin_enumerate);
  runtime.register_native_builtin("zip", builtin_zip);
  runtime.register_native_builtin("map", builtin_map);
  runtime.register_native_builtin("filter", builtin_filter);
  runtime.register_native_builtin("sum", builtin_sum);
  runtime.register_native_builtin("min", builtin_min);
  runtime.register_native_builtin("max", builtin_max);
  runtime.register_native_builtin("abs", builtin_abs);
  runtime.register_native_builtin("round", builtin_round);
  runtime.register_native_builtin("getattr", builtin_getattr);
  runtime.register_native_builtin("setattr", builtin_setattr);
  runtime.register_native_builtin("hasattr", builtin_hasattr);
  runtime.register_native_builtin("dir", builtin_dir);
  runtime.register_native_builtin("vars", builtin_vars);
  runtime.register_native_builtin("globals", builtin_globals);
  runtime.register_native_builtin("locals", builtin_locals);
  runtime.register_native_builtin("compile", builtin_compile);
  runtime.register_native_builtin("eval", builtin_eval);
  runtime.register_native_builtin("exec", builtin_exec);
}

} // namespace xlang3
