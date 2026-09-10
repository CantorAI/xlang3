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
#include "xlang3/ir.h"
#include "xlang3/mapping.h"
#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sequence.h"
#include "xlang3/sema.h"
#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

#include "source_encoding.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace xlang3 {

namespace {

bool raise_type_error(Runtime& runtime, std::string message, std::string& error) {
  error = std::move(message);
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool exception_matches_builtin(Runtime& runtime, const Value& exception, const char* name) {
  auto* actual = value_as_class(runtime.exception_type(exception));
  const Value* expected_value = runtime.find_builtin(name);
  auto* expected = expected_value == nullptr ? nullptr : value_as_class(*expected_value);
  if (actual == nullptr || expected == nullptr) {
    return false;
  }
  return actual == expected || class_is_subclass(actual, expected);
}

void attach_attribute_error_context(
    Runtime& runtime,
    Value& exception,
    const Value& object,
    const Value& name) {
  if (!exception_matches_builtin(runtime, exception, "AttributeError")) {
    return;
  }
  Value existing;
  std::string ignored;
  if (!object_get_attr(exception, "name", existing, ignored) ||
      existing.tag == ValueTag::None || existing.tag == ValueTag::Invalid) {
    ignored.clear();
    object_set_attr(exception, "name", name, ignored);
  }
  value_set_invalid(existing);
  ignored.clear();
  if (!object_get_attr(exception, "obj", existing, ignored) ||
      existing.tag == ValueTag::None || existing.tag == ValueTag::Invalid) {
    ignored.clear();
    object_set_attr(exception, "obj", object, ignored);
  }
}

bool value_to_source_text(Runtime& runtime, const Value& value, std::string& out, std::string& error) {
  auto* string = value_as_string(value);
  if (string != nullptr) {
    out = string_object_to_string(*string);
    return true;
  }

  std::string_view bytes_view;
  if (auto* bytes = value_as_bytes(value)) {
    bytes_view = bytes_object_view(*bytes);
  } else if (auto* bytearray = value_as_bytearray(value)) {
    bytes_view = std::string_view(bytearray->value.data(), bytearray->value.size());
  } else {
    return raise_type_error(runtime, "source must be str, bytes or code object", error);
  }

  PythonSourceText decoded;
  if (!decode_python_source_bytes(bytes_view, decoded, error)) {
    runtime.raise_class_error("SyntaxError", error);
    return false;
  }
  out = std::move(decoded.text);
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
  bool module_not_found = false;
  if (!runtime.import_module(name, module, error, &module_not_found)) {
    runtime.raise_class_error(module_not_found ? "ModuleNotFoundError" : "ImportError", error);
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
    int64_t flags,
    Value& out,
    std::string& error) {
  auto raise_syntax_parse_error = [&](const std::string& parse_error) -> bool {
    error = parse_error.find("unexpected character '") != std::string::npos
        ? "invalid syntax"
        : parse_error;
    const bool allow_incomplete = (flags & 0x4000) != 0;
    const bool looks_incomplete =
        source.empty() ||
        (!source.empty() && source.find_first_not_of(" \t\r\n") != std::string::npos && source.back() == ':') ||
        parse_error.find("expected indented") != std::string::npos ||
        parse_error.find("expected expression") != std::string::npos ||
        parse_error.find("unterminated") != std::string::npos;
    const std::string line_prefix = "line ";
    const std::string column_marker = ", column ";
    const std::string end_column_marker = ", end column ";
    const size_t line_start = parse_error.find(line_prefix);
    const size_t column_start = parse_error.find(column_marker);
    const bool indentation_error =
        parse_error.find("indentation") != std::string::npos ||
        (line_start != std::string::npos && column_start != std::string::npos &&
         parse_error.find("column 1") != std::string::npos && !source.empty() &&
         (source.front() == ' ' || source.front() == '\t'));
    const char* exception_name = allow_incomplete && looks_incomplete
        ? "_IncompleteInputError"
        : (indentation_error ? "IndentationError" : "SyntaxError");
    Value exception = runtime.make_exception(exception_name, error);
    if (line_start != std::string::npos) {
      try {
        size_t line_end = parse_error.find_first_not_of("0123456789", line_start + line_prefix.size());
        if (line_end == std::string::npos) line_end = parse_error.size();
        const int64_t line_number = std::stoll(
            parse_error.substr(line_start + line_prefix.size(), line_end - line_start - line_prefix.size()));
        std::string source_line;
        size_t source_start = 0;
        for (int64_t current = 1; current < line_number; ++current) {
          const size_t newline = source.find('\n', source_start);
          if (newline == std::string::npos) break;
          source_start = newline + 1;
        }
        size_t source_end = source.find('\n', source_start);
        if (source_end == std::string::npos) source_end = source.size();
        const std::string raw_source_line = source.substr(source_start, source_end - source_start);
        source_line = raw_source_line + "\n";
        int64_t column_number = 0;
        int64_t end_column_number = -1;
        if (column_start != std::string::npos && column_start > line_start) {
          size_t column_end = parse_error.find(':', column_start + column_marker.size());
          if (column_end == std::string::npos) column_end = parse_error.size();
          column_number = std::stoll(parse_error.substr(
              column_start + column_marker.size(), column_end - column_start - column_marker.size()));
          const size_t end_column_start = parse_error.find(end_column_marker, column_start + column_marker.size());
          if (end_column_start != std::string::npos) {
            size_t end_column_end = parse_error.find(':', end_column_start + end_column_marker.size());
            if (end_column_end == std::string::npos) end_column_end = parse_error.size();
            end_column_number = std::stoll(parse_error.substr(
                end_column_start + end_column_marker.size(),
                end_column_end - end_column_start - end_column_marker.size()));
          }
        } else if (const size_t unexpected = parse_error.find("unexpected character '");
                   unexpected != std::string::npos && unexpected + 22 < parse_error.size()) {
          const char invalid = parse_error[unexpected + 22];
          const size_t found = raw_source_line.find(invalid);
          column_number = found == std::string::npos
              ? static_cast<int64_t>(raw_source_line.size() + 1)
              : static_cast<int64_t>(found + 1);
        } else if (indentation_error) {
          column_number = static_cast<int64_t>(raw_source_line.size() + 1);
        }
        if (!indentation_error && parse_error.find("expected expression") != std::string::npos) {
          std::vector<size_t> open_brackets;
          for (size_t i = 0; i < raw_source_line.size(); ++i) {
            if (raw_source_line[i] == '(' || raw_source_line[i] == '[' || raw_source_line[i] == '{') {
              open_brackets.push_back(i);
            } else if ((raw_source_line[i] == ')' || raw_source_line[i] == ']' || raw_source_line[i] == '}') &&
                       !open_brackets.empty()) {
              open_brackets.pop_back();
            }
          }
          if (!open_brackets.empty()) {
            column_number = static_cast<int64_t>(open_brackets.back() + 1);
            end_column_number = 0;
          }
        }
        std::string ignored;
        object_set_attr(exception, "filename", Value::string(filename), ignored);
        object_set_attr(exception, "lineno", Value::int64(line_number), ignored);
        object_set_attr(
            exception, "offset",
            column_number == 0 ? Value::none() : Value::int64(column_number), ignored);
        object_set_attr(exception, "text", Value::string(std::move(source_line)), ignored);
        object_set_attr(exception, "end_lineno", Value::int64(line_number), ignored);
        object_set_attr(
            exception, "end_offset",
            indentation_error ? Value::int64(-1)
                              : (column_number == 0
                                     ? Value::none()
                                     : Value::int64(
                                           end_column_number < 0 ? column_number + 1 : end_column_number)),
            ignored);
      } catch (...) {
        // Keep the original parser diagnostic when it has no numeric location.
      }
    }
    runtime.set_pending_exception(std::move(exception));
    return false;
  };

  if (mode == "eval") {
    if (eval_source_starts_with_statement_keyword(source)) {
      error = "invalid syntax";
      runtime.raise_class_error("SyntaxError", error);
      return false;
    }
    auto parsed_expr = parse_expression_source(source);
    if (!parsed_expr.errors.empty()) {
      return raise_syntax_parse_error(parsed_expr.errors.front());
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

  // Reject a generator expression followed by another call argument before
  // entering the general parser.  Besides matching Python's diagnostic, this
  // prevents recovery from repeatedly reconsidering the same `for` token.
  if (const size_t generator_for = source.find(" for "); generator_for != std::string::npos) {
    const size_t call_open = source.rfind('(', generator_for);
    size_t following_comma = std::string::npos;
    if (call_open != std::string::npos) {
      int depth = 0;
      for (size_t i = call_open; i < source.size(); ++i) {
        if (source[i] == '(' || source[i] == '[' || source[i] == '{') {
          ++depth;
        } else if (source[i] == ')' || source[i] == ']' || source[i] == '}') {
          --depth;
          if (depth == 0) break;
        } else if (source[i] == ',' && depth == 1 && i > generator_for) {
          following_comma = i;
          break;
        }
      }
    }
    if (call_open != std::string::npos && following_comma != std::string::npos) {
      size_t preceding_comma = source.rfind(',', generator_for);
      size_t expression_start = call_open + 1;
      if (preceding_comma != std::string::npos && preceding_comma > call_open) {
        expression_start = preceding_comma + 1;
      }
      while (expression_start < source.size() &&
             (source[expression_start] == ' ' || source[expression_start] == '\t')) {
        ++expression_start;
      }
      return raise_syntax_parse_error(
          "line 1, column " + std::to_string(expression_start + 1) +
          ", end column " + std::to_string(following_comma + 1) +
          ": Generator expression must be parenthesized");
    }
  }

  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    return raise_syntax_parse_error(parsed.errors.front());
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

bool compile_source_to_ast(
    Runtime& runtime,
    const std::string& source,
    const std::string& filename,
    const std::string& mode,
    Value& out,
    std::string& error) {
  Value ast_module;
  if (!runtime.import_module("_ast", ast_module, error)) {
    return false;
  }
  Value parse_func;
  if (!module_get_attr(ast_module, "parse", parse_func, error)) {
    return false;
  }
  Value parse_args[3] = {Value::string(source), Value::string(filename), Value::string(mode)};
  return runtime_call_callable(runtime, parse_func, parse_args, 3, out, error);
}

bool run_code_object(Runtime& runtime, CodeObject& code, Value globals_module, Value& out, std::string& error) {
  if (code.module == nullptr || code.function_id >= code.module->functions.size()) {
    error = "invalid code object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  Interpreter interpreter(runtime);
  Value target_globals = std::move(globals_module);
  RuntimeResult result = interpreter.run_module(*code.module, target_globals, code.module, false);
  if (!result.errors.empty()) {
    error = result.errors.front();
    if (result.exception.tag != ValueTag::None && result.exception.tag != ValueTag::Invalid) {
      runtime.set_pending_exception(result.exception);
    }
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

bool copy_module_to_dict(const Value& module_value, Value& dict_value, std::string& error) {
  auto* module = value_as_module(module_value);
  if (module == nullptr || value_as_dict(dict_value) == nullptr) {
    return false;
  }
  for (const auto& entry : module->name_to_slot) {
    if (entry.second >= module->slots.size()) {
      continue;
    }
    if (!mapping_set_item(dict_value, Value::string(entry.first), module->slots[entry.second], error)) {
      return false;
    }
  }
  return true;
}

bool copy_module_to_module(const Value& source_value, Value& target_value, std::string& error) {
  auto* source = value_as_module(source_value);
  if (source == nullptr || value_as_module(target_value) == nullptr) {
    return false;
  }
  for (const auto& entry : source->name_to_slot) {
    if (entry.second >= source->slots.size()) {
      continue;
    }
    if (!module_set_attr(target_value, entry.first, source->slots[entry.second], error)) {
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
  Value globals;
  const bool globals_is_dict = argc >= 2 && value_as_dict(args[1]) != nullptr;
  if (argc >= 2 && value_as_module(args[1]) != nullptr) {
    value_assign_fast(globals, args[1]);
  } else if (argc < 2 || args[1].tag == ValueTag::None) {
    globals = runtime.current_globals_module();
    if (value_as_module(globals) == nullptr) {
      return false;
    }
  } else if (!globals_is_dict) {
    return false;
  }

  const bool has_distinct_locals = argc >= 3 && args[2].tag != ValueTag::None;
  if (!globals_is_dict && !has_distinct_locals) {
    value_assign_fast(out, globals);
    return true;
  }

  out = Value::module("<eval>");
  module_set_attr(out, "__name__", Value::string("<eval>"), error);
  if (globals_is_dict) {
    if (!copy_dict_to_module(args[1], out, error)) {
      return false;
    }
  } else if (!copy_module_to_module(globals, out, error)) {
    return false;
  }
  if (has_distinct_locals) {
    if (value_as_dict(args[2]) != nullptr) {
      if (!copy_dict_to_module(args[2], out, error)) {
        return false;
      }
    } else if (value_as_module(args[2]) != nullptr) {
      if (!copy_module_to_module(args[2], out, error)) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

bool collect_iterable(Runtime& runtime, const Value& iterable, std::vector<Value>& out, std::string& error) {
  return runtime_collect_iterable(runtime, iterable, out, error);
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

bool value_is_callable(Runtime&, const Value& value) {
  if (value_as_function(value) != nullptr ||
      value_as_native_function(value) != nullptr ||
      value_as_bound_method(value) != nullptr ||
      value_as_static_method(value) != nullptr ||
      value_as_class(value) != nullptr) {
    return true;
  }
  if (value_as_instance(value) != nullptr) {
    Value call_attr;
    std::string ignored;
    return object_get_class_attr_for_instance(value, "__call__", call_attr, ignored);
  }
  return false;
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
  if (!value_is_callable(runtime, args[0])) {
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
  if (!value_is_callable(runtime, args[0])) {
    return raise_type_error(runtime, "staticmethod() argument must be callable", error);
  }
  out = Value::static_method(args[0]);
  return true;
}

bool infer_super_defining_class(Runtime& runtime, const Value& self, Value& out, std::string& error) {
  auto* instance = value_as_instance(self);
  Value subject_class;
  if (instance != nullptr) {
    value_assign_fast(subject_class, instance->klass);
  } else if (value_as_class(self) != nullptr) {
    value_assign_fast(subject_class, self);
  } else {
    error = "super(): current self is not an instance or class";
    return false;
  }
  Value mro_value;
  if (!object_get_attr(subject_class, "__mro__", mro_value, error)) {
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
  if (value_as_class(self) != nullptr) {
    Value metaclass_value;
    std::string meta_error;
    if (object_get_attr(self, "__class__", metaclass_value, meta_error)) {
      Value meta_mro_value;
      auto* meta_mro = value_as_tuple(meta_mro_value);
      if (object_get_attr(metaclass_value, "__mro__", meta_mro_value, meta_error) &&
          (meta_mro = value_as_tuple(meta_mro_value)) != nullptr) {
        for (const auto& class_value : meta_mro->items) {
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
              value_assign_fast(out, class_value);
              return true;
            }
          }
        }
      }
    }
  }
  value_assign_fast(out, subject_class);
  return true;
}

bool current_super_first_argument(Runtime& runtime, const Value& locals, Value& out) {
  std::string ignored;
  const auto* owner = runtime.current_frame_module_owner();
  if (owner != nullptr && *owner) {
    const uint32_t function_id = runtime.current_frame_function_id();
    if (function_id < (*owner)->functions.size() && !(*owner)->functions[function_id].params.empty()) {
      const auto& first_name = (*owner)->functions[function_id].params[0];
      if (!first_name.empty() && mapping_get_item(locals, Value::string(first_name), out, ignored)) {
        return true;
      }
    }
  }
  return mapping_get_item(locals, Value::string("self"), out, ignored) ||
         mapping_get_item(locals, Value::string("cls"), out, ignored);
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
    if (!current_super_first_argument(runtime, locals, self)) {
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
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "callable() expected 1 argument";
    return false;
  }
  out = Value::boolean(value_is_callable(runtime, args[0]));
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
  if (!runtime_get_iter(runtime, args[0], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = functional_enumerate_iterator(std::move(iterator), index);
  return true;
}

bool call_binary_method_if_implemented(
    Runtime& runtime,
    const Value& receiver,
    const Value& argument,
    const char* name,
    bool& implemented,
    Value& out,
    std::string& error) {
  implemented = false;
  Value method;
  std::string attr_error;
  if (!attribute_get(receiver, name, method, attr_error)) {
    return true;
  }
  Value result;
  if (!runtime_call_callable(runtime, method, &argument, 1, result, error)) {
    return false;
  }
  const Value* not_implemented = runtime.find_builtin("NotImplemented");
  if (not_implemented != nullptr && value_is(result, *not_implemented)) {
    return true;
  }
  implemented = true;
  value_assign_fast(out, result);
  return true;
}

bool binary_or_impl(
    Runtime& runtime,
    const Value& left,
    const Value& right,
    Value& out,
    std::string& error) {
  bool implemented = false;
  if (!call_binary_method_if_implemented(
          runtime, left, right, "__or__", implemented, out, error)) {
    return false;
  }
  if (implemented) {
    return true;
  }
  if (!call_binary_method_if_implemented(
          runtime, right, left, "__ror__", implemented, out, error)) {
    return false;
  }
  if (implemented) {
    return true;
  }
  if (value_bit_or(left, right, out, error)) {
    return true;
  }
  return raise_type_error(runtime, error, error);
}

bool builtin_binary_or(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    return raise_type_error(runtime, "binary | expects two operands", error);
  }
  return binary_or_impl(runtime, args[0], args[1], out, error);
}

bool builtin_inplace_or(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    return raise_type_error(runtime, "in-place | expects two operands", error);
  }
  bool implemented = false;
  if (!call_binary_method_if_implemented(
          runtime, args[0], args[1], "__ior__", implemented, out, error)) {
    return false;
  }
  if (implemented) {
    return true;
  }
  return binary_or_impl(runtime, args[0], args[1], out, error);
}

bool builtin_inplace_add(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    return raise_type_error(runtime, "in-place + expects two operands", error);
  }
  bool implemented = false;
  if (!call_binary_method_if_implemented(
          runtime, args[0], args[1], "__iadd__", implemented, out, error)) {
    return false;
  }
  if (implemented) {
    return true;
  }
  if (value_as_list(args[0]) != nullptr) {
    std::vector<Value> items;
    if (!runtime_collect_iterable(runtime, args[1], items, error)) {
      return false;
    }
    Value target = args[0];
    for (const auto& item : items) {
      if (!sequence_list_append(target, item, error)) {
        return false;
      }
    }
    value_assign_fast(out, args[0]);
    return true;
  }
  if (!call_binary_method_if_implemented(
          runtime, args[0], args[1], "__add__", implemented, out, error)) {
    return false;
  }
  if (implemented) {
    return true;
  }
  if (!call_binary_method_if_implemented(
          runtime, args[1], args[0], "__radd__", implemented, out, error)) {
    return false;
  }
  if (implemented) {
    return true;
  }
  if (value_add(args[0], args[1], out, error)) {
    return true;
  }
  return raise_type_error(runtime, error, error);
}

bool builtin_enumerate_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1 || argc > 2) {
    return raise_type_error(runtime, "enumerate() expected 1 or 2 arguments", error);
  }
  Value positional[2];
  value_assign_fast(positional[0], args[0]);
  uint32_t positional_count = argc;
  if (argc == 2) {
    value_assign_fast(positional[1], args[1]);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name != "start") {
      return raise_type_error(runtime, "enumerate() got an unexpected keyword argument '" + name + "'", error);
    }
    if (argc == 2) {
      return raise_type_error(runtime, "enumerate() got multiple values for argument 'start'", error);
    }
    value_assign_fast(positional[1], *kwargs[i].value);
    positional_count = 2;
  }
  return builtin_enumerate(runtime, positional, positional_count, out, error, user_data);
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
    if (!runtime_get_iter(runtime, args[i], iterator, error)) {
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
  if (!runtime_get_iter(runtime, args[1], iterator, error)) {
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
    out = functional_zip_iterator(&runtime, {});
    return true;
  }
  std::vector<Value> iterators;
  iterators.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    Value iterator;
    if (!runtime_get_iter(runtime, args[i], iterator, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    iterators.push_back(std::move(iterator));
  }
  out = functional_zip_iterator(&runtime, std::move(iterators));
  return true;
}

void rebind_exec_globals(
    Value& value,
    const Value& source_module,
    const Value& target_module,
    const Value& target_globals_dict,
    std::unordered_set<Object*>& visited) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || !visited.insert(value.as.obj).second) {
    return;
  }
  if (auto* function = value_as_function(value)) {
    if (value_is(function->globals_module, source_module)) {
      value_assign_fast(function->globals_module, target_module);
      if (target_globals_dict.tag != ValueTag::Invalid) {
        value_assign_fast(function->globals_dict, target_globals_dict);
      }
    }
    return;
  }
  if (auto* klass = value_as_class(value)) {
    if (value_is(klass->globals_module, source_module)) {
      value_assign_fast(klass->globals_module, target_module);
    }
    for (auto& attr : klass->attrs) {
      rebind_exec_globals(attr.second, source_module, target_module, target_globals_dict, visited);
    }
    return;
  }
  if (auto* method = value_as_static_method(value)) {
    rebind_exec_globals(method->function, source_module, target_module, target_globals_dict, visited);
    return;
  }
  if (auto* method = value_as_class_method(value)) {
    rebind_exec_globals(method->function, source_module, target_module, target_globals_dict, visited);
    return;
  }
  if (auto* property = value_as_property(value)) {
    rebind_exec_globals(property->fget, source_module, target_module, target_globals_dict, visited);
    rebind_exec_globals(property->fset, source_module, target_module, target_globals_dict, visited);
    rebind_exec_globals(property->fdel, source_module, target_module, target_globals_dict, visited);
  }
}

void rebind_exec_module_globals(
    Value& source_module,
    const Value& target_module,
    const Value& target_globals_dict = Value::invalid()) {
  auto* module = value_as_module(source_module);
  if (module == nullptr) {
    return;
  }
  std::unordered_set<Object*> visited;
  for (auto& value : module->slots) {
    rebind_exec_globals(value, source_module, target_module, target_globals_dict, visited);
  }
}

bool builtin_zip_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  bool strict = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name != "strict") {
      return raise_type_error(runtime, "zip() got an unexpected keyword argument '" + name + "'", error);
    }
    strict = value_truthy(*kwargs[i].value);
  }
  std::vector<Value> iterators;
  iterators.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    Value iterator;
    if (!runtime_get_iter(runtime, args[i], iterator, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    iterators.push_back(std::move(iterator));
  }
  out = functional_zip_iterator(&runtime, std::move(iterators), strict);
  return true;
}

bool builtin_reversed(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "reversed() expected 1 argument", error);
  }
  Value length_value;
  if (!sequence_len(args[0], length_value, error) || length_value.tag != ValueTag::Int64) {
    return raise_type_error(runtime, "object is not reversible", error);
  }
  std::vector<Value> values;
  values.reserve(static_cast<size_t>(length_value.as.i64 < 0 ? 0 : length_value.as.i64));
  for (int64_t i = length_value.as.i64; i > 0; --i) {
    Value item;
    if (!sequence_get_item(args[0], Value::int64(i - 1), item, error)) {
      return raise_type_error(runtime, error.empty() ? "object is not reversible" : error, error);
    }
    values.push_back(std::move(item));
  }
  Value reversed_list = Value::list(std::move(values));
  return runtime_get_iter(runtime, reversed_list, out, error);
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
  if (!runtime_get_iter(runtime, args[0], iterator, error)) {
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
    if (!runtime_value_compare(runtime, "<", left, right, less, compare_error)) {
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
    bool want_min,
    const Value* key_callable = nullptr,
    const Value* default_value = nullptr) {
  if (argc == 0) {
    return raise_type_error(runtime, want_min ? "min() expected at least 1 argument" : "max() expected at least 1 argument", error);
  }
  if (default_value != nullptr && argc != 1) {
    return raise_type_error(runtime, want_min ? "min() default only valid with a single iterable" : "max() default only valid with a single iterable", error);
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
    if (default_value != nullptr) {
      value_assign_fast(out, *default_value);
      return true;
    }
    runtime.raise_class_error("ValueError", want_min ? "min() arg is an empty sequence" : "max() arg is an empty sequence");
    error = want_min ? "min() arg is an empty sequence" : "max() arg is an empty sequence";
    return false;
  }
  std::vector<Value> keys;
  if (key_callable != nullptr && key_callable->tag != ValueTag::None) {
    keys.reserve(values.size());
    for (const auto& value : values) {
      Value key;
      if (!runtime_call_callable(runtime, *key_callable, &value, 1, key, error)) {
        return false;
      }
      keys.push_back(std::move(key));
    }
  }
  size_t best = 0;
  for (size_t i = 1; i < values.size(); ++i) {
    Value comparison;
    const Value& left = keys.empty() ? values[i] : keys[i];
    const Value& right = keys.empty() ? values[best] : keys[best];
    if (!value_compare(want_min ? "<" : ">", left, right, comparison, error)) {
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

bool minmax_common_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    bool want_min) {
  const Value* key_callable = nullptr;
  const Value* default_value = nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      return raise_type_error(runtime, want_min ? "min() received invalid keyword argument" : "max() received invalid keyword argument", error);
    }
    if (name == "key") {
      key_callable = kwargs[i].value;
    } else if (name == "default") {
      default_value = kwargs[i].value;
    } else {
      return raise_type_error(runtime, (want_min ? "min() got an unexpected keyword argument '" : "max() got an unexpected keyword argument '") + name + "'", error);
    }
  }
  return minmax_common(runtime, args, argc, out, error, want_min, key_callable, default_value);
}

bool builtin_min_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return minmax_common_kw(runtime, args, argc, kwargs, kwargc, out, error, true);
}

bool builtin_max_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return minmax_common_kw(runtime, args, argc, kwargs, kwargc, out, error, false);
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
  const std::string attr_name = string_object_to_string(*name);
  const auto call_class_getattr = [&](Value& result, bool& attempted, std::string& call_error) {
    attempted = false;
    auto* klass = value_as_class(args[0]);
    if (klass == nullptr || value_as_class(klass->metaclass) == nullptr) return false;
    Value hook;
    std::string hook_error;
    if (!class_get_bound_attr(
            runtime, klass->metaclass, args[0], "__getattr__", hook, hook_error)) {
      return false;
    }
    attempted = true;
    return runtime_call_callable(runtime, hook, &args[1], 1, result, call_error);
  };
  if (attr_name == "__class__" && runtime_type_of_value(runtime, args[0], out)) {
    return true;
  }
  if (attr_name == "__annotations__" && value_as_class(args[0]) != nullptr) {
    if (object_get_class_annotations(runtime, args[0], out, error)) {
      return true;
    }
    Value pending;
    if (argc == 3 && runtime.take_pending_exception(pending) &&
        exception_matches_builtin(runtime, pending, "AttributeError")) {
      value_assign_fast(out, args[2]);
      error.clear();
      return true;
    }
    if (pending.tag != ValueTag::Invalid) {
      runtime.set_pending_exception(std::move(pending));
    }
    return false;
  }
  if (auto* instance = value_as_instance(args[0])) {
    auto* klass = value_as_class(instance->klass);
    Value hook;
    std::string hook_error;
    if (klass && klass->has_getattribute_hook &&
        object_get_class_attr_for_instance(args[0], "__getattribute__", hook, hook_error)) {
      if (runtime_call_callable(runtime, hook, args, 2, out, error)) return true;
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (exception_matches_builtin(runtime, pending, "AttributeError")) {
          if (klass->has_getattr_hook &&
              object_get_class_attr_for_instance(args[0], "__getattr__", hook, hook_error)) {
            if (runtime_call_callable(runtime, hook, args, 2, out, error)) return true;
            if (!runtime.take_pending_exception(pending)) return false;
          }
          attach_attribute_error_context(runtime, pending, args[0], args[1]);
          if (argc == 3 && exception_matches_builtin(runtime, pending, "AttributeError")) {
            value_assign_fast(out, args[2]);
            error.clear();
            return true;
          }
        }
        runtime.set_pending_exception(std::move(pending));
      }
      return false;
    }
  }
  if (value_as_instance(args[0]) != nullptr) {
    Value descriptor;
    std::string descriptor_error;
    if (object_get_class_attr_for_instance(args[0], attr_name, descriptor, descriptor_error) &&
        object_value_has_descriptor_get(descriptor)) {
      Value get_method;
      std::string get_error;
      if (attribute_get(descriptor, "__get__", get_method, get_error)) {
        auto* instance = value_as_instance(args[0]);
        Value get_args[2];
        value_assign_fast(get_args[0], args[0]);
        value_assign_fast(get_args[1], instance->klass);
        if (runtime_call_callable(runtime, get_method, get_args, 2, out, get_error)) {
          return true;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          if (exception_matches_builtin(runtime, pending, "AttributeError")) {
            if (argc == 3) {
              value_assign_fast(out, args[2]);
              return true;
            }
          }
          runtime.set_pending_exception(std::move(pending));
          error = get_error;
          return false;
        }
      }
    }
  }
  if (value_as_class(args[0]) != nullptr) {
    auto* receiver_class = value_as_class(args[0]);
    auto* metaclass = receiver_class == nullptr ? nullptr : value_as_class(receiver_class->metaclass);
    Value meta_descriptor;
    std::string meta_error;
    if (metaclass != nullptr &&
        object_lookup_class_attr(receiver_class->metaclass, attr_name, meta_descriptor, meta_error) &&
        object_value_is_data_descriptor(meta_descriptor) &&
        object_value_has_descriptor_get(meta_descriptor)) {
      Value get_method;
      if (attribute_get(meta_descriptor, "__get__", get_method, meta_error)) {
        Value get_args[2] = {args[0], receiver_class->metaclass};
        if (runtime_call_callable(runtime, get_method, get_args, 2, out, meta_error)) {
          return true;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          if (argc == 3 && exception_matches_builtin(runtime, pending, "AttributeError")) {
            value_assign_fast(out, args[2]);
            error.clear();
            return true;
          }
          runtime.set_pending_exception(std::move(pending));
        }
        error = std::move(meta_error);
        return false;
      }
    }
    Value descriptor;
    std::string descriptor_error;
    if (object_lookup_class_attr(args[0], attr_name, descriptor, descriptor_error) &&
        object_value_has_descriptor_get(descriptor)) {
      Value get_method;
      std::string get_error;
      if (attribute_get(descriptor, "__get__", get_method, get_error)) {
        Value get_args[2] = {Value::none(), args[0]};
        if (runtime_call_callable(runtime, get_method, get_args, 2, out, get_error)) {
          return true;
        }
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          if (exception_matches_builtin(runtime, pending, "AttributeError")) {
            bool attempted = false;
            std::string call_error;
            if (call_class_getattr(out, attempted, call_error)) return true;
            if (attempted) {
              Value hook_pending;
              if (runtime.take_pending_exception(hook_pending)) pending = std::move(hook_pending);
              error = std::move(call_error);
            }
            if (argc == 3 && exception_matches_builtin(runtime, pending, "AttributeError")) {
              value_assign_fast(out, args[2]);
              error.clear();
              return true;
            }
          }
          runtime.set_pending_exception(std::move(pending));
          error = std::move(get_error);
          return false;
        }
      }
    }
  }
  std::string attr_error;
  if (attribute_get(args[0], attr_name, out, attr_error)) {
    return true;
  }
  if (auto* instance = value_as_instance(args[0])) {
    auto* klass = value_as_class(instance->klass);
    Value hook;
    std::string hook_error;
    if (klass != nullptr && klass->has_getattr_hook &&
        object_get_class_attr_for_instance(args[0], "__getattr__", hook, hook_error)) {
      Value hook_args[] = {args[0], args[1]};
      if (runtime_call_callable(runtime, hook, hook_args, 2, out, error)) {
        return true;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        attach_attribute_error_context(runtime, pending, args[0], args[1]);
        if (argc == 3 && exception_matches_builtin(runtime, pending, "AttributeError")) {
          value_assign_fast(out, args[2]);
          error.clear();
          return true;
        }
        runtime.set_pending_exception(std::move(pending));
      }
      return false;
    }
  }
  if (value_as_class(args[0]) != nullptr) {
    bool attempted = false;
    std::string call_error;
    if (call_class_getattr(out, attempted, call_error)) return true;
    if (attempted) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (argc == 3 && exception_matches_builtin(runtime, pending, "AttributeError")) {
          value_assign_fast(out, args[2]);
          error.clear();
          return true;
        }
        runtime.set_pending_exception(std::move(pending));
      }
      error = std::move(call_error);
      return false;
    }
  }
  if (auto* module = value_as_module(args[0])) {
    auto slot = module->name_to_slot.find(attr_name);
    if (slot != module->name_to_slot.end() && slot->second < module->slots.size()) {
      auto* property = value_as_property(module->slots[slot->second]);
      if (property && property->native_module_runtime) {
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          if (argc == 3 && exception_matches_builtin(runtime, pending, "AttributeError")) {
            value_assign_fast(out, args[2]);
            return true;
          }
          runtime.set_pending_exception(std::move(pending));
        } else {
          runtime.raise_class_error("RuntimeError", attr_error);
        }
        error = std::move(attr_error);
        return false;
      }
    }
    Value module_getattr;
    std::string getattr_error;
    if (module_get_attr(args[0], "__getattr__", module_getattr, getattr_error)) {
      Value attr_arg = Value::string(attr_name);
      Value dynamic_attr;
      std::string call_error;
      if (runtime_call_callable(runtime, module_getattr, &attr_arg, 1, dynamic_attr, call_error)) {
        value_assign_fast(out, dynamic_attr);
        return true;
      }
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (argc == 3 && exception_matches_builtin(runtime, pending, "AttributeError")) {
          value_assign_fast(out, args[2]);
          error.clear();
          return true;
        }
        runtime.set_pending_exception(std::move(pending));
      }
      error = std::move(call_error);
      return false;
    }
  }
  if (argc == 3) {
    value_assign_fast(out, args[2]);
    return true;
  }
  error = std::move(attr_error);
  runtime.raise_class_error("AttributeError", error);
  Value pending;
  if (runtime.take_pending_exception(pending)) {
    attach_attribute_error_context(runtime, pending, args[0], args[1]);
    runtime.set_pending_exception(std::move(pending));
  }
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
  if (auto* instance = value_as_instance(target)) {
    auto* klass = value_as_class(instance->klass);
    Value hook;
    if (klass && klass->has_setattr_hook &&
        object_get_class_attr_for_instance(target, "__setattr__", hook, error)) {
      return runtime_call_callable(runtime, hook, args, argc, out, error);
    }
    Value descriptor;
    std::string descriptor_error;
    if (object_get_class_attr_for_instance(target, string_object_to_string(*name), descriptor, descriptor_error)) {
      Value setter;
      if (attribute_get(descriptor, "__set__", setter, descriptor_error)) {
        Value setter_args[2] = {target, args[2]};
        return runtime_call_callable(runtime, setter, setter_args, 2, out, error);
      }
    }
  }
  if (!attribute_set(target, string_object_to_string(*name), args[2], error)) {
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_delattr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    return raise_type_error(runtime, "delattr() expected 2 arguments", error);
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    return raise_type_error(runtime, "delattr(): attribute name must be string", error);
  }
  Value target = args[0];
  if (auto* instance = value_as_instance(target)) {
    auto* klass = value_as_class(instance->klass);
    Value hook;
    if (klass && klass->has_delattr_hook &&
        object_get_class_attr_for_instance(target, "__delattr__", hook, error)) {
      return runtime_call_callable(runtime, hook, args, argc, out, error);
    }
    Value descriptor;
    std::string descriptor_error;
    if (object_get_class_attr_for_instance(target, string_object_to_string(*name), descriptor, descriptor_error)) {
      Value deleter;
      if (attribute_get(descriptor, "__delete__", deleter, descriptor_error)) {
        return runtime_call_callable(runtime, deleter, &target, 1, out, error);
      }
    }
  }
  if (!object_delete_attr(target, string_object_to_string(*name), error)) {
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
  Value attr_args[2];
  value_assign_fast(attr_args[0], args[0]);
  value_assign_fast(attr_args[1], args[1]);
  if (builtin_getattr(runtime, attr_args, 2, ignored, attr_error, nullptr)) {
    out = Value::boolean(true);
    return true;
  }
  Value pending;
  if (runtime.take_pending_exception(pending)) {
    if (exception_matches_builtin(runtime, pending, "AttributeError")) {
      out = Value::boolean(false);
      return true;
    }
    runtime.set_pending_exception(std::move(pending));
    error = std::move(attr_error);
    return false;
  }
  out = Value::boolean(false);
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
    if (entry.second < module.slots.size() && module.slots[entry.second].tag != ValueTag::Invalid) {
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
  Value dir_method;
  bool has_custom_dir = false;
  std::string dir_error;
  if (auto* klass = value_as_class(args[0])) {
    if (value_as_class(klass->metaclass) != nullptr) {
      has_custom_dir = class_get_bound_attr(
          runtime, klass->metaclass, args[0], "__dir__", dir_method, dir_error);
    }
  } else if (auto* instance = value_as_instance(args[0])) {
    if (value_as_class(instance->klass) != nullptr) {
      has_custom_dir = class_get_bound_attr(
          runtime, instance->klass, args[0], "__dir__", dir_method, dir_error);
    }
  } else if (auto* module = value_as_module(args[0])) {
    auto name = module->name_to_slot.find("__dir__");
    if (name != module->name_to_slot.end() && name->second < module->slots.size() &&
        module->slots[name->second].tag != ValueTag::Invalid) {
      value_assign_fast(dir_method, module->slots[name->second]);
      has_custom_dir = true;
    }
  }
  if (!dir_error.empty()) {
    error = std::move(dir_error);
    return false;
  }
  if (has_custom_dir) {
    Value result;
    if (!runtime_call_callable(runtime, dir_method, nullptr, 0, result, error)) {
      return false;
    }
    std::vector<Value> result_names;
    if (!collect_iterable(runtime, result, result_names, error)) {
      return false;
    }
    std::set<std::string> unique_names;
    for (const auto& result_name : result_names) {
      auto* string = value_as_string(result_name);
      if (string == nullptr) {
        return raise_type_error(runtime, "__dir__() must return an iterable of strings", error);
      }
      unique_names.insert(string_object_to_string(*string));
    }
    out = names_to_list(unique_names);
    return true;
  }
  std::set<std::string> names;
  const auto add_class_hierarchy_names = [&](const ClassObject* root, bool include_slots) {
    std::vector<const ClassObject*> pending{root};
    std::set<const ClassObject*> visited;
    while (!pending.empty()) {
      const ClassObject* current = pending.back();
      pending.pop_back();
      if (current == nullptr || !visited.insert(current).second) continue;
      for (const auto& attr : current->attrs) names.insert(attr.first);
      if (include_slots) {
        for (const auto& slot : current->instance_slot_names) names.insert(slot);
      }
      for (const auto& base : current->bases) {
        if (auto* base_class = value_as_class(base)) pending.push_back(base_class);
      }
    }
  };
  if (auto* module = value_as_module(args[0])) {
    for (const auto& entry : module->name_to_slot) {
      if (entry.second < module->slots.size() && module->slots[entry.second].tag != ValueTag::Invalid) {
        names.insert(entry.first);
      }
    }
    out = names_to_list(names);
    return true;
  }
  if (auto* klass = value_as_class(args[0])) {
    names.insert("__bases__");
    names.insert("__mro__");
    names.insert("__name__");
    add_class_hierarchy_names(klass, true);
    out = names_to_list(names);
    return true;
  }
  if (auto* instance = value_as_instance(args[0])) {
    names.insert("__class__");
    for (const auto& attr : instance->attrs) {
      names.insert(attr.first);
    }
    if (auto* klass = value_as_class(instance->klass)) {
      add_class_hierarchy_names(klass, true);
    }
    if (names.find("__text_signature__") != names.end()) {
      Value text_signature;
      std::string text_signature_error;
      if (!object_get_attr(args[0], "__text_signature__", text_signature, text_signature_error)) {
        names.erase("__text_signature__");
      }
    }
    out = names_to_list(names);
    return true;
  }
  if (value_as_function(args[0]) != nullptr || value_as_native_function(args[0]) != nullptr) {
    names.insert("__annotations__");
    names.insert("__call__");
    names.insert("__doc__");
    names.insert("__module__");
    names.insert("__name__");
    names.insert("__qualname__");
    names.insert("__text_signature__");
    out = names_to_list(names);
    return true;
  }
  if (value_as_bound_method(args[0]) != nullptr) {
    names.insert("__call__");
    names.insert("__doc__");
    names.insert("__func__");
    names.insert("__module__");
    names.insert("__name__");
    names.insert("__qualname__");
    names.insert("__self__");
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
    if (object_get_attr(args[0], "__dict__", out, error) && value_as_dict(out) != nullptr) {
      return true;
    }
    runtime.raise_class_error("TypeError", error.empty() ? "vars() argument must have __dict__ attribute" : error);
    return false;
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
  if (argc >= 4 && args[3].tag == ValueTag::Int64 && (args[3].as.i64 & 0x0400) != 0) {
    return compile_source_to_ast(runtime, source, filename_text, string_object_to_string(*mode), out, error);
  }
  int64_t flags = 0;
  if (argc >= 4 && args[3].tag == ValueTag::Int64) {
    flags = args[3].as.i64;
  }
  return compile_source_to_code(runtime, source, filename_text, string_object_to_string(*mode), flags, out, error);
}

bool builtin_compile_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  static constexpr const char* kArgNames[] = {
      "source",
      "filename",
      "mode",
      "flags",
      "dont_inherit",
      "optimize",
      "_feature_version",
  };
  if (argc > 7) {
    return raise_type_error(runtime, "compile() expected at most 7 arguments", error);
  }
  std::vector<Value> bound(7);
  std::vector<bool> provided(7, false);
  for (uint32_t i = 0; i < argc; ++i) {
    value_assign_fast(bound[i], args[i]);
    provided[i] = true;
  }
  auto set_keyword = [&](uint32_t index, const Value& value, const char* name) -> bool {
    if (provided[index]) {
      return raise_type_error(runtime, "compile() got multiple values for argument '" + std::string(name) + "'", error);
    }
    value_assign_fast(bound[index], value);
    provided[index] = true;
    return true;
  };
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      return raise_type_error(runtime, "compile() received invalid keyword argument", error);
    }
    bool matched = false;
    for (uint32_t arg_index = 0; arg_index < 7; ++arg_index) {
      if (std::string(kwargs[i].name) == kArgNames[arg_index]) {
        if (!set_keyword(arg_index, *kwargs[i].value, kArgNames[arg_index])) {
          return false;
        }
        matched = true;
        break;
      }
    }
    if (!matched) {
      return raise_type_error(runtime, "compile() got an unexpected keyword argument '" + std::string(kwargs[i].name) + "'", error);
    }
  }
  if (!provided[0] || !provided[1] || !provided[2]) {
    return raise_type_error(runtime, "compile() expected at least 3 arguments", error);
  }
  const uint32_t compile_argc = provided[3] ? 4u : 3u;
  return builtin_compile(runtime, bound.data(), compile_argc, out, error, nullptr);
}

bool builtin_exec(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data);

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
    if (!compile_source_to_code(runtime, source, "<string>", "eval", 0, code_value, error)) {
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
    return builtin_exec(runtime, args, argc, out, error, nullptr);
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
  const bool globals_is_dict = argc >= 2 && value_as_dict(args[1]) != nullptr;
  Value preserved_globals_name;
  value_set_invalid(preserved_globals_name);
  if (globals_is_dict) {
    std::string ignored;
    mapping_get_item(args[1], Value::string("__name__"), preserved_globals_name, ignored);
  } else if (argc >= 2 && value_as_module(args[1]) != nullptr) {
    std::string ignored;
    module_get_attr(args[1], "__name__", preserved_globals_name, ignored);
  }
  if (argc >= 2 && value_as_module(args[1]) == nullptr && !globals_is_dict && args[1].tag != ValueTag::None) {
    return raise_type_error(runtime, "exec() globals must be a dict or module", error);
  }
  const bool locals_is_dict = argc == 3 && value_as_dict(args[2]) != nullptr;
  const bool locals_is_module = argc == 3 && value_as_module(args[2]) != nullptr;
  if (argc == 3 && args[2].tag != ValueTag::None && !locals_is_dict && !locals_is_module) {
    return raise_type_error(runtime, "exec() locals must be a dict or module", error);
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
    if (!compile_source_to_code(runtime, source, "<string>", "exec", 0, code_value, error)) {
      return false;
    }
  }

  Value globals_module;
  if (globals_is_dict) {
    globals_module = Value::module("<exec>");
    module_set_attr(globals_module, "__name__", Value::string("<exec>"), error);
    if (!copy_dict_to_module(args[1], globals_module, error)) {
      return false;
    }
  } else {
    globals_module = argc >= 2 && value_as_module(args[1]) != nullptr ? args[1] : runtime.current_globals_module();
  }
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
  if (argc == 3 && args[2].tag != ValueTag::None) {
    Value exec_module = Value::module("<exec>");
    module_set_attr(exec_module, "__name__", Value::string("<exec>"), error);
    if (globals_is_dict && !copy_dict_to_module(args[1], exec_module, error)) {
      return false;
    }
    if (!globals_is_dict && value_as_module(args[1]) != nullptr) {
      if (!copy_module_to_module(args[1], exec_module, error)) {
        return false;
      }
    }
    if (locals_is_dict) {
      if (!copy_dict_to_module(args[2], exec_module, error)) {
        return false;
      }
    } else if (locals_is_module) {
      if (!copy_module_to_module(args[2], exec_module, error)) {
        return false;
      }
    }
    if (!run_code_object(runtime, *code, exec_module, out, error)) {
      return false;
    }
    if (preserved_globals_name.tag != ValueTag::Invalid) {
      module_set_attr(exec_module, "__name__", preserved_globals_name, error);
    }
    if (locals_is_dict) {
      Value locals_dict = args[2];
      return copy_module_to_dict(exec_module, locals_dict, error);
    }
    return true;
  }
  Value exec_module = Value::module("<exec>");
  module_set_attr(exec_module, "__name__", Value::string("<exec>"), error);
  Value globals_dict;
  if (globals_is_dict) {
    if (!copy_dict_to_module(args[1], exec_module, error)) {
      return false;
    }
  } else {
    if (!copy_module_to_module(globals_module, exec_module, error)) {
      return false;
    }
  }
  if (!run_code_object(runtime, *code, exec_module, out, error)) {
    return false;
  }
  if (preserved_globals_name.tag != ValueTag::Invalid) {
    module_set_attr(exec_module, "__name__", preserved_globals_name, error);
  }
  if (globals_is_dict) {
    Value globals_dict = args[1];
    rebind_exec_module_globals(exec_module, exec_module, globals_dict);
    return copy_module_to_dict(exec_module, globals_dict, error);
  }
  rebind_exec_module_globals(exec_module, globals_module);
  if (!copy_module_to_module(exec_module, globals_module, error)) {
    return false;
  }
  return true;
}

bool builtin_repr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "repr() expected 1 argument";
    return false;
  }
  if (auto* alias = value_as_generic_alias(args[0])) {
    auto* origin_class = value_as_class(alias->origin);
    auto* alias_args = value_as_tuple(alias->args);
    if (origin_class != nullptr && origin_class->name == "Union" && alias_args != nullptr) {
      std::string text;
      for (size_t i = 0; i < alias_args->items.size(); ++i) {
        if (i != 0) text += " | ";
        const auto& item = alias_args->items[i];
        if (auto* item_class = value_as_class(item)) {
          text += item_class->name == "NoneType" ? "None" : item_class->name;
        } else if (item.tag == ValueTag::None) {
          text += "None";
        } else {
          Value item_repr;
          if (!builtin_repr(runtime, &item, 1, item_repr, error, nullptr)) {
            return false;
          }
          auto* item_text = value_as_string(item_repr);
          if (item_text == nullptr) {
            return raise_type_error(runtime, "__repr__ returned non-string", error);
          }
          text += string_object_to_string(*item_text);
        }
      }
      out = Value::string(std::move(text));
      return true;
    }
  }
  if (auto* proxy = value_as_mapping_proxy(args[0])) {
    Value source_repr;
    if (!builtin_repr(runtime, &proxy->source, 1, source_repr, error, nullptr)) {
      return false;
    }
    auto* source_text = value_as_string(source_repr);
    if (source_text == nullptr) {
      return raise_type_error(runtime, "__repr__ returned non-string", error);
    }
    out = Value::string(
        "mappingproxy(" + string_object_to_string(*source_text) + ")");
    return true;
  }
  if (value_as_instance(args[0]) != nullptr) {
    Value repr_method;
    std::string attr_error;
    if (attribute_get(args[0], "__repr__", repr_method, attr_error)) {
      Value result;
      if (!runtime_call_callable(runtime, repr_method, nullptr, 0, result, error)) {
        return false;
      }
      auto* string = value_as_string(result);
      if (string == nullptr) {
        return raise_type_error(runtime, "__repr__ returned non-string", error);
      }
      out = result;
      return true;
    }
  }
  out = Value::string(value_to_repr(args[0]));
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
    Runtime& runtime,
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
  if (value_as_instance(args[0]) != nullptr) {
    Value method;
    if (object_get_attr(args[0], "__format__", method, error)) {
      Value spec_value = Value::string(spec);
      if (!runtime_call_callable(runtime, method, &spec_value, 1, out, error)) {
        return false;
      }
      if (value_as_string(out) == nullptr) {
        error = "__format__ must return a str";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      return true;
    }
    error.clear();
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

bool runtime_hash_value(Runtime& runtime, const Value& value, size_t& out, std::string& error) {
  if (value_as_instance(value) != nullptr) {
    Value hash_method;
    std::string attr_error;
    if (object_get_attr(value, "__hash__", hash_method, attr_error)) {
      if (hash_method.tag == ValueTag::None) {
        error = "unhashable type";
        return false;
      }
      Value hash_value;
      error.clear();
      if (!runtime_call_callable(runtime, hash_method, nullptr, 0, hash_value, error)) {
        return false;
      }
      if (hash_value.tag != ValueTag::Int64) {
        error = "__hash__ method should return an integer";
        return false;
      }
      out = static_cast<size_t>(hash_value.as.i64);
      return true;
    }
  }
  if (const auto* tuple = value_as_tuple(value)) {
    size_t hash = 0x345678ul;
    for (const auto& item : tuple->items) {
      size_t item_hash = 0;
      if (!runtime_hash_value(runtime, item, item_hash, error)) {
        return false;
      }
      hash = (hash ^ item_hash) * 1000003ul;
      hash ^= tuple->items.size();
    }
    out = hash == static_cast<size_t>(-1) ? static_cast<size_t>(-2) : hash;
    return true;
  }
  if (const auto* set = value_as_set(value); set != nullptr && set->frozen) {
    size_t hash = 0x2f4f0f1f0e0d0c0bull;
    for (const auto& item : set->items) {
      size_t item_hash = 0;
      if (!runtime_hash_value(runtime, item, item_hash, error)) {
        return false;
      }
      size_t shuffled = item_hash ^ (item_hash << 16) ^ static_cast<size_t>(89869747);
      shuffled *= static_cast<size_t>(3644798167u);
      hash ^= shuffled;
    }
    hash ^= set->items.size() * static_cast<size_t>(1927868237u);
    out = hash == static_cast<size_t>(-1) ? static_cast<size_t>(-2) : hash;
    return true;
  }
  return value_hash_key(value, out, error);
}

bool builtin_hash(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "hash() expected 1 argument", error);
  }
  size_t hash = 0;
  if (!runtime_hash_value(runtime, args[0], hash, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::int64(static_cast<int64_t>(hash));
  return true;
}

bool append_utf8_codepoint(int64_t codepoint, std::string& out) {
  if (codepoint < 0 || codepoint > 0x10ffff) {
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
  if (value_floor_div(args[0], args[1], quotient, error) &&
      value_mod(args[0], args[1], remainder, error)) {
    out = Value::tuple({quotient, remainder});
    return true;
  }
  error.clear();

  Value method;
  std::string attr_error;
  if (attribute_get(args[0], "__divmod__", method, attr_error)) {
    if (runtime_call_callable(runtime, method, &args[1], 1, out, error)) {
      return true;
    }
    return false;
  }
  attr_error.clear();
  if (attribute_get(args[1], "__rdivmod__", method, attr_error)) {
    if (runtime_call_callable(runtime, method, &args[0], 1, out, error)) {
      return true;
    }
    return false;
  }
  return raise_type_error(runtime, "unsupported operand type(s) for divmod()", error);
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
  if (!runtime_get_iter(runtime, args[0], iterator, error)) {
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
  if (!runtime_get_iter(runtime, args[0], iterator, error)) {
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

bool builtin_template_interpolation(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 4) {
    return raise_type_error(runtime, "template interpolation expects 4 arguments", error);
  }
  const Value* interpolation_type = runtime.find_builtin("__xlang3_template_interpolation_type__");
  if (interpolation_type == nullptr) {
    error = "template interpolation type is unavailable";
    return false;
  }
  out = Value::instance(*interpolation_type);
  return object_set_attr(out, "value", args[0], error) &&
         object_set_attr(out, "expression", args[1], error) &&
         object_set_attr(out, "conversion", args[2], error) &&
         object_set_attr(out, "format_spec", args[3], error);
}

bool builtin_template_iter(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    return raise_type_error(runtime, "Template.__iter__ expected no arguments", error);
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    return raise_type_error(runtime, "Template.__iter__ expected a Template", error);
  }
  return runtime_get_iter(runtime, instance->sequence_storage, out, error);
}

bool builtin_template_literal(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2 || value_as_tuple(args[0]) == nullptr || value_as_tuple(args[1]) == nullptr) {
    return raise_type_error(runtime, "template literal expects string and interpolation tuples", error);
  }
  const Value* template_type = runtime.find_builtin("__xlang3_template_type__");
  if (template_type == nullptr) {
    error = "template type is unavailable";
    return false;
  }
  const auto* strings = value_as_tuple(args[0]);
  const auto* interpolations = value_as_tuple(args[1]);
  std::vector<Value> values;
  std::vector<Value> parts;
  values.reserve(interpolations->items.size());
  parts.reserve(strings->items.size() + interpolations->items.size());
  for (size_t index = 0; index < strings->items.size(); ++index) {
    parts.push_back(strings->items[index]);
    if (index < interpolations->items.size()) {
      const Value& interpolation = interpolations->items[index];
      parts.push_back(interpolation);
      Value value;
      if (!object_get_attr(interpolation, "value", value, error)) {
        return false;
      }
      values.push_back(std::move(value));
    }
  }
  out = Value::instance(*template_type);
  auto* instance = value_as_instance(out);
  instance->sequence_storage = Value::tuple(std::move(parts));
  return object_set_attr(out, "strings", args[0], error) &&
         object_set_attr(out, "interpolations", args[1], error) &&
         object_set_attr(out, "values", Value::tuple(std::move(values)), error);
}

} // namespace

void register_functional_builtins(Runtime& runtime) {
  if (const auto* string_type = runtime.find_builtin("str")) {
    runtime.register_builtin("__xlang3_fstring_str__", *string_type);
  }
  const Value* object_type = runtime.find_builtin("object");
  const Value* type_type = runtime.find_builtin("type");
  if (object_type != nullptr && type_type != nullptr) {
    Value interpolation_type = Value::class_object(
        "Interpolation",
        {{"__module__", Value::string("string.templatelib")}},
        *object_type,
        {},
        *type_type);
    Value template_type = Value::class_object(
        "Template",
        {{"__module__", Value::string("string.templatelib")},
         {"__iter__", runtime.make_native_function("Template.__iter__", builtin_template_iter)}},
        *object_type,
        {},
        *type_type);
    runtime.register_builtin("__xlang3_template_interpolation_type__", std::move(interpolation_type));
    runtime.register_builtin("__xlang3_template_type__", std::move(template_type));
  }
  runtime.register_native_builtin("__xlang3_template_interpolation__", builtin_template_interpolation);
  runtime.register_native_builtin("__xlang3_template_literal__", builtin_template_literal);
  runtime.register_native_builtin("__xlang3_binary_or__", builtin_binary_or);
  runtime.register_native_builtin("__xlang3_inplace_or__", builtin_inplace_or);
  runtime.register_native_builtin("__xlang3_inplace_add__", builtin_inplace_add);
  runtime.register_native_builtin("_identity", builtin_identity);
  runtime.register_native_builtin("super", builtin_super);
  runtime.register_native_builtin("callable", builtin_callable);
  runtime.register_native_builtin("enumerate", builtin_enumerate, nullptr, false, builtin_enumerate_kw);
  runtime.register_native_builtin("zip", builtin_zip, nullptr, false, builtin_zip_kw);
  runtime.register_native_builtin("reversed", builtin_reversed);
  runtime.register_native_builtin("map", builtin_map);
  runtime.register_native_builtin("filter", builtin_filter);
  runtime.register_native_builtin("sum", builtin_sum);
  runtime.register_native_builtin("sorted", builtin_sorted, nullptr, false, builtin_sorted_kw);
  runtime.register_native_builtin("min", builtin_min, nullptr, false, builtin_min_kw);
  runtime.register_native_builtin("max", builtin_max, nullptr, false, builtin_max_kw);
  runtime.register_native_builtin("abs", builtin_abs);
  runtime.register_native_builtin("round", builtin_round);
  runtime.register_native_builtin("repr", builtin_repr);
  runtime.register_native_builtin("__xlang3_fstring_repr__", builtin_repr);
  runtime.register_native_builtin("format", builtin_format);
  runtime.register_native_builtin("__xlang3_fstring_format__", builtin_format);
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
  runtime.register_native_builtin("delattr", builtin_delattr);
  runtime.register_native_builtin("hasattr", builtin_hasattr);
  runtime.register_native_builtin("dir", builtin_dir);
  runtime.register_native_builtin("vars", builtin_vars);
  runtime.register_native_builtin("globals", builtin_globals);
  runtime.register_native_builtin("locals", builtin_locals);
  runtime.register_native_builtin("compile", builtin_compile, nullptr, false, builtin_compile_kw);
  runtime.register_native_builtin("eval", builtin_eval);
  runtime.register_native_builtin("exec", builtin_exec);
}

} // namespace xlang3
