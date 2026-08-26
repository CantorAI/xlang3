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
#include "xlang3/value_hash.h"

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
    Value iter_method;
    std::string attr_error;
    if (!attribute_get(iterable, "__iter__", iter_method, attr_error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    Value iter_result;
    std::string call_error;
    if (!runtime_call_callable(runtime, iter_method, nullptr, 0, iter_result, call_error)) {
      runtime.raise_class_error("TypeError", call_error);
      error = call_error;
      return false;
    }
    std::string concrete_error;
    if (sequence_get_iter(iter_result, iterator, concrete_error)) {
      error.clear();
    } else {
      iterator = functional_protocol_iterator(&runtime, std::move(iter_result));
      error.clear();
    }
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

struct SortEntry {
  Value key;
  Value value;
};

bool collect_sorted_entries(
    Runtime& runtime,
    const Value& iterable,
    const Value* key_callable,
    std::vector<SortEntry>& entries,
    std::string& error) {
  std::vector<Value> values;
  if (!collect_iterable(runtime, iterable, values, error)) {
    return false;
  }
  entries.clear();
  entries.reserve(values.size());
  for (const auto& value : values) {
    Value key;
    if (key_callable != nullptr && key_callable->tag != ValueTag::None) {
      if (!runtime_call_callable(runtime, *key_callable, &value, 1, key, error)) {
        return false;
      }
    } else {
      value_assign_fast(key, value);
    }
    entries.push_back({std::move(key), value});
  }
  return true;
}

bool sort_entries(Runtime& runtime, std::vector<SortEntry>& entries, bool reverse, std::string& error) {
  bool compare_failed = false;
  std::string compare_error;
  std::stable_sort(entries.begin(), entries.end(), [&](const SortEntry& lhs, const SortEntry& rhs) {
    if (compare_failed) {
      return false;
    }
    Value less;
    const Value& left = reverse ? rhs.key : lhs.key;
    const Value& right = reverse ? lhs.key : rhs.key;
    if (!value_compare("<", left, right, less, compare_error)) {
      compare_failed = true;
      return false;
    }
    return value_truthy(less);
  });
  if (compare_failed) {
    return raise_type_error(runtime, compare_error.empty() ? "sorted() comparison failed" : compare_error, error);
  }
  return true;
}

bool sorted_impl(
    Runtime& runtime,
    const Value& iterable,
    const Value* key_callable,
    bool reverse,
    Value& out,
    std::string& error) {
  std::vector<SortEntry> entries;
  if (!collect_sorted_entries(runtime, iterable, key_callable, entries, error) ||
      !sort_entries(runtime, entries, reverse, error)) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(entries.size());
  for (const auto& entry : entries) {
    values.push_back(entry.value);
  }
  out = Value::list(std::move(values));
  return true;
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
  return sorted_impl(runtime, args[0], nullptr, false, out, error);
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
  const Value* key_callable = nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      return raise_type_error(runtime, "sorted() received invalid keyword argument", error);
    }
    if (name == "reverse") {
      reverse = value_truthy(*kwargs[i].value);
    } else if (name == "key") {
      key_callable = kwargs[i].value;
    } else {
      return raise_type_error(runtime, "sorted() got an unexpected keyword argument '" + name + "'", error);
    }
  }
  if (argc != 1) {
    return raise_type_error(runtime, "sorted() expected 1 argument", error);
  }
  return sorted_impl(runtime, args[0], key_callable, reverse, out, error);
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
    auto* klass = value_as_class(instance->klass);
    const size_t slot_count = klass == nullptr ? 0 : klass->instance_slot_names.size();
    entries.reserve(instance->attrs.size() + slot_count);
    if (klass != nullptr) {
      const uint32_t count = instance_slot_count(instance);
      for (size_t i = 0; i < klass->instance_slot_names.size() && i < count; ++i) {
        const auto& value = instance_slot_at(instance, static_cast<uint32_t>(i));
        if (value.tag != ValueTag::Invalid) {
          entries.push_back({Value::string(klass->instance_slot_names[i]), value});
        }
      }
    }
    for (const auto& attr : instance->attrs) {
      entries.push_back({Value::string(attr.first), attr.second});
    }
    if (auto* storage = value_as_dict(instance->mapping_storage)) {
      for (const auto& entry : storage->entries) {
        entries.push_back({entry.first, entry.second});
      }
    }
    out = Value::dict(std::move(entries));
    return true;
  }
  Value dict;
  std::string attr_error;
  if (object_get_attr(args[0], "__dict__", dict, attr_error) && value_as_dict(dict) != nullptr) {
    value_assign_fast(out, dict);
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
  value_assign_fast(out, runtime.current_globals_module());
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

struct ParsedFormatSpec {
  char fill = ' ';
  char align = '\0';
  char sign = '-';
  bool alternate = false;
  bool zero = false;
  int width = 0;
  int precision = -1;
  char type = '\0';
};

bool is_format_align(char ch) {
  return ch == '<' || ch == '>' || ch == '^';
}

bool parse_format_spec(std::string_view spec, ParsedFormatSpec& parsed, std::string& error) {
  size_t i = 0;
  if (i + 1 < spec.size() && is_format_align(spec[i + 1])) {
    parsed.fill = spec[i];
    parsed.align = spec[i + 1];
    i += 2;
  } else if (i < spec.size() && is_format_align(spec[i])) {
    parsed.align = spec[i++];
  }
  if (i < spec.size() && (spec[i] == '+' || spec[i] == '-' || spec[i] == ' ')) {
    parsed.sign = spec[i++];
  }
  if (i < spec.size() && spec[i] == '#') {
    parsed.alternate = true;
    ++i;
  }
  if (i < spec.size() && spec[i] == '0') {
    parsed.zero = true;
    parsed.fill = '0';
    if (parsed.align == '\0') {
      parsed.align = '>';
    }
    ++i;
  }
  while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
    parsed.width = parsed.width * 10 + (spec[i++] - '0');
  }
  if (i < spec.size() && spec[i] == '.') {
    ++i;
    parsed.precision = 0;
    if (i >= spec.size() || !std::isdigit(static_cast<unsigned char>(spec[i]))) {
      error = "format precision requires digits";
      return false;
    }
    while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
      parsed.precision = parsed.precision * 10 + (spec[i++] - '0');
    }
  }
  if (i < spec.size()) {
    parsed.type = spec[i++];
  }
  if (i != spec.size()) {
    error = "unsupported format specifier";
    return false;
  }
  return true;
}

std::string apply_format_width(std::string text, const ParsedFormatSpec& spec) {
  if (spec.width <= static_cast<int>(text.size())) {
    return text;
  }
  const size_t pad = static_cast<size_t>(spec.width) - text.size();
  const char align = spec.align == '\0' ? '>' : spec.align;
  if (align == '<') {
    text.append(pad, spec.fill);
    return text;
  }
  if (align == '^') {
    const size_t left = pad / 2;
    const size_t right = pad - left;
    return std::string(left, spec.fill) + text + std::string(right, spec.fill);
  }
  return std::string(pad, spec.fill) + text;
}

std::string unsigned_to_base(uint64_t value, uint32_t base, bool uppercase) {
  char buffer[65];
  size_t pos = sizeof(buffer);
  const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
  do {
    buffer[--pos] = digits[value % base];
    value /= base;
  } while (value != 0);
  return std::string(buffer + pos, buffer + sizeof(buffer));
}

bool format_int_value(int64_t value, const ParsedFormatSpec& spec, Value& out, std::string& error) {
  const char type = spec.type == '\0' ? 'd' : spec.type;
  uint32_t base = 10;
  bool uppercase = false;
  std::string prefix;
  if (type == 'b') {
    base = 2;
    if (spec.alternate) prefix = "0b";
  } else if (type == 'o') {
    base = 8;
    if (spec.alternate) prefix = "0o";
  } else if (type == 'x' || type == 'X') {
    base = 16;
    uppercase = type == 'X';
    if (spec.alternate) prefix = uppercase ? "0X" : "0x";
  } else if (type != 'd') {
    error = "unsupported integer format specifier";
    return false;
  }
  const bool negative = value < 0;
  const uint64_t magnitude = negative ? static_cast<uint64_t>(-(value + 1)) + 1u : static_cast<uint64_t>(value);
  std::string sign;
  if (negative) {
    sign = "-";
  } else if (spec.sign == '+') {
    sign = "+";
  } else if (spec.sign == ' ') {
    sign = " ";
  }
  std::string digits = unsigned_to_base(magnitude, base, uppercase);
  std::string text;
  if (spec.zero && (spec.align == '>' || spec.align == '\0') && spec.width > 0) {
    const size_t reserved = sign.size() + prefix.size() + digits.size();
    text = sign + prefix;
    if (spec.width > static_cast<int>(reserved)) {
      text.append(static_cast<size_t>(spec.width) - reserved, '0');
    }
    text += digits;
  } else {
    text = sign + prefix + digits;
    text = apply_format_width(std::move(text), spec);
  }
  out = Value::string(std::move(text));
  return true;
}

bool format_double_value(double value, const ParsedFormatSpec& spec, Value& out, std::string& error) {
  char type = spec.type == '\0' ? 'g' : spec.type;
  if (type == 'F') {
    type = 'f';
  }
  if (type != 'f' && type != 'g' && type != 'e' && type != 'E' && type != '%') {
    error = "unsupported float format specifier";
    return false;
  }
  const bool percent = type == '%';
  const double printed_value = percent ? value * 100.0 : value;
  const int precision = spec.precision >= 0 ? spec.precision : 6;
  char buffer[256];
  const char printf_type = percent ? 'f' : type;
  std::string control = "%." + std::to_string(precision) + std::string(1, printf_type);
  std::snprintf(buffer, sizeof(buffer), control.c_str(), printed_value);
  std::string text(buffer);
  if (!text.empty() && text[0] != '-' && (spec.sign == '+' || spec.sign == ' ')) {
    text.insert(text.begin(), spec.sign == '+' ? '+' : ' ');
  }
  if (percent) {
    text.push_back('%');
  }
  out = Value::string(apply_format_width(std::move(text), spec));
  return true;
}

bool format_string_value(const Value& value, const ParsedFormatSpec& spec, Value& out, std::string& error) {
  if (spec.type != '\0' && spec.type != 's') {
    error = "unsupported string format specifier";
    return false;
  }
  std::string text = value_to_string(value);
  if (spec.precision >= 0 && spec.precision < static_cast<int>(text.size())) {
    text.resize(static_cast<size_t>(spec.precision));
  }
  out = Value::string(apply_format_width(std::move(text), spec));
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

  ParsedFormatSpec parsed;
  if (!parse_format_spec(spec, parsed, error)) {
    return false;
  }
  if (args[0].tag == ValueTag::Int64) {
    return format_int_value(args[0].as.i64, parsed, out, error);
  }
  if (args[0].tag == ValueTag::Double) {
    return format_double_value(args[0].as.f64, parsed, out, error);
  }
  return format_string_value(args[0], parsed, out, error);
}

bool builtin_hash(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "hash() expected 1 argument", error);
  }
  size_t hash = 0;
  if (!value_hash_key(args[0], hash, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::int64(static_cast<int64_t>(hash));
  return true;
}

bool append_utf8_codepoint(int64_t codepoint, std::string& out) {
  if (codepoint < 0 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
    return false;
  }
  if (codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
  return true;
}

bool builtin_chr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "chr() expected 1 argument", error);
  }
  if (args[0].tag != ValueTag::Int64) {
    return raise_type_error(runtime, "chr() argument must be int", error);
  }
  std::string text;
  if (!append_utf8_codepoint(args[0].as.i64, text)) {
    error = "chr() arg not in range(0x110000)";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::string(std::move(text));
  return true;
}

std::string format_integer_base(int64_t value, uint32_t base, const char* prefix) {
  const bool negative = value < 0;
  uint64_t magnitude = negative ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
  std::string digits = unsigned_to_base(magnitude, base, false);
  return std::string(negative ? "-" : "") + prefix + digits;
}

bool builtin_bin(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    return raise_type_error(runtime, "bin() expected int argument", error);
  }
  out = Value::string(format_integer_base(args[0].as.i64, 2, "0b"));
  return true;
}

bool builtin_oct(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    return raise_type_error(runtime, "oct() expected int argument", error);
  }
  out = Value::string(format_integer_base(args[0].as.i64, 8, "0o"));
  return true;
}

bool builtin_hex(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    return raise_type_error(runtime, "hex() expected int argument", error);
  }
  out = Value::string(format_integer_base(args[0].as.i64, 16, "0x"));
  return true;
}

bool builtin_pow(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 && argc != 3) {
    return raise_type_error(runtime, "pow() expected 2 or 3 arguments", error);
  }
  if (argc == 3) {
    if (args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64 || args[2].tag != ValueTag::Int64) {
      return raise_type_error(runtime, "pow() 3-argument form requires ints in this XLang3 compatibility subset", error);
    }
    if (args[2].as.i64 == 0) {
      error = "pow() 3rd argument cannot be 0";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    int64_t base = args[0].as.i64 % args[2].as.i64;
    int64_t exp = args[1].as.i64;
    int64_t result_value = 1 % args[2].as.i64;
    if (exp < 0) {
      return raise_type_error(runtime, "pow() 2nd argument cannot be negative when 3rd argument specified", error);
    }
    while (exp > 0) {
      if ((exp & 1) != 0) {
        result_value = (result_value * base) % args[2].as.i64;
      }
      base = (base * base) % args[2].as.i64;
      exp >>= 1;
    }
    out = Value::int64(result_value);
    return true;
  }
  if (!value_pow(args[0], args[1], out, error)) {
    const std::string message = error;
    return raise_type_error(runtime, message, error);
  }
  return true;
}

bool builtin_divmod(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return raise_type_error(runtime, "divmod() expected 2 arguments", error);
  }
  Value quotient;
  Value remainder;
  if (!value_floor_div(args[0], args[1], quotient, error) ||
      !value_mod(args[0], args[1], remainder, error)) {
    const std::string message = error;
    return raise_type_error(runtime, message, error);
  }
  out = Value::tuple({quotient, remainder});
  return true;
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
  runtime.register_native_builtin("hash", builtin_hash);
  runtime.register_native_builtin("chr", builtin_chr);
  runtime.register_native_builtin("bin", builtin_bin);
  runtime.register_native_builtin("oct", builtin_oct);
  runtime.register_native_builtin("hex", builtin_hex);
  runtime.register_native_builtin("pow", builtin_pow);
  runtime.register_native_builtin("divmod", builtin_divmod);
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
