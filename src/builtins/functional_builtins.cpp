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
#include "xlang3/mapping.h"
#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sequence.h"
#include "xlang3/sema.h"

#include <cctype>
#include <cmath>
#include <cstdio>
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

bool builtin_import(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 5) {
    error = "__import__() expected name, globals, locals, fromlist, level";
    return false;
  }
  std::string name;
  auto* name_string = value_as_string(args[0]);
  if (name_string == nullptr) {
    return raise_type_error(runtime, "__import__() name must be str", error);
  }
  name = string_object_to_string(*name_string);
  Value module;
  if (!runtime.import_module(name, module, error)) {
    return false;
  }

  bool has_fromlist = false;
  if (argc >= 4 && args[3].tag != ValueTag::None) {
    if (auto* list = value_as_list(args[3])) {
      has_fromlist = !list->items.empty();
    } else if (auto* tuple = value_as_tuple(args[3])) {
      has_fromlist = !tuple->items.empty();
    } else {
      has_fromlist = value_truthy(args[3]);
    }
  }
  if (!has_fromlist) {
    const auto dot = name.find('.');
    if (dot != std::string::npos) {
      Value top;
      if (runtime.import_module(name.substr(0, dot), top, error)) {
        value_assign_fast(out, top);
        return true;
      }
    }
  }
  value_assign_fast(out, module);
  return true;
}

bool builtin_import_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc > 5) {
    error = "__import__() expected name, globals, locals, fromlist, level";
    return false;
  }

  std::vector<Value> positional;
  positional.reserve(5);
  for (uint32_t i = 0; i < argc; ++i) {
    positional.push_back(args[i]);
  }

  auto set_arg = [&](uint32_t index, const Value& value, const char* name) -> bool {
    while (positional.size() < index) {
      positional.push_back(Value::none());
    }
    if (positional.size() > index) {
      error = std::string("__import__() got multiple values for argument '") + name + "'";
      return false;
    }
    positional.push_back(value);
    return true;
  };

  for (uint32_t i = 0; i < kwargc; ++i) {
    const char* name = kwargs[i].name;
    const Value* value = kwargs[i].value;
    if (name == nullptr || value == nullptr) {
      error = "__import__() keyword argument is invalid";
      return false;
    }
    const std::string key(name);
    if (key == "name") {
      if (!set_arg(0, *value, "name")) return false;
    } else if (key == "globals") {
      if (!set_arg(1, *value, "globals")) return false;
    } else if (key == "locals") {
      if (!set_arg(2, *value, "locals")) return false;
    } else if (key == "fromlist") {
      if (!set_arg(3, *value, "fromlist")) return false;
    } else if (key == "level") {
      if (!set_arg(4, *value, "level")) return false;
    } else {
      error = "__import__() got unsupported keyword argument '" + key + "'";
      return false;
    }
  }

  if (positional.empty()) {
    error = "__import__() missing required argument 'name'";
    return false;
  }
  return builtin_import(
      runtime,
      positional.data(),
      static_cast<uint32_t>(positional.size()),
      out,
      error,
      user_data);
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
    const std::string& filename,
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
    module->source_file = filename;
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
  module->source_file = filename;
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

bool copy_dict_to_module(const Value& dict_value, Value& module, std::string& error) {
  auto* dict = value_as_dict(dict_value);
  if (dict == nullptr) {
    return false;
  }
  for (const auto& entry : dict->entries) {
    auto* key = value_as_string(entry.first);
    if (key == nullptr) {
      continue;
    }
    if (!module_set_attr(module, string_object_to_string(*key), entry.second, error)) {
      return false;
    }
  }
  return true;
}

bool eval_globals_from_args(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error) {
  if (argc >= 2 && value_as_module(args[1]) != nullptr) {
    value_assign_fast(out, args[1]);
    return true;
  }
  if (argc < 2 || args[1].tag == ValueTag::None) {
    out = runtime.current_globals_module();
    return value_as_module(out) != nullptr;
  }
  if (value_as_dict(args[1]) == nullptr) {
    return false;
  }

  out = Value::module("<eval>");
  module_set_attr(out, "__name__", Value::string("<eval>"), error);
  if (!copy_dict_to_module(args[1], out, error)) {
    return false;
  }
  if (argc >= 3 && args[2].tag != ValueTag::None) {
    if (value_as_dict(args[2]) == nullptr) {
      return false;
    }
    if (!copy_dict_to_module(args[2], out, error)) {
      return false;
    }
  }
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

bool builtin_classmethod(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "classmethod() expected 1 argument", error);
  }
  if (!value_truthy(Value::boolean(
          value_as_function(args[0]) != nullptr ||
          value_as_native_function(args[0]) != nullptr ||
          value_as_bound_method(args[0]) != nullptr))) {
    return raise_type_error(runtime, "classmethod() argument must be callable", error);
  }
  out = Value::class_method(args[0]);
  return true;
}

bool builtin_staticmethod(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "staticmethod() expected 1 argument", error);
  }
  if (!value_truthy(Value::boolean(
          value_as_function(args[0]) != nullptr ||
          value_as_native_function(args[0]) != nullptr ||
          value_as_bound_method(args[0]) != nullptr))) {
    return raise_type_error(runtime, "staticmethod() argument must be callable", error);
  }
  out = Value::static_method(args[0]);
  return true;
}

bool infer_super_defining_class(Runtime& runtime, const Value& self, Value& out, std::string& error) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr) {
    error = "super(): current self is not an instance";
    return false;
  }
  Value mro_value;
  if (!object_get_attr(instance->klass, "__mro__", mro_value, error)) {
    return false;
  }
  auto* mro = value_as_tuple(mro_value);
  if (mro == nullptr) {
    error = "super(): invalid method resolution order";
    return false;
  }
  const uint32_t current_function_id = runtime.current_frame_function_id();
  const auto* current_module_owner = runtime.current_frame_module_owner();
  Value defining_class;
  for (const auto& class_value : mro->items) {
    auto* klass = value_as_class(class_value);
    if (klass == nullptr) {
      continue;
    }
    for (const auto& attr : klass->attrs) {
      Value function_value;
      if (auto* method = value_as_static_method(attr.second)) {
        value_assign_fast(function_value, method->function);
      } else if (auto* method = value_as_class_method(attr.second)) {
        value_assign_fast(function_value, method->function);
      } else {
        value_assign_fast(function_value, attr.second);
      }
      auto* function = value_as_function(function_value);
      const bool same_module =
          function != nullptr &&
          current_module_owner != nullptr &&
          function->module != nullptr &&
          function->module.get() == current_module_owner->get();
      if (same_module && function->function_id == current_function_id) {
        value_assign_fast(defining_class, class_value);
      }
    }
  }
  if (defining_class.tag != ValueTag::Invalid) {
    value_assign_fast(out, defining_class);
    return true;
  }
  value_assign_fast(out, instance->klass);
  return true;
}

bool builtin_super(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0 && argc != 2) {
    return raise_type_error(runtime, "super() expected 0 or 2 arguments", error);
  }

  Value klass;
  Value self;
  if (argc == 2) {
    if (value_as_class(args[0]) == nullptr) {
      return raise_type_error(runtime, "super() first argument must be type", error);
    }
    value_assign_fast(klass, args[0]);
    value_assign_fast(self, args[1]);
  } else {
    Value locals = runtime.current_locals_snapshot();
    if (!mapping_get_item(locals, Value::string("self"), self, error)) {
      return raise_type_error(runtime, "super(): no current instance", error);
    }
    if (!infer_super_defining_class(runtime, self, klass, error)) {
      std::string message = error;
      return raise_type_error(runtime, std::move(message), error);
    }
  }

  out = Value::super_object(std::move(klass), std::move(self));
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

bool builtin_sorted(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "sorted() expected 1 argument", error);
  }
  std::vector<Value> values;
  if (!collect_iterable(runtime, args[0], values, error)) {
    return false;
  }
  std::sort(values.begin(), values.end(), [&](const Value& lhs, const Value& rhs) {
    Value less;
    std::string compare_error;
    if (!value_compare("<", lhs, rhs, less, compare_error)) {
      return false;
    }
    return value_truthy(less);
  });
  out = Value::list(std::move(values));
  return true;
}

bool builtin_sorted_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  bool reverse = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      return raise_type_error(runtime, "sorted() received invalid keyword argument", error);
    }
    if (name == "reverse") {
      reverse = value_truthy(*kwargs[i].value);
    } else if (name == "key") {
      // Accepted for CPython call shape. Generic native-to-Python callback keys
      // are handled by the VM call path in a later compatibility pass.
    } else {
      return raise_type_error(runtime, "sorted() got an unexpected keyword argument '" + name + "'", error);
    }
  }
  if (!builtin_sorted(runtime, args, argc, out, error, nullptr)) {
    return false;
  }
  if (reverse) {
    if (auto* list = value_as_list(out)) {
      std::reverse(list->items.begin(), list->items.end());
    }
  }
  return true;
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
  if (argc == 0) {
    out = Value::list({});
    return true;
  }
  if (argc != 1) {
    return raise_type_error(runtime, "dir() expected at most 1 argument", error);
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
  auto* filename = value_as_string(args[1]);
  const std::string filename_text = filename == nullptr ? "<string>" : string_object_to_string(*filename);
  return compile_source_to_code(runtime, source, filename_text, string_object_to_string(*mode), out, error);
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
  Value code_value;
  if (auto* code = value_as_code(args[0])) {
    (void)code;
    value_assign_fast(code_value, args[0]);
  } else {
    std::string source;
    if (!value_to_source_text(runtime, args[0], source, error)) {
      return false;
    }
    if (!compile_source_to_code(runtime, source, "<string>", "eval", code_value, error)) {
      return false;
    }
  }

  Value globals_module;
  if (!eval_globals_from_args(runtime, args, argc, globals_module, error)) {
    return raise_type_error(runtime, "eval() globals and locals must be dict or module", error);
  }
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
    if (!compile_source_to_code(runtime, source, "<string>", "exec", code_value, error)) {
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

bool builtin_repr(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "repr() expected 1 argument";
    return false;
  }
  if (auto* string = value_as_string(args[0])) {
    std::string text;
    text.push_back('\'');
    for (char ch : string_object_view(*string)) {
      if (ch == '\'' || ch == '\\') {
        text.push_back('\\');
      }
      if (ch == '\n') {
        text += "\\n";
      } else if (ch == '\r') {
        text += "\\r";
      } else if (ch == '\t') {
        text += "\\t";
      } else {
        text.push_back(ch);
      }
    }
    text.push_back('\'');
    out = Value::string(std::move(text));
    return true;
  }
  out = Value::string(value_to_string(args[0]));
  return true;
}

bool builtin_format(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    error = "format() expected 1 or 2 arguments";
    return false;
  }
  std::string spec;
  if (argc == 2) {
    auto* spec_object = value_as_string(args[1]);
    if (spec_object == nullptr) {
      error = "format() argument 2 must be str";
      return false;
    }
    spec = string_object_to_string(*spec_object);
  }
  if (spec.empty()) {
    out = Value::string(value_to_string(args[0]));
    return true;
  }

  if (args[0].tag == ValueTag::Int64) {
    bool zero_pad = false;
    size_t i = 0;
    if (i < spec.size() && spec[i] == '0') {
      zero_pad = true;
      ++i;
    }
    int width = 0;
    while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
      width = width * 10 + (spec[i++] - '0');
    }
    if (i < spec.size() && spec[i] == 'd') {
      ++i;
    }
    if (i == spec.size()) {
      std::string text = value_to_string(args[0]);
      const bool negative = !text.empty() && text[0] == '-';
      const size_t sign = negative ? 1 : 0;
      if (width > static_cast<int>(text.size())) {
        const auto pad_count = static_cast<size_t>(width) - text.size();
        if (zero_pad && negative) {
          text = "-" + std::string(pad_count, '0') + text.substr(sign);
        } else {
          text = std::string(pad_count, zero_pad ? '0' : ' ') + text;
        }
      }
      out = Value::string(std::move(text));
      return true;
    }
  }

  if (args[0].tag == ValueTag::Double && spec.size() >= 3 && spec[0] == '.' && spec.back() == 'f') {
    int precision = 0;
    bool valid = true;
    for (size_t i = 1; i + 1 < spec.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(spec[i]))) {
        valid = false;
        break;
      }
      precision = precision * 10 + (spec[i] - '0');
    }
    if (valid && precision >= 0 && precision <= 32) {
      char buffer[128];
      std::snprintf(buffer, sizeof(buffer), "%.*f", precision, args[0].as.f64);
      out = Value::string(buffer);
      return true;
    }
  }

  error = "unsupported format specifier";
  return false;
}

bool builtin_all(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "all() expected 1 argument", error);
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
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
      out = Value::boolean(true);
      return true;
    }
    if (!value_truthy(item)) {
      out = Value::boolean(false);
      return true;
    }
  }
}

bool builtin_any(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "any() expected 1 argument", error);
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
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
      out = Value::boolean(false);
      return true;
    }
    if (value_truthy(item)) {
      out = Value::boolean(true);
      return true;
    }
  }
}

} // namespace

void register_functional_builtins(Runtime& runtime) {
  runtime.register_native_builtin("_identity", builtin_identity);
  runtime.register_native_builtin("super", builtin_super);
  runtime.register_native_builtin("callable", builtin_callable);
  runtime.register_native_builtin("enumerate", builtin_enumerate);
  runtime.register_native_builtin("zip", builtin_zip);
  runtime.register_native_builtin("map", builtin_map);
  runtime.register_native_builtin("filter", builtin_filter);
  runtime.register_native_builtin("sum", builtin_sum);
  runtime.register_native_builtin("sorted", builtin_sorted, nullptr, false, builtin_sorted_kw);
  runtime.register_native_builtin("min", builtin_min);
  runtime.register_native_builtin("max", builtin_max);
  runtime.register_native_builtin("abs", builtin_abs);
  runtime.register_native_builtin("round", builtin_round);
  runtime.register_native_builtin("repr", builtin_repr);
  runtime.register_native_builtin("format", builtin_format);
  runtime.register_native_builtin("all", builtin_all);
  runtime.register_native_builtin("any", builtin_any);
  runtime.register_native_builtin("__import__", builtin_import, nullptr, false, builtin_import_kw);
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
