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

#include "xlang3/functional_iterators.h"
#include "xlang3/ast.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xlang3 {

namespace {

struct AstState {
  Runtime* runtime = nullptr;
  Value ast_base;
  Value load_singleton;
  std::unordered_map<std::string, Value> classes;
  // Views refer to immutable keys in classes; unordered-map rehash preserves
  // their addresses. Retained class Values also prevent identity reuse while
  // the lookup is installed on this Runtime.
  std::unordered_map<const ClassObject*, std::string_view> kinds_by_class;
};

std::vector<Value> field_tuple(std::initializer_list<const char*> names) {
  std::vector<Value> fields;
  fields.reserve(names.size());
  for (const char* name : names) {
    fields.push_back(Value::string(name));
  }
  return fields;
}

std::vector<std::string> fields_for(const Value& node) {
  Value fields_value;
  std::string ignored;
  if (!object_get_attr(node, "_fields", fields_value, ignored)) {
    return {};
  }
  auto* tuple = value_as_tuple(fields_value);
  if (tuple == nullptr) {
    return {};
  }
  std::vector<std::string> fields;
  fields.reserve(tuple->items.size());
  for (const auto& item : tuple->items) {
    if (auto* string = value_as_string(item)) {
      fields.push_back(string_object_to_string(*string));
    }
  }
  return fields;
}

bool ast_constructor_error(Runtime& runtime, const char* type,
                           std::string message, std::string& error) {
  error = std::move(message);
  runtime.raise_class_error(type, error);
  return false;
}

bool ast_constructor_warning(Runtime& runtime, const std::string& message, std::string& error) {
  const Value* category = runtime.find_builtin("DeprecationWarning");
  return category != nullptr && runtime_warn(runtime, Value::string(message), *category, 2, error);
}

bool ast_constructor_fields(Runtime& runtime, const Value& klass, const char* name,
                            std::vector<std::string>& out, std::string& error) {
  Value fields;
  if (!runtime_getattr(runtime, klass, Value::string(name), fields, error)) return false;
  const auto append_fields = [&](const auto& items) {
    for (const auto& item : items) {
      auto* text = value_as_string(item);
      if (text == nullptr) return ast_constructor_error(
          runtime, "TypeError", "AST field names must be strings", error);
      out.push_back(string_object_to_string(*text));
    }
    return true;
  };
  if (auto* tuple = value_as_tuple(fields)) return append_fields(tuple->items);
  if (auto* list = value_as_list(fields)) return append_fields(list->items);
  return ast_constructor_error(runtime, "TypeError", "AST field names must be a sequence", error);
}

bool ast_node_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1) {
    error = "AST.__init__() missing self";
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  auto* instance = value_as_instance(self);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr) return ast_constructor_error(runtime, "TypeError", "AST constructor requires an AST instance", error);
  std::vector<std::string> fields;
  if (!ast_constructor_fields(runtime, instance->klass, "_fields", fields, error)) return false;
  std::unordered_set<std::string> remaining(fields.begin(), fields.end());
  if (argc - 1 > fields.size()) {
    return ast_constructor_error(runtime, "TypeError", "AST constructor got too many positional arguments", error);
  }
  for (uint32_t i = 1; i < argc; ++i) {
    if (!runtime_setattr(runtime, self, Value::string(fields[i - 1]), args[i], error)) {
      return false;
    }
    remaining.erase(fields[i - 1]);
  }
  std::vector<std::string> attributes;
  bool loaded_attributes = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    const std::string name(kwargs[i].name);
    if (std::find(fields.begin(), fields.end(), name) != fields.end()) {
      if (remaining.erase(name) == 0) return ast_constructor_error(
          runtime, "TypeError", klass->name + " got multiple values for argument '" + name + "'", error);
    } else {
      if (!loaded_attributes) {
        if (!ast_constructor_fields(runtime, instance->klass, "_attributes", attributes, error)) return false;
        loaded_attributes = true;
      }
      if (std::find(attributes.begin(), attributes.end(), name) == attributes.end() &&
          !ast_constructor_warning(runtime, klass->name + ".__init__ got an unexpected keyword argument '" + name +
              "'. Support for arbitrary keyword arguments is deprecated and will be removed in Python 3.15.", error)) return false;
    }
    if (!runtime_setattr(runtime, self, Value::string(name), *kwargs[i].value, error)) {
      return false;
    }
  }
  if (!remaining.empty()) {
    const Value absent = Value::invalid();
    Value types;
    if (!runtime_getattr(runtime, instance->klass, Value::string("_field_types"), types, error, &absent)) return false;
    if (types.tag != ValueTag::Invalid) {
      if (value_as_dict(types) == nullptr) return ast_constructor_error(
          runtime, "SystemError", "bad argument to internal function", error);
      auto* state = static_cast<AstState*>(user_data);
      for (const auto& name : fields) {
        if (remaining.erase(name) == 0) continue;
        Value type;
        if (!mapping_get_string_item(types, name, type, error)) {
          error.clear();
          if (!ast_constructor_warning(runtime, "Field '" + name + "' is missing from " + klass->name +
              "._field_types. This will become an error in Python 3.15.", error)) return false;
          continue;
        }
        auto* alias = value_as_generic_alias(type);
        if (alias != nullptr && alias->is_union) continue;  // Optional defaults live on the class.
        if (alias != nullptr) {
          // Lists belong to each constructor invocation. Sharing a class-level
          // empty list would leak mutations between unrelated AST nodes.
          if (!runtime_setattr(runtime, self, Value::string(name), Value::list({}), error)) return false;
        } else if (value_is(type, state->classes.at("expr_context"))) {
          if (!runtime_setattr(runtime, self, Value::string(name), state->load_singleton, error)) return false;
        } else if (!ast_constructor_warning(runtime, klass->name + ".__init__ missing 1 required positional argument: '" +
                       name + "'. This will become an error in Python 3.15.", error)) return false;
      }
    }
  }
  value_set_none(out);
  return true;
}

bool ast_node_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return ast_node_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

Value ast_class(Runtime& runtime, const char* name, const Value& base, std::initializer_list<const char*> fields = {}, AstState* state = nullptr) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"_fields", Value::tuple(field_tuple(fields))});
  const std::string_view class_name(name);
  if (class_name == "stmt" || class_name == "expr" || class_name == "excepthandler" ||
      class_name == "pattern" || class_name == "type_param") {
    attrs.push_back({"_attributes", Value::tuple(field_tuple(
        {"lineno", "col_offset", "end_lineno", "end_col_offset"}))});
    attrs.push_back({"end_lineno", Value::none()});
    attrs.push_back({"end_col_offset", Value::none()});
  } else if (class_name == "AST") {
    attrs.push_back({"_attributes", Value::tuple({})});
  }
  attrs.push_back({"__match_args__", Value::tuple(field_tuple(fields))});
  if (std::string(name) == "AST") {
    attrs.push_back({"__init__", runtime.make_native_function("_ast.AST.__init__", ast_node_init, state, nullptr, nullptr, false, ast_node_init_kw)});
  }
  return Value::class_object(name, std::move(attrs), base);
}

Value node_class(AstState* state, const char* name) {
  auto it = state->classes.find(name);
  return it == state->classes.end() ? Value::invalid() : it->second;
}

Value ast_instance(AstState* state, const char* name) {
  Value klass = node_class(state, name);
  if (klass.tag == ValueTag::Invalid) {
    return Value::invalid();
  }
  return Value::instance(klass);
}

Value make_empty_arguments(AstState* state, std::string& error) {
  Value args = ast_instance(state, "arguments");
  if (args.tag == ValueTag::Invalid) {
    return args;
  }
  object_set_attr(args, "posonlyargs", Value::list({}), error);
  object_set_attr(args, "args", Value::list({}), error);
  object_set_attr(args, "vararg", Value::none(), error);
  object_set_attr(args, "kwonlyargs", Value::list({}), error);
  object_set_attr(args, "kw_defaults", Value::list({}), error);
  object_set_attr(args, "kwarg", Value::none(), error);
  object_set_attr(args, "defaults", Value::list({}), error);
  return args;
}

std::string_view ast_trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

bool ast_parse_decimal(std::string_view text, int64_t& out) {
  text = ast_trim(text);
  if (text.empty()) {
    return false;
  }
  int64_t sign = 1;
  if (text.front() == '+' || text.front() == '-') {
    sign = text.front() == '-' ? -1 : 1;
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return false;
  }
  int64_t value = 0;
  for (char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
    value = value * 10 + (ch - '0');
  }
  out = sign * value;
  return true;
}

Value ast_make_load(AstState* state) {
  return ast_instance(state, "Load");
}

Value ast_make_constant(AstState* state, Value value, std::string& error) {
  Value constant = ast_instance(state, "Constant");
  if (constant.tag == ValueTag::Invalid) {
    error = "missing _ast Constant class";
    return constant;
  }
  object_set_attr(constant, "value", value, error);
  object_set_attr(constant, "kind", Value::none(), error);
  return constant;
}

Value ast_make_name(AstState* state, std::string_view name, std::string& error) {
  Value node = ast_instance(state, "Name");
  if (node.tag == ValueTag::Invalid) {
    error = "missing _ast Name class";
    return node;
  }
  object_set_attr(node, "id", Value::string(std::string(name)), error);
  object_set_attr(node, "ctx", ast_make_load(state), error);
  return node;
}

Value ast_make_arg(AstState* state, std::string_view name, std::string& error) {
  Value node = ast_instance(state, "arg");
  if (node.tag == ValueTag::Invalid) {
    error = "missing _ast arg class";
    return node;
  }
  object_set_attr(node, "arg", Value::string(std::string(ast_trim(name))), error);
  object_set_attr(node, "annotation", Value::none(), error);
  object_set_attr(node, "type_comment", Value::none(), error);
  return node;
}

void ast_set_location(
    Value& node,
    uint32_t line,
    uint32_t end_line,
    uint32_t column,
    uint32_t end_column,
    std::string& error) {
  object_set_attr(node, "lineno", Value::int64(line), error);
  object_set_attr(node, "end_lineno", Value::int64(end_line), error);
  object_set_attr(node, "col_offset", Value::int64(column), error);
  object_set_attr(node, "end_col_offset", Value::int64(end_column), error);
}

Value ast_parse_simple_expr(
    AstState* state,
    std::string_view source,
    std::string& error,
    uint32_t source_line = 1,
    uint32_t column_offset = 0) {
  const std::string_view untrimmed_source = source;
  source = ast_trim(source);
  column_offset += static_cast<uint32_t>(source.data() - untrimmed_source.data());
  const auto find_top_level_operator = [&](std::string_view operators) {
    size_t found = std::string_view::npos;
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < source.size(); ++i) {
      const char ch = source[i];
      if (quote != '\0') {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == quote) quote = '\0';
        continue;
      }
      if (ch == '\'' || ch == '"') {
        quote = ch;
      } else if (ch == '(' || ch == '[' || ch == '{') {
        ++depth;
      } else if (ch == ')' || ch == ']' || ch == '}') {
        --depth;
      } else if (depth == 0 && operators.find(ch) != std::string_view::npos && i != 0) {
        found = i;
      }
    }
    return found;
  };
  size_t binary = find_top_level_operator("|");
  if (binary == std::string_view::npos) {
    binary = find_top_level_operator("+-");
  }
  if (binary == std::string_view::npos) {
    binary = find_top_level_operator("*/%@");
  }
  if (binary != std::string_view::npos) {
    size_t operator_width = 1;
    if (binary > 0 && source[binary] == source[binary - 1] &&
        (source[binary] == '*' || source[binary] == '/')) {
      --binary;
      operator_width = 2;
    }
    Value left = ast_parse_simple_expr(state, source.substr(0, binary), error, source_line, column_offset);
    Value right = ast_parse_simple_expr(
        state, source.substr(binary + operator_width), error, source_line,
        column_offset + static_cast<uint32_t>(binary + operator_width));
    Value binop = ast_instance(state, "BinOp");
    if (left.tag == ValueTag::Invalid || right.tag == ValueTag::Invalid || binop.tag == ValueTag::Invalid) {
      error = "missing _ast BinOp class";
      return Value::invalid();
    }
    object_set_attr(binop, "left", left, error);
    const std::string_view op = source.substr(binary, operator_width);
    const char* op_class =
        op == "|" ? "BitOr" :
        op == "+" ? "Add" :
        op == "-" ? "Sub" :
        op == "*" ? "Mult" :
        op == "**" ? "Pow" :
        op == "/" ? "Div" :
        op == "//" ? "FloorDiv" :
        op == "%" ? "Mod" : "MatMult";
    object_set_attr(
        binop,
        "op",
        ast_instance(state, op_class),
        error);
    object_set_attr(binop, "right", right, error);
    ast_set_location(
        binop, source_line, source_line, column_offset,
        column_offset + static_cast<uint32_t>(source.size()), error);
    return binop;
  }
  if (source.size() >= 2 && source.front() == '[' && source.back() == ']') {
    std::vector<Value> elements;
    const std::string_view contents = source.substr(1, source.size() - 2);
    size_t start = 0;
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i <= contents.size(); ++i) {
      const char ch = i < contents.size() ? contents[i] : ',';
      if (quote != '\0') {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == quote) quote = '\0';
      } else if (ch == '\'' || ch == '"') {
        quote = ch;
      } else if (ch == '(' || ch == '[' || ch == '{') {
        ++depth;
      } else if (ch == ')' || ch == ']' || ch == '}') {
        --depth;
      } else if (ch == ',' && depth == 0) {
        const auto item = ast_trim(contents.substr(start, i - start));
        if (!item.empty()) {
          Value element = ast_parse_simple_expr(
              state, item, error, source_line,
              column_offset + 1 + static_cast<uint32_t>(item.data() - contents.data()));
          if (element.tag == ValueTag::Invalid) return Value::invalid();
          elements.push_back(std::move(element));
        }
        start = i + 1;
      }
    }
    Value list = ast_instance(state, "List");
    if (list.tag == ValueTag::Invalid) {
      error = "missing _ast List class";
      return Value::invalid();
    }
    object_set_attr(list, "elts", Value::list(std::move(elements)), error);
    object_set_attr(list, "ctx", ast_make_load(state), error);
    ast_set_location(
        list, source_line, source_line, column_offset,
        column_offset + static_cast<uint32_t>(source.size()), error);
    return list;
  }
  if (source.size() >= 2 && source.front() == '{' && source.back() == '}' &&
      source.find(':') == std::string_view::npos) {
    std::vector<Value> elements;
    const std::string_view contents = source.substr(1, source.size() - 2);
    size_t start = 0;
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i <= contents.size(); ++i) {
      const char ch = i < contents.size() ? contents[i] : ',';
      if (quote != '\0') {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == quote) quote = '\0';
      } else if (ch == '\'' || ch == '"') {
        quote = ch;
      } else if (ch == '(' || ch == '[' || ch == '{') {
        ++depth;
      } else if (ch == ')' || ch == ']' || ch == '}') {
        --depth;
      } else if (ch == ',' && depth == 0) {
        const auto item = ast_trim(contents.substr(start, i - start));
        if (!item.empty()) {
          Value element = ast_parse_simple_expr(
              state, item, error, source_line,
              column_offset + 1 + static_cast<uint32_t>(item.data() - contents.data()));
          if (element.tag == ValueTag::Invalid) return Value::invalid();
          elements.push_back(std::move(element));
        }
        start = i + 1;
      }
    }
    Value set = ast_instance(state, "Set");
    if (set.tag == ValueTag::Invalid) {
      error = "missing _ast Set class";
      return Value::invalid();
    }
    object_set_attr(set, "elts", Value::list(std::move(elements)), error);
    ast_set_location(
        set, source_line, source_line, column_offset,
        column_offset + static_cast<uint32_t>(source.size()), error);
    return set;
  }
  if (!source.empty() && source.back() == ']') {
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    char quote = '\0';
    bool escaped = false;
    size_t subscript_open = std::string_view::npos;
    for (size_t i = 0; i < source.size(); ++i) {
      const char ch = source[i];
      if (quote != '\0') {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == quote) quote = '\0';
        continue;
      }
      if (ch == '\'' || ch == '"') {
        quote = ch;
      } else if (ch == '(') {
        ++paren_depth;
      } else if (ch == ')') {
        --paren_depth;
      } else if (ch == '{') {
        ++brace_depth;
      } else if (ch == '}') {
        --brace_depth;
      } else if (ch == '[') {
        if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
          subscript_open = i;
        }
        ++bracket_depth;
      } else if (ch == ']') {
        --bracket_depth;
      }
    }
    if (subscript_open != std::string_view::npos && subscript_open > 0 &&
        bracket_depth == 0) {
      Value base = ast_parse_simple_expr(
          state, source.substr(0, subscript_open), error, source_line, column_offset);
      Value slice = ast_parse_simple_expr(
          state, source.substr(subscript_open + 1, source.size() - subscript_open - 2),
          error, source_line,
          column_offset + static_cast<uint32_t>(subscript_open + 1));
      Value subscript = ast_instance(state, "Subscript");
      if (base.tag != ValueTag::Invalid && slice.tag != ValueTag::Invalid &&
          subscript.tag != ValueTag::Invalid) {
        object_set_attr(subscript, "value", base, error);
        object_set_attr(subscript, "slice", slice, error);
        object_set_attr(subscript, "ctx", ast_make_load(state), error);
        ast_set_location(
            subscript, source_line, source_line, column_offset,
            column_offset + static_cast<uint32_t>(source.size()), error);
        return subscript;
      }
    }
  }
  if (!source.empty() && source.back() == ')') {
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    size_t call_open = std::string_view::npos;
    for (size_t i = 0; i < source.size(); ++i) {
      const char ch = source[i];
      if (quote != '\0') {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == quote) quote = '\0';
        continue;
      }
      if (ch == '\'' || ch == '"') {
        quote = ch;
      } else if (ch == '(') {
        if (depth == 0) call_open = i;
        ++depth;
      } else if (ch == ')') {
        --depth;
      }
    }
    if (call_open != std::string_view::npos && call_open > 0 && depth == 0) {
      // This shortcut only represents an empty call. Preserve that cheap path,
      // but let the full parser handle argument syntax instead of silently
      // dropping it: Python AST visitors and template compilers depend on it.
      if (!ast_trim(source.substr(call_open + 1, source.size() - call_open - 2)).empty()) {
        return Value::invalid();
      }
      Value function = ast_parse_simple_expr(
          state, source.substr(0, call_open), error, source_line, column_offset);
      if (function.tag == ValueTag::Invalid) {
        return Value::invalid();
      }
      Value call = ast_instance(state, "Call");
      if (function.tag != ValueTag::Invalid && call.tag != ValueTag::Invalid) {
        object_set_attr(call, "func", function, error);
        object_set_attr(call, "args", Value::list({}), error);
        object_set_attr(call, "keywords", Value::list({}), error);
        ast_set_location(
            call, source_line, source_line, column_offset,
            column_offset + static_cast<uint32_t>(source.size()), error);
        return call;
      }
    }
  }
  int64_t integer = 0;
  if (ast_parse_decimal(source, integer)) {
    Value constant = ast_make_constant(state, Value::int64(integer), error);
    ast_set_location(
        constant, source_line, source_line, column_offset,
        column_offset + static_cast<uint32_t>(source.size()), error);
    return constant;
  }
  if (!source.empty() &&
      ((source.front() == '"' && source.back() == '"') || (source.front() == '\'' && source.back() == '\''))) {
    std::string decoded;
    const auto quoted = source.substr(1, source.size() - 2);
    decoded.reserve(quoted.size());
    for (size_t i = 0; i < quoted.size(); ++i) {
      if (quoted[i] != '\\' || i + 1 >= quoted.size()) {
        decoded.push_back(quoted[i]);
        continue;
      }
      const char escaped_char = quoted[++i];
      switch (escaped_char) {
        case '\\': decoded.push_back('\\'); break;
        case '\'': decoded.push_back('\''); break;
        case '"': decoded.push_back('"'); break;
        case 'n': decoded.push_back('\n'); break;
        case 'r': decoded.push_back('\r'); break;
        case 't': decoded.push_back('\t'); break;
        default:
          decoded.push_back('\\');
          decoded.push_back(escaped_char);
          break;
      }
    }
    Value constant = ast_make_constant(state, Value::string(std::move(decoded)), error);
    ast_set_location(
        constant, source_line, source_line, column_offset,
        column_offset + static_cast<uint32_t>(source.size()), error);
    return constant;
  }
  // Keyword literals are constants even if their introspection entries in
  // builtins are rebound. Keep this scalar path independent of name lookup.
  if (source == "None" || source == "False" || source == "True") {
    Value constant = ast_make_constant(
        state, source == "None" ? Value::none() : Value::boolean(source == "True"), error);
    ast_set_location(
        constant, source_line, source_line, column_offset,
        column_offset + static_cast<uint32_t>(source.size()), error);
    return constant;
  }
  bool is_identifier = !source.empty() &&
      (std::isalpha(static_cast<unsigned char>(source.front())) || source.front() == '_');
  for (char ch : source) {
    is_identifier = is_identifier &&
        (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_');
  }
  if (is_identifier) {
    Value name = ast_make_name(state, source, error);
    ast_set_location(
        name, source_line, source_line, column_offset,
        column_offset + static_cast<uint32_t>(source.size()), error);
    return name;
  }
  error = "unsupported _ast parse expression";
  return Value::invalid();
}

bool parse_simple_expression_module_ast(
    Runtime&,
    AstState* state,
    std::string_view source,
    Value& out,
    std::string& error) {
  std::string_view expression = ast_trim(source);
  uint32_t source_line = 1;
  uint32_t column_offset = 0;
  if (expression.size() >= 2 && expression.front() == '(' && expression.back() == ')') {
    expression.remove_prefix(1);
    expression.remove_suffix(1);
    size_t first = 0;
    while (first < expression.size() && std::isspace(static_cast<unsigned char>(expression[first]))) {
      if (expression[first] == '\n') {
        ++source_line;
        column_offset = 0;
      } else {
        ++column_offset;
      }
      ++first;
    }
    expression.remove_prefix(first);
    expression = ast_trim(expression);
  }
  if (expression.empty() || expression.find('\n') != std::string_view::npos) {
    return false;
  }
  // traceback wraps a physical source line in parentheses before asking AST
  // for expression anchors.  Flow statements remain invalid in that form;
  // accepting their trailing call as an expression would draw misleading
  // carets across the entire return/raise statement.
  if (expression.rfind("return ", 0) == 0 ||
      expression.rfind("raise ", 0) == 0) {
    return false;
  }
  Value value = ast_parse_simple_expr(state, expression, error, source_line, column_offset);
  if (value.tag == ValueTag::Invalid) {
    return false;
  }
  Value statement = ast_instance(state, "Expr");
  Value module = ast_instance(state, "Module");
  if (statement.tag == ValueTag::Invalid || module.tag == ValueTag::Invalid) {
    return false;
  }
  object_set_attr(statement, "value", value, error);
  ast_set_location(
      statement, source_line, source_line, column_offset,
      column_offset + static_cast<uint32_t>(expression.size()), error);
  object_set_attr(module, "body", Value::list({statement}), error);
  object_set_attr(module, "type_ignores", Value::list({}), error);
  value_assign_fast(out, module);
  return true;
}

bool parse_simple_flow_statement_ast(
    AstState* state,
    std::string_view source,
    Value& out,
    std::string& error) {
  size_t leading = 0;
  while (leading < source.size() &&
         (source[leading] == ' ' || source[leading] == '\t')) {
    ++leading;
  }
  source.remove_prefix(leading);
  const char* node_name = nullptr;
  size_t keyword_size = 0;
  if (source.rfind("return ", 0) == 0) {
    node_name = "Return";
    keyword_size = 7;
  } else if (source.rfind("raise ", 0) == 0) {
    node_name = "Raise";
    keyword_size = 6;
  } else {
    return false;
  }
  source = ast_trim(source);
  if (source.empty() || source.find('\n') != std::string_view::npos) {
    return false;
  }
  Value value = ast_parse_simple_expr(
      state, source.substr(keyword_size), error, 1,
      static_cast<uint32_t>(leading + keyword_size));
  Value statement = ast_instance(state, node_name);
  Value module = ast_instance(state, "Module");
  if (value.tag == ValueTag::Invalid || statement.tag == ValueTag::Invalid ||
      module.tag == ValueTag::Invalid) {
    return false;
  }
  if (std::string_view(node_name) == "Return") {
    object_set_attr(statement, "value", value, error);
  } else {
    object_set_attr(statement, "exc", value, error);
    object_set_attr(statement, "cause", Value::none(), error);
  }
  ast_set_location(
      statement, 1, 1, static_cast<uint32_t>(leading),
      static_cast<uint32_t>(leading + source.size()), error);
  object_set_attr(module, "body", Value::list({statement}), error);
  object_set_attr(module, "type_ignores", Value::list({}), error);
  value_assign_fast(out, module);
  return true;
}

bool parse_simple_module_ast(Runtime&, AstState* state, std::string_view source, Value& out, std::string& error) {
  const size_t newline = source.find('\n');
  std::string_view first_line = ast_trim(source.substr(0, newline));
  if (first_line.empty() || first_line.rfind("def ", 0) == 0) {
    return false;
  }
  const size_t equals = first_line.find('=');
  if (equals == std::string_view::npos || first_line.find('=', equals + 1) != std::string_view::npos) {
    return false;
  }
  std::string_view name = ast_trim(first_line.substr(0, equals));
  std::string_view rhs = first_line.substr(equals + 1);
  bool valid_name = !name.empty() &&
      (std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_');
  for (char ch : name) {
    valid_name = valid_name && (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_');
  }
  if (!valid_name) {
    return false;
  }
  Value module = ast_instance(state, "Module");
  Value assign = ast_instance(state, "Assign");
  Value target = ast_make_name(state, name, error);
  Value value = ast_parse_simple_expr(state, rhs, error);
  if (module.tag == ValueTag::Invalid || assign.tag == ValueTag::Invalid ||
      target.tag == ValueTag::Invalid || value.tag == ValueTag::Invalid) {
    return false;
  }
  object_set_attr(assign, "targets", Value::list({target}), error);
  object_set_attr(assign, "value", value, error);
  object_set_attr(assign, "type_comment", Value::none(), error);
  object_set_attr(module, "body", Value::list({assign}), error);
  object_set_attr(module, "type_ignores", Value::list({}), error);
  value_assign_fast(out, module);
  return true;
}

bool parse_simple_function_ast(Runtime&, AstState* state, std::string_view source, Value& out, std::string& error) {
  std::string_view text = source;
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  if (text.substr(0, 4) != "def ") {
    return false;
  }
  text.remove_prefix(4);
  size_t name_end = 0;
  while (name_end < text.size() &&
         (std::isalnum(static_cast<unsigned char>(text[name_end])) || text[name_end] == '_')) {
    ++name_end;
  }
  if (name_end == 0 || name_end >= text.size() || text[name_end] != '(') {
    return false;
  }
  std::string name(text.substr(0, name_end));
  size_t close = text.rfind(')');
  if (close == std::string_view::npos) {
    return false;
  }
  std::string_view params = text.substr(name_end + 1, close - name_end - 1);

  Value module = ast_instance(state, "Module");
  Value function = ast_instance(state, "FunctionDef");
  Value pass = ast_instance(state, "Pass");
  if (module.tag == ValueTag::Invalid || function.tag == ValueTag::Invalid || pass.tag == ValueTag::Invalid) {
    error = "missing _ast class";
    return false;
  }
  Value args = make_empty_arguments(state, error);
  if (args.tag == ValueTag::Invalid) {
    error = "missing _ast arguments class";
    return false;
  }

  std::vector<std::string> pieces;
  size_t piece_start = 0;
  int depth = 0;
  char quote = '\0';
  bool escaped = false;
  for (size_t i = 0; i <= params.size(); ++i) {
    const char ch = i < params.size() ? params[i] : ',';
    if (quote != '\0') {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        quote = '\0';
      }
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
      continue;
    }
    if (ch == '(' || ch == '[' || ch == '{') {
      ++depth;
      continue;
    }
    if (ch == ')' || ch == ']' || ch == '}') {
      --depth;
      continue;
    }
    if (ch == ',' && depth == 0) {
      auto piece = ast_trim(params.substr(piece_start, i - piece_start));
      if (!piece.empty()) {
        pieces.emplace_back(piece);
      }
      piece_start = i + 1;
    }
  }

  struct ParsedParameter {
    Value node;
    Value default_value;
  };
  std::vector<ParsedParameter> positional;
  std::vector<Value> posonly;
  std::vector<Value> regular;
  std::vector<Value> kwonly;
  std::vector<Value> kw_defaults;
  Value vararg = Value::none();
  Value kwarg = Value::none();
  bool keyword_only = false;
  bool saw_slash = false;
  size_t positional_only_count = 0;

  auto split_default = [](std::string_view piece, std::string_view& parameter_name,
                          std::string_view& default_source) {
    int nested = 0;
    char nested_quote = '\0';
    bool nested_escaped = false;
    for (size_t i = 0; i < piece.size(); ++i) {
      const char ch = piece[i];
      if (nested_quote != '\0') {
        if (nested_escaped) nested_escaped = false;
        else if (ch == '\\') nested_escaped = true;
        else if (ch == nested_quote) nested_quote = '\0';
        continue;
      }
      if (ch == '\'' || ch == '"') {
        nested_quote = ch;
      } else if (ch == '(' || ch == '[' || ch == '{') {
        ++nested;
      } else if (ch == ')' || ch == ']' || ch == '}') {
        --nested;
      } else if (ch == '=' && nested == 0) {
        parameter_name = ast_trim(piece.substr(0, i));
        default_source = ast_trim(piece.substr(i + 1));
        return;
      }
    }
    parameter_name = ast_trim(piece);
    default_source = {};
  };

  for (const auto& storage : pieces) {
    std::string_view piece = ast_trim(storage);
    if (piece == "/") {
      if (saw_slash || keyword_only) return false;
      saw_slash = true;
      positional_only_count = positional.size();
      continue;
    }
    if (piece == "*") {
      keyword_only = true;
      continue;
    }
    if (piece.rfind("**", 0) == 0) {
      kwarg = ast_make_arg(state, ast_trim(piece.substr(2)), error);
      if (kwarg.tag == ValueTag::Invalid) return false;
      keyword_only = true;
      continue;
    }
    if (piece.front() == '*') {
      vararg = ast_make_arg(state, ast_trim(piece.substr(1)), error);
      if (vararg.tag == ValueTag::Invalid) return false;
      keyword_only = true;
      continue;
    }

    std::string_view parameter_name;
    std::string_view default_source;
    split_default(piece, parameter_name, default_source);
    if (parameter_name.empty() || parameter_name.find(':') != std::string_view::npos) {
      return false;
    }
    Value parameter = ast_make_arg(state, parameter_name, error);
    if (parameter.tag == ValueTag::Invalid) return false;
    Value default_value = Value::invalid();
    if (!default_source.empty()) {
      default_value = ast_parse_simple_expr(state, default_source, error);
      if (default_value.tag == ValueTag::Invalid) return false;
    }
    if (keyword_only) {
      kwonly.push_back(parameter);
      kw_defaults.push_back(
          default_value.tag == ValueTag::Invalid ? Value::none() : default_value);
    } else {
      positional.push_back(ParsedParameter{parameter, default_value});
    }
  }

  for (size_t i = 0; i < positional.size(); ++i) {
    if (saw_slash && i < positional_only_count) {
      posonly.push_back(positional[i].node);
    } else {
      regular.push_back(positional[i].node);
    }
  }
  std::vector<Value> defaults;
  bool found_default = false;
  for (const auto& parameter : positional) {
    if (parameter.default_value.tag != ValueTag::Invalid) {
      found_default = true;
      defaults.push_back(parameter.default_value);
    } else if (found_default) {
      return false;
    }
  }
  object_set_attr(args, "posonlyargs", Value::list(std::move(posonly)), error);
  object_set_attr(args, "args", Value::list(std::move(regular)), error);
  object_set_attr(args, "vararg", vararg, error);
  object_set_attr(args, "kwonlyargs", Value::list(std::move(kwonly)), error);
  object_set_attr(args, "kw_defaults", Value::list(std::move(kw_defaults)), error);
  object_set_attr(args, "kwarg", kwarg, error);
  object_set_attr(args, "defaults", Value::list(std::move(defaults)), error);

  object_set_attr(function, "name", Value::string(name), error);
  object_set_attr(function, "args", args, error);
  object_set_attr(function, "body", Value::list({pass}), error);
  object_set_attr(function, "decorator_list", Value::list({}), error);
  object_set_attr(function, "returns", Value::none(), error);
  object_set_attr(function, "type_comment", Value::none(), error);
  object_set_attr(module, "body", Value::list({function}), error);
  object_set_attr(module, "type_ignores", Value::list({}), error);
  value_assign_fast(out, module);
  return true;
}

Value convert_parser_expr(AstState* state, const ast::Expr& expr, bool store_context, std::string& error);

Value convert_parser_comprehension(
    AstState* state, const ast::Expr& target, const ast::Expr& iterable,
    const ast::ExprPtr& filter, bool is_async, std::string& error) {
  Value converted_target = convert_parser_expr(state, target, true, error);
  Value converted_iterable = convert_parser_expr(state, iterable, false, error);
  if (converted_target.tag == ValueTag::Invalid ||
      converted_iterable.tag == ValueTag::Invalid) return Value::invalid();
  std::vector<Value> conditions;
  if (filter != nullptr) {
    Value converted_filter = convert_parser_expr(state, *filter, false, error);
    if (converted_filter.tag == ValueTag::Invalid) return Value::invalid();
    conditions.push_back(std::move(converted_filter));
  }
  Value node = ast_instance(state, "comprehension");
  object_set_attr(node, "target", converted_target, error);
  object_set_attr(node, "iter", converted_iterable, error);
  object_set_attr(node, "ifs", Value::list(std::move(conditions)), error);
  object_set_attr(node, "is_async", Value::int64(is_async ? 1 : 0), error);
  return node;
}

Value parser_context(AstState* state, bool store) {
  return ast_instance(state, store ? "Store" : "Load");
}

void set_parser_location(Value& node, const ast::Expr& expr, std::string& error) {
  ast_set_location(node, expr.line, expr.end_line,
      expr.column == 0 ? 0 : expr.column - 1,
      expr.end_column == 0 ? 0 : expr.end_column - 1, error);
}

void set_parser_location(Value& node, const ast::Stmt& stmt, std::string& error) {
  ast_set_location(node, stmt.line, stmt.end_line,
      stmt.column == 0 ? 0 : stmt.column - 1,
      stmt.end_column == 0 ? 0 : stmt.end_column - 1, error);
}

Value convert_parser_expr(AstState* state, const ast::Expr& expr, bool store_context, std::string& error) {
  Value node;
  if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
    node = ast_instance(state, "Name");
    object_set_attr(node, "id", Value::string(name->name), error);
    object_set_attr(node, "ctx", parser_context(state, store_context), error);
  } else if (auto* literal = dynamic_cast<const ast::LiteralExpr*>(&expr)) {
    Value literal_value;
    switch (literal->kind) {
      case ast::LiteralExpr::Kind::None: literal_value = Value::none(); break;
      case ast::LiteralExpr::Kind::Bool: literal_value = Value::boolean(literal->bool_value); break;
      case ast::LiteralExpr::Kind::Int:
        literal_value = value_bigint_from_decimal(
            literal->text,
            literal->text.size() > 1 && literal->text[0] == '0' &&
                    (literal->text[1] == 'x' || literal->text[1] == 'X' ||
                     literal->text[1] == 'o' || literal->text[1] == 'O' ||
                     literal->text[1] == 'b' || literal->text[1] == 'B')
                ? 0 : 10,
            error);
        if (literal_value.tag != ValueTag::Invalid)
          node = ast_make_constant(state, literal_value, error);
        break;
      case ast::LiteralExpr::Kind::Double:
      case ast::LiteralExpr::Kind::Complex: {
        std::string number = literal->text;
        number.erase(std::remove(number.begin(), number.end(), '_'), number.end());
        if (literal->kind == ast::LiteralExpr::Kind::Complex) number.pop_back();
        const double parsed = std::strtod(number.c_str(), nullptr);
        literal_value = literal->kind == ast::LiteralExpr::Kind::Complex
            ? Value::complex(0.0, parsed) : Value::number(parsed);
        node = ast_make_constant(state, literal_value, error);
        break;
      }
      case ast::LiteralExpr::Kind::Ellipsis: {
        const Value* ellipsis = state->runtime == nullptr
            ? nullptr : state->runtime->find_builtin("Ellipsis");
        node = ast_make_constant(
            state, ellipsis == nullptr ? Value::none() : *ellipsis, error);
        break;
      }
      case ast::LiteralExpr::Kind::String:
        node = ast_make_constant(state, Value::string(literal->text), error);
        break;
      case ast::LiteralExpr::Kind::Bytes:
        node = ast_make_constant(state, Value::bytes(literal->text), error);
        break;
    }
    if (node.tag == ValueTag::Invalid &&
        (literal->kind == ast::LiteralExpr::Kind::None || literal->kind == ast::LiteralExpr::Kind::Bool)) {
      node = ast_make_constant(state, literal_value, error);
    }
  } else if (auto* yielded = dynamic_cast<const ast::YieldExpr*>(&expr)) {
    node = ast_instance(state, yielded->from ? "YieldFrom" : "Yield");
    Value value = yielded->expr == nullptr ? Value::none()
        : convert_parser_expr(state, *yielded->expr, false, error);
    if (value.tag == ValueTag::Invalid) return Value::invalid();
    object_set_attr(node, "value", value, error);
  } else if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&expr)) {
    Value value = convert_parser_expr(state, *starred->expr, store_context, error);
    if (value.tag == ValueTag::Invalid) return Value::invalid();
    node = ast_instance(state, "Starred");
    object_set_attr(node, "value", value, error);
    object_set_attr(node, "ctx", parser_context(state, store_context), error);
  } else if (auto* named = dynamic_cast<const ast::NamedExpr*>(&expr)) {
    Value target = ast_make_name(state, named->name, error);
    Value value = convert_parser_expr(state, *named->value, false, error);
    if (target.tag == ValueTag::Invalid || value.tag == ValueTag::Invalid)
      return Value::invalid();
    object_set_attr(target, "ctx", parser_context(state, true), error);
    node = ast_instance(state, "NamedExpr");
    object_set_attr(node, "target", target, error);
    object_set_attr(node, "value", value, error);
  } else if (auto* awaited = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
    Value value = convert_parser_expr(state, *awaited->expr, false, error);
    if (value.tag == ValueTag::Invalid) return Value::invalid();
    node = ast_instance(state, "Await");
    object_set_attr(node, "value", value, error);
  } else if (auto* lambda = dynamic_cast<const ast::LambdaExpr*>(&expr)) {
    Value arguments = make_empty_arguments(state, error);
    std::vector<Value> posonly;
    std::vector<Value> regular;
    std::vector<Value> kwonly;
    std::vector<Value> kw_defaults;
    std::vector<Value> defaults;
    Value vararg = Value::none();
    Value kwarg = Value::none();
    for (const auto& parameter : lambda->signature) {
      Value arg = ast_make_arg(state, parameter.name, error);
      switch (parameter.kind) {
        case ast::LambdaExpr::Param::Kind::PosOnly:
          posonly.push_back(arg);
          if (parameter.default_value != nullptr)
            defaults.push_back(convert_parser_expr(state, *parameter.default_value, false, error));
          break;
        case ast::LambdaExpr::Param::Kind::PosOrKeyword:
          regular.push_back(arg);
          if (parameter.default_value != nullptr)
            defaults.push_back(convert_parser_expr(state, *parameter.default_value, false, error));
          break;
        case ast::LambdaExpr::Param::Kind::VarArgs: vararg = arg; break;
        case ast::LambdaExpr::Param::Kind::KeywordOnly:
          kwonly.push_back(arg);
          kw_defaults.push_back(parameter.default_value == nullptr ? Value::none()
              : convert_parser_expr(state, *parameter.default_value, false, error));
          break;
        case ast::LambdaExpr::Param::Kind::KwArgs: kwarg = arg; break;
      }
    }
    object_set_attr(arguments, "posonlyargs", Value::list(std::move(posonly)), error);
    object_set_attr(arguments, "args", Value::list(std::move(regular)), error);
    object_set_attr(arguments, "vararg", vararg, error);
    object_set_attr(arguments, "kwonlyargs", Value::list(std::move(kwonly)), error);
    object_set_attr(arguments, "kw_defaults", Value::list(std::move(kw_defaults)), error);
    object_set_attr(arguments, "kwarg", kwarg, error);
    object_set_attr(arguments, "defaults", Value::list(std::move(defaults)), error);
    Value body = convert_parser_expr(state, *lambda->body, false, error);
    if (body.tag == ValueTag::Invalid) return Value::invalid();
    node = ast_instance(state, "Lambda");
    object_set_attr(node, "args", arguments, error);
    object_set_attr(node, "body", body, error);
  } else if (auto* fstring = dynamic_cast<const ast::FStringExpr*>(&expr)) {
    std::vector<Value> values;
    for (const auto& part : fstring->parts) {
      if (!part.is_expr) {
        values.push_back(ast_make_constant(state, Value::string(part.text), error));
        continue;
      }
      if (part.debug_equal) {
        values.push_back(ast_make_constant(state, Value::string(part.debug_text), error));
      }
      auto parsed = parse_expression_source(part.text);
      if (!parsed.errors.empty() || parsed.expression == nullptr) {
        error = parsed.errors.empty() ? "invalid formatted expression" : parsed.errors.front();
        return Value::invalid();
      }
      Value formatted = ast_instance(state, "FormattedValue");
      Value expression = convert_parser_expr(state, *parsed.expression, false, error);
      if (expression.tag == ValueTag::Invalid) return Value::invalid();
      object_set_attr(formatted, "value", expression, error);
      object_set_attr(formatted, "conversion", Value::int64(
          part.conversion == '\0' ? (part.debug_equal ? 'r' : -1) : part.conversion), error);
      Value format_spec = Value::none();
      if (!part.format_spec.empty()) {
        ast::FStringExpr spec(parse_fstring_parts(part.format_spec));
        format_spec = convert_parser_expr(state, spec, false, error);
      }
      object_set_attr(formatted, "format_spec", format_spec, error);
      values.push_back(std::move(formatted));
    }
    node = ast_instance(state, "JoinedStr");
    object_set_attr(node, "values", Value::list(std::move(values)), error);
  } else if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
    Value owner = convert_parser_expr(state, *attr->object, false, error);
    node = ast_instance(state, "Attribute");
    object_set_attr(node, "value", owner, error);
    object_set_attr(node, "attr", Value::string(attr->name), error);
    object_set_attr(node, "ctx", parser_context(state, store_context), error);
  } else if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    Value callee = convert_parser_expr(state, *call->callee, false, error);
    if (callee.tag == ValueTag::Invalid) return Value::invalid();
    std::vector<Value> args;
    std::vector<Value> keywords;
    if (!call->call_args.empty()) {
      for (const auto& argument : call->call_args) {
        Value value = convert_parser_expr(state, *argument.value, false, error);
        if (value.tag == ValueTag::Invalid) return Value::invalid();
        if (argument.name.empty() && !argument.kw_star) {
          if (argument.star) {
            Value starred = ast_instance(state, "Starred");
            object_set_attr(starred, "value", value, error);
            object_set_attr(starred, "ctx", parser_context(state, false), error);
            args.push_back(std::move(starred));
          } else {
            args.push_back(std::move(value));
          }
        } else {
          Value keyword = ast_instance(state, "keyword");
          object_set_attr(keyword, "arg", argument.kw_star ? Value::none() : Value::string(argument.name), error);
          object_set_attr(keyword, "value", value, error);
          keywords.push_back(std::move(keyword));
        }
      }
    } else {
      for (const auto& argument : call->args) {
        Value converted = convert_parser_expr(state, *argument, false, error);
        if (converted.tag == ValueTag::Invalid) return Value::invalid();
        args.push_back(std::move(converted));
      }
    }
    node = ast_instance(state, "Call");
    object_set_attr(node, "func", callee, error);
    object_set_attr(node, "args", Value::list(std::move(args)), error);
    object_set_attr(node, "keywords", Value::list(std::move(keywords)), error);
  } else if (auto* conditional = dynamic_cast<const ast::ConditionalExpr*>(&expr)) {
    node = ast_instance(state, "IfExp");
    object_set_attr(node, "test", convert_parser_expr(state, *conditional->condition, false, error), error);
    object_set_attr(node, "body", convert_parser_expr(state, *conditional->then_expr, false, error), error);
    object_set_attr(node, "orelse", convert_parser_expr(state, *conditional->else_expr, false, error), error);
  } else if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
    static const std::unordered_map<std::string, const char*> operators = {
        {"+", "UAdd"}, {"-", "USub"}, {"~", "Invert"}, {"not", "Not"}};
    auto found = operators.find(unary->op);
    if (found == operators.end()) return Value::invalid();
    node = ast_instance(state, "UnaryOp");
    object_set_attr(node, "op", ast_instance(state, found->second), error);
    object_set_attr(node, "operand", convert_parser_expr(state, *unary->expr, false, error), error);
  } else if (auto* comparison = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
    static const std::unordered_map<std::string, const char*> operators = {
        {"==", "Eq"}, {"!=", "NotEq"}, {"<", "Lt"}, {"<=", "LtE"},
        {">", "Gt"}, {">=", "GtE"}, {"is", "Is"}, {"is not", "IsNot"},
        {"in", "In"}, {"not in", "NotIn"}};
    std::vector<Value> ops;
    std::vector<Value> comparators;
    for (const auto& [op, value] : comparison->comparisons) {
      auto found = operators.find(op);
      if (found == operators.end()) return Value::invalid();
      ops.push_back(ast_instance(state, found->second));
      comparators.push_back(convert_parser_expr(state, *value, false, error));
    }
    node = ast_instance(state, "Compare");
    object_set_attr(node, "left", convert_parser_expr(state, *comparison->first, false, error), error);
    object_set_attr(node, "ops", Value::list(std::move(ops)), error);
    object_set_attr(node, "comparators", Value::list(std::move(comparators)), error);
  } else if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    if (binary->op == "and" || binary->op == "or") {
      node = ast_instance(state, "BoolOp");
      object_set_attr(node, "op", ast_instance(state, binary->op == "and" ? "And" : "Or"), error);
      object_set_attr(node, "values", Value::list({
          convert_parser_expr(state, *binary->lhs, false, error),
          convert_parser_expr(state, *binary->rhs, false, error)}), error);
      set_parser_location(node, expr, error);
      return node;
    }
    static const std::unordered_map<std::string, const char*> comparisons = {
        {"==", "Eq"}, {"!=", "NotEq"}, {"<", "Lt"}, {"<=", "LtE"},
        {">", "Gt"}, {">=", "GtE"}, {"is", "Is"}, {"is not", "IsNot"},
        {"in", "In"}, {"not in", "NotIn"}};
    if (auto comparison = comparisons.find(binary->op); comparison != comparisons.end()) {
      node = ast_instance(state, "Compare");
      object_set_attr(node, "left", convert_parser_expr(state, *binary->lhs, false, error), error);
      object_set_attr(node, "ops", Value::list({ast_instance(state, comparison->second)}), error);
      object_set_attr(node, "comparators", Value::list({
          convert_parser_expr(state, *binary->rhs, false, error)}), error);
      set_parser_location(node, expr, error);
      return node;
    }
    static const std::unordered_map<std::string, const char*> operators = {
        {"+", "Add"}, {"-", "Sub"}, {"*", "Mult"}, {"@", "MatMult"},
        {"/", "Div"}, {"//", "FloorDiv"}, {"%", "Mod"}, {"**", "Pow"},
        {"<<", "LShift"}, {">>", "RShift"}, {"|", "BitOr"},
        {"^", "BitXor"}, {"&", "BitAnd"}};
    auto found = operators.find(binary->op);
    if (found == operators.end()) return Value::invalid();
    node = ast_instance(state, "BinOp");
    object_set_attr(node, "left", convert_parser_expr(state, *binary->lhs, false, error), error);
    object_set_attr(node, "op", ast_instance(state, found->second), error);
    object_set_attr(node, "right", convert_parser_expr(state, *binary->rhs, false, error), error);
  } else if (auto* slice = dynamic_cast<const ast::SliceExpr*>(&expr)) {
    Value lower = slice->start == nullptr ? Value::none()
        : convert_parser_expr(state, *slice->start, false, error);
    Value upper = slice->stop == nullptr ? Value::none()
        : convert_parser_expr(state, *slice->stop, false, error);
    Value step = slice->step == nullptr ? Value::none()
        : convert_parser_expr(state, *slice->step, false, error);
    if (lower.tag == ValueTag::Invalid || upper.tag == ValueTag::Invalid ||
        step.tag == ValueTag::Invalid) return Value::invalid();
    node = ast_instance(state, "Slice");
    object_set_attr(node, "lower", lower, error);
    object_set_attr(node, "upper", upper, error);
    object_set_attr(node, "step", step, error);
  } else if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
    node = ast_instance(state, "Subscript");
    object_set_attr(node, "value", convert_parser_expr(state, *subscript->object, false, error), error);
    object_set_attr(node, "slice", convert_parser_expr(state, *subscript->index, false, error), error);
    object_set_attr(node, "ctx", parser_context(state, store_context), error);
  } else if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    std::vector<Value> elements;
    for (const auto& item : tuple->items) {
      Value converted = convert_parser_expr(state, *item, store_context, error);
      if (converted.tag == ValueTag::Invalid) return Value::invalid();
      elements.push_back(std::move(converted));
    }
    node = ast_instance(state, "Tuple");
    object_set_attr(node, "elts", Value::list(std::move(elements)), error);
    object_set_attr(node, "ctx", parser_context(state, store_context), error);
  } else if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    std::vector<Value> elements;
    for (const auto& item : list->items) {
      Value converted = convert_parser_expr(state, *item, store_context, error);
      if (converted.tag == ValueTag::Invalid) return Value::invalid();
      elements.push_back(std::move(converted));
    }
    node = ast_instance(state, "List");
    object_set_attr(node, "elts", Value::list(std::move(elements)), error);
    object_set_attr(node, "ctx", parser_context(state, store_context), error);
  } else if (auto* dict = dynamic_cast<const ast::DictExpr*>(&expr)) {
    std::vector<Value> keys;
    std::vector<Value> values;
    keys.reserve(dict->entries.size());
    values.reserve(dict->entries.size());
    for (const auto& entry : dict->entries) {
      Value key = entry.first == nullptr ? Value::none()
          : convert_parser_expr(state, *entry.first, false, error);
      Value value = convert_parser_expr(state, *entry.second, false, error);
      if (key.tag == ValueTag::Invalid || value.tag == ValueTag::Invalid)
        return Value::invalid();
      keys.push_back(std::move(key));
      values.push_back(std::move(value));
    }
    node = ast_instance(state, "Dict");
    object_set_attr(node, "keys", Value::list(std::move(keys)), error);
    object_set_attr(node, "values", Value::list(std::move(values)), error);
  } else if (auto* list_comp = dynamic_cast<const ast::ListCompExpr*>(&expr)) {
    Value element = convert_parser_expr(state, *list_comp->result, false, error);
    Value first = convert_parser_comprehension(state, *list_comp->target_expr,
        *list_comp->iterable, list_comp->filter, list_comp->is_async, error);
    if (element.tag == ValueTag::Invalid || first.tag == ValueTag::Invalid) return Value::invalid();
    std::vector<Value> generators{first};
    for (const auto& clause : list_comp->extra_clauses) {
      Value converted = convert_parser_comprehension(state, *clause.target_expr,
          *clause.iterable, clause.filter, clause.is_async, error);
      if (converted.tag == ValueTag::Invalid) return Value::invalid();
      generators.push_back(std::move(converted));
    }
    node = ast_instance(state, "ListComp");
    object_set_attr(node, "elt", element, error);
    object_set_attr(node, "generators", Value::list(std::move(generators)), error);
  } else if (auto* set_comp = dynamic_cast<const ast::SetCompExpr*>(&expr)) {
    Value element = convert_parser_expr(state, *set_comp->result, false, error);
    Value first = convert_parser_comprehension(state, *set_comp->target_expr,
        *set_comp->iterable, set_comp->filter, set_comp->is_async, error);
    if (element.tag == ValueTag::Invalid || first.tag == ValueTag::Invalid) return Value::invalid();
    std::vector<Value> generators{first};
    for (const auto& clause : set_comp->extra_clauses) {
      Value converted = convert_parser_comprehension(state, *clause.target_expr,
          *clause.iterable, clause.filter, clause.is_async, error);
      if (converted.tag == ValueTag::Invalid) return Value::invalid();
      generators.push_back(std::move(converted));
    }
    node = ast_instance(state, "SetComp");
    object_set_attr(node, "elt", element, error);
    object_set_attr(node, "generators", Value::list(std::move(generators)), error);
  } else if (auto* dict_comp = dynamic_cast<const ast::DictCompExpr*>(&expr)) {
    Value key = convert_parser_expr(state, *dict_comp->key, false, error);
    Value value = convert_parser_expr(state, *dict_comp->value, false, error);
    Value first = convert_parser_comprehension(state, *dict_comp->target_expr,
        *dict_comp->iterable, dict_comp->filter, dict_comp->is_async, error);
    if (key.tag == ValueTag::Invalid || value.tag == ValueTag::Invalid ||
        first.tag == ValueTag::Invalid) return Value::invalid();
    std::vector<Value> generators{first};
    for (const auto& clause : dict_comp->extra_clauses) {
      Value converted = convert_parser_comprehension(state, *clause.target_expr,
          *clause.iterable, clause.filter, clause.is_async, error);
      if (converted.tag == ValueTag::Invalid) return Value::invalid();
      generators.push_back(std::move(converted));
    }
    node = ast_instance(state, "DictComp");
    object_set_attr(node, "key", key, error);
    object_set_attr(node, "value", value, error);
    object_set_attr(node, "generators", Value::list(std::move(generators)), error);
  } else if (auto* generator = dynamic_cast<const ast::GeneratorExpr*>(&expr)) {
    Value element = convert_parser_expr(state, *generator->result, false, error);
    Value first = convert_parser_comprehension(state, *generator->target_expr,
        *generator->iterable, generator->filter, generator->is_async, error);
    if (element.tag == ValueTag::Invalid || first.tag == ValueTag::Invalid) return Value::invalid();
    std::vector<Value> generators{first};
    for (const auto& clause : generator->extra_clauses) {
      Value converted = convert_parser_comprehension(state, *clause.target_expr,
          *clause.iterable, clause.filter, clause.is_async, error);
      if (converted.tag == ValueTag::Invalid) return Value::invalid();
      generators.push_back(std::move(converted));
    }
    node = ast_instance(state, "GeneratorExp");
    object_set_attr(node, "elt", element, error);
    object_set_attr(node, "generators", Value::list(std::move(generators)), error);
  } else if (auto* set = dynamic_cast<const ast::SetExpr*>(&expr)) {
    std::vector<Value> elements;
    elements.reserve(set->items.size());
    for (const auto& item : set->items) {
      Value element = convert_parser_expr(state, *item, false, error);
      if (element.tag == ValueTag::Invalid) return Value::invalid();
      elements.push_back(std::move(element));
    }
    node = ast_instance(state, "Set");
    object_set_attr(node, "elts", Value::list(std::move(elements)), error);
  } else {
    return Value::invalid();
  }
  if (node.tag != ValueTag::Invalid) set_parser_location(node, expr, error);
  return node;
}

Value convert_parser_pattern(AstState* state, const ast::Expr& expr, std::string& error);

bool append_parser_or_patterns(
    AstState* state, const ast::Expr& expr, std::vector<Value>& patterns,
    std::string& error) {
  if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr);
      binary != nullptr && binary->op == "|") {
    return append_parser_or_patterns(state, *binary->lhs, patterns, error) &&
           append_parser_or_patterns(state, *binary->rhs, patterns, error);
  }
  Value pattern = convert_parser_pattern(state, expr, error);
  if (pattern.tag == ValueTag::Invalid) return false;
  patterns.push_back(std::move(pattern));
  return true;
}

Value convert_parser_pattern(AstState* state, const ast::Expr& expr, std::string& error) {
  Value node;
  if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
    node = ast_instance(state, "MatchAs");
    object_set_attr(node, "pattern", Value::none(), error);
    object_set_attr(node, "name", name->name == "_" ? Value::none() : Value::string(name->name), error);
  } else if (auto* literal = dynamic_cast<const ast::LiteralExpr*>(&expr)) {
    const bool singleton = literal->kind == ast::LiteralExpr::Kind::None ||
                           literal->kind == ast::LiteralExpr::Kind::Bool;
    node = ast_instance(state, singleton ? "MatchSingleton" : "MatchValue");
    Value value = convert_parser_expr(state, expr, false, error);
    if (value.tag == ValueTag::Invalid) return value;
    if (singleton) {
      Value constant;
      if (!object_get_attr(value, "value", constant, error)) return Value::invalid();
      object_set_attr(node, "value", constant, error);
    } else {
      object_set_attr(node, "value", value, error);
    }
  } else if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    if (binary->op == "|") {
      std::vector<Value> patterns;
      if (!append_parser_or_patterns(state, expr, patterns, error)) return Value::invalid();
      node = ast_instance(state, "MatchOr");
      object_set_attr(node, "patterns", Value::list(std::move(patterns)), error);
    } else if (binary->op == "as") {
      auto* capture = dynamic_cast<const ast::NameExpr*>(binary->rhs.get());
      if (capture == nullptr) return Value::invalid();
      Value pattern = convert_parser_pattern(state, *binary->lhs, error);
      if (pattern.tag == ValueTag::Invalid) return pattern;
      node = ast_instance(state, "MatchAs");
      object_set_attr(node, "pattern", pattern, error);
      object_set_attr(node, "name", Value::string(capture->name), error);
    }
  } else if (auto* sequence = dynamic_cast<const ast::ListExpr*>(&expr)) {
    std::vector<Value> patterns;
    for (const auto& item : sequence->items) {
      Value pattern = convert_parser_pattern(state, *item, error);
      if (pattern.tag == ValueTag::Invalid) return pattern;
      patterns.push_back(std::move(pattern));
    }
    node = ast_instance(state, "MatchSequence");
    object_set_attr(node, "patterns", Value::list(std::move(patterns)), error);
  } else if (auto* sequence = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    std::vector<Value> patterns;
    for (const auto& item : sequence->items) {
      Value pattern = convert_parser_pattern(state, *item, error);
      if (pattern.tag == ValueTag::Invalid) return pattern;
      patterns.push_back(std::move(pattern));
    }
    node = ast_instance(state, "MatchSequence");
    object_set_attr(node, "patterns", Value::list(std::move(patterns)), error);
  } else if (auto* mapping = dynamic_cast<const ast::DictExpr*>(&expr)) {
    std::vector<Value> keys;
    std::vector<Value> patterns;
    Value rest = Value::none();
    for (const auto& entry : mapping->entries) {
      if (entry.first == nullptr) {
        auto* capture = dynamic_cast<const ast::NameExpr*>(entry.second.get());
        if (capture == nullptr) return Value::invalid();
        rest = Value::string(capture->name);
        continue;
      }
      Value key = convert_parser_expr(state, *entry.first, false, error);
      Value pattern = convert_parser_pattern(state, *entry.second, error);
      if (key.tag == ValueTag::Invalid || pattern.tag == ValueTag::Invalid) return Value::invalid();
      keys.push_back(std::move(key));
      patterns.push_back(std::move(pattern));
    }
    node = ast_instance(state, "MatchMapping");
    object_set_attr(node, "keys", Value::list(std::move(keys)), error);
    object_set_attr(node, "patterns", Value::list(std::move(patterns)), error);
    object_set_attr(node, "rest", rest, error);
  } else if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    Value klass = convert_parser_expr(state, *call->callee, false, error);
    if (klass.tag == ValueTag::Invalid) return klass;
    std::vector<Value> positional;
    std::vector<Value> keyword_names;
    std::vector<Value> keyword_patterns;
    for (const auto& argument : call->args) {
      Value pattern = convert_parser_pattern(state, *argument, error);
      if (pattern.tag == ValueTag::Invalid) return pattern;
      positional.push_back(std::move(pattern));
    }
    for (const auto& argument : call->call_args) {
      if (argument.star || argument.kw_star) return Value::invalid();
      Value pattern = convert_parser_pattern(state, *argument.value, error);
      if (pattern.tag == ValueTag::Invalid) return pattern;
      if (argument.name.empty()) positional.push_back(std::move(pattern));
      else {
        keyword_names.push_back(Value::string(argument.name));
        keyword_patterns.push_back(std::move(pattern));
      }
    }
    node = ast_instance(state, "MatchClass");
    object_set_attr(node, "cls", klass, error);
    object_set_attr(node, "patterns", Value::list(std::move(positional)), error);
    object_set_attr(node, "kwd_attrs", Value::list(std::move(keyword_names)), error);
    object_set_attr(node, "kwd_patterns", Value::list(std::move(keyword_patterns)), error);
  } else if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&expr)) {
    auto* capture = dynamic_cast<const ast::NameExpr*>(starred->expr.get());
    if (capture == nullptr) return Value::invalid();
    node = ast_instance(state, "MatchStar");
    object_set_attr(node, "name", capture->name == "_" ? Value::none() : Value::string(capture->name), error);
  } else if (dynamic_cast<const ast::AttrExpr*>(&expr) != nullptr ||
             dynamic_cast<const ast::UnaryExpr*>(&expr) != nullptr) {
    Value value = convert_parser_expr(state, expr, false, error);
    if (value.tag == ValueTag::Invalid) return value;
    node = ast_instance(state, "MatchValue");
    object_set_attr(node, "value", value, error);
  }
  if (node.tag == ValueTag::Invalid) return node;
  set_parser_location(node, expr, error);
  return node;
}

bool convert_parser_statements(
    AstState* state, const std::vector<ast::StmtPtr>& statements,
    std::vector<Value>& out, std::string& error) {
  const auto import_aliases = [&](const std::vector<ast::ImportBinding>& bindings,
                                  bool from_import) {
    std::vector<Value> names;
    names.reserve(bindings.size());
    for (const auto& binding : bindings) {
      Value alias = ast_instance(state, "alias");
      const std::string default_binding = from_import
          ? binding.name : binding.name.substr(0, binding.name.find('.'));
      object_set_attr(alias, "name", Value::string(binding.name), error);
      object_set_attr(alias, "asname",
          binding.as_name == default_binding ? Value::none()
                                             : Value::string(binding.as_name), error);
      names.push_back(std::move(alias));
    }
    return Value::list(std::move(names));
  };
  auto convert_type_params = [&](
      const std::vector<std::string>& names,
      const std::vector<std::pair<uint32_t, uint32_t>>& positions) {
    std::vector<Value> converted;
    converted.reserve(names.size());
    for (size_t index = 0; index < names.size(); ++index) {
      Value parameter = ast_instance(state, "TypeVar");
      object_set_attr(parameter, "name", Value::string(names[index]), error);
      object_set_attr(parameter, "bound", Value::none(), error);
      object_set_attr(parameter, "default_value", Value::none(), error);
      if (index < positions.size()) {
        const auto [line, column] = positions[index];
        ast_set_location(
            parameter, line, line, column == 0 ? 0 : column - 1,
            (column == 0 ? 0 : column - 1) +
                static_cast<uint32_t>(names[index].size()), error);
      }
      converted.push_back(std::move(parameter));
    }
    return Value::list(std::move(converted));
  };
  for (const auto& statement_ptr : statements) {
    const ast::Stmt& statement = *statement_ptr;
    Value node;
    if (auto* function = dynamic_cast<const ast::FunctionDef*>(&statement)) {
      node = ast_instance(state, function->is_async ? "AsyncFunctionDef" : "FunctionDef");
      Value arguments = make_empty_arguments(state, error);
      std::vector<Value> posonly;
      std::vector<Value> regular;
      std::vector<Value> kwonly;
      std::vector<Value> kw_defaults;
      std::vector<Value> defaults;
      Value vararg = Value::none();
      Value kwarg = Value::none();
      for (const auto& parameter : function->signature) {
        Value arg = ast_make_arg(state, parameter.name, error);
        if (parameter.annotation != nullptr) {
          object_set_attr(arg, "annotation",
              convert_parser_expr(state, *parameter.annotation, false, error), error);
        }
        switch (parameter.kind) {
          case ast::FunctionDef::Param::Kind::PosOnly:
            posonly.push_back(arg);
            if (parameter.default_value != nullptr) {
              defaults.push_back(convert_parser_expr(state, *parameter.default_value, false, error));
            }
            break;
          case ast::FunctionDef::Param::Kind::PosOrKeyword:
            regular.push_back(arg);
            if (parameter.default_value != nullptr) {
              defaults.push_back(convert_parser_expr(state, *parameter.default_value, false, error));
            }
            break;
          case ast::FunctionDef::Param::Kind::VarArgs: vararg = arg; break;
          case ast::FunctionDef::Param::Kind::KeywordOnly:
            kwonly.push_back(arg);
            kw_defaults.push_back(parameter.default_value == nullptr
                ? Value::none()
                : convert_parser_expr(state, *parameter.default_value, false, error));
            break;
          case ast::FunctionDef::Param::Kind::KwArgs: kwarg = arg; break;
        }
      }
      object_set_attr(arguments, "posonlyargs", Value::list(std::move(posonly)), error);
      object_set_attr(arguments, "args", Value::list(std::move(regular)), error);
      object_set_attr(arguments, "vararg", vararg, error);
      object_set_attr(arguments, "kwonlyargs", Value::list(std::move(kwonly)), error);
      object_set_attr(arguments, "kw_defaults", Value::list(std::move(kw_defaults)), error);
      object_set_attr(arguments, "kwarg", kwarg, error);
      object_set_attr(arguments, "defaults", Value::list(std::move(defaults)), error);
      std::vector<Value> body;
      if (!convert_parser_statements(state, function->body, body, error)) return false;
      std::vector<Value> decorators;
      for (const auto& decorator : function->decorators) {
        decorators.push_back(convert_parser_expr(state, *decorator, false, error));
      }
      object_set_attr(node, "name", Value::string(function->name), error);
      object_set_attr(node, "args", arguments, error);
      object_set_attr(node, "body", Value::list(std::move(body)), error);
      object_set_attr(node, "decorator_list", Value::list(std::move(decorators)), error);
      object_set_attr(node, "returns", function->return_annotation == nullptr
          ? Value::none()
          : convert_parser_expr(state, *function->return_annotation, false, error), error);
      object_set_attr(node, "type_comment", Value::none(), error);
      object_set_attr(node, "type_params", convert_type_params(
          function->type_params, function->type_param_positions), error);
    } else if (auto* klass = dynamic_cast<const ast::ClassDef*>(&statement)) {
      std::vector<Value> bases;
      std::vector<Value> keywords;
      std::vector<Value> decorators;
      std::vector<Value> body;
      for (const auto& base : klass->bases) {
        Value converted = convert_parser_expr(state, *base, false, error);
        if (converted.tag == ValueTag::Invalid) return false;
        bases.push_back(std::move(converted));
      }
      for (const auto& [name, value] : klass->keywords) {
        Value converted = convert_parser_expr(state, *value, false, error);
        if (converted.tag == ValueTag::Invalid) return false;
        Value keyword = ast_instance(state, "keyword");
        object_set_attr(keyword, "arg", name.empty() ? Value::none() : Value::string(name), error);
        object_set_attr(keyword, "value", converted, error);
        keywords.push_back(std::move(keyword));
      }
      for (const auto& decorator : klass->decorators) {
        Value converted = convert_parser_expr(state, *decorator, false, error);
        if (converted.tag == ValueTag::Invalid) return false;
        decorators.push_back(std::move(converted));
      }
      if (!convert_parser_statements(state, klass->body, body, error)) return false;
      node = ast_instance(state, "ClassDef");
      object_set_attr(node, "name", Value::string(klass->name), error);
      object_set_attr(node, "bases", Value::list(std::move(bases)), error);
      object_set_attr(node, "keywords", Value::list(std::move(keywords)), error);
      object_set_attr(node, "body", Value::list(std::move(body)), error);
      object_set_attr(node, "decorator_list", Value::list(std::move(decorators)), error);
      object_set_attr(node, "type_params", convert_type_params(
          klass->type_params, klass->type_param_positions), error);
    } else if (auto* alias = dynamic_cast<const ast::TypeAliasStmt*>(&statement)) {
      Value name = ast_instance(state, "Name");
      object_set_attr(name, "id", Value::string(alias->name), error);
      object_set_attr(name, "ctx", parser_context(state, true), error);
      ast_set_location(
          name, alias->name_line, alias->name_line,
          alias->name_column == 0 ? 0 : alias->name_column - 1,
          (alias->name_column == 0 ? 0 : alias->name_column - 1) +
              static_cast<uint32_t>(alias->name.size()), error);
      std::vector<Value> type_params;
      for (size_t index = 0; index < alias->type_params.size(); ++index) {
        Value parameter = ast_instance(state, "TypeVar");
        object_set_attr(parameter, "name", Value::string(alias->type_params[index]), error);
        object_set_attr(parameter, "bound", Value::none(), error);
        object_set_attr(parameter, "default_value", Value::none(), error);
        if (index < alias->type_param_positions.size()) {
          const auto [line, column] = alias->type_param_positions[index];
          ast_set_location(
              parameter, line, line, column == 0 ? 0 : column - 1,
              (column == 0 ? 0 : column - 1) +
                  static_cast<uint32_t>(alias->type_params[index].size()), error);
        }
        type_params.push_back(std::move(parameter));
      }
      Value value = convert_parser_expr(state, *alias->value, false, error);
      if (value.tag == ValueTag::Invalid) return false;
      node = ast_instance(state, "TypeAlias");
      object_set_attr(node, "name", name, error);
      object_set_attr(node, "type_params", Value::list(std::move(type_params)), error);
      object_set_attr(node, "value", value, error);
    } else if (auto* imported = dynamic_cast<const ast::ImportStmt*>(&statement)) {
      node = ast_instance(state, "Import");
      object_set_attr(node, "names", import_aliases(
          {{imported->name, imported->bind_name}}, false), error);
    } else if (auto* imported = dynamic_cast<const ast::ImportManyStmt*>(&statement)) {
      node = ast_instance(state, "Import");
      object_set_attr(node, "names", import_aliases(imported->names, false), error);
    } else if (auto* imported = dynamic_cast<const ast::FromImportStmt*>(&statement)) {
      node = ast_instance(state, "ImportFrom");
      const size_t level = imported->module.find_first_not_of('.');
      const std::string module_name = level == std::string::npos
          ? std::string() : imported->module.substr(level);
      object_set_attr(node, "module", module_name.empty()
          ? Value::none() : Value::string(module_name), error);
      object_set_attr(node, "names", import_aliases(imported->names, true), error);
      object_set_attr(node, "level", Value::int64(
          static_cast<int64_t>(level == std::string::npos
              ? imported->module.size() : level)), error);
    } else if (auto* loop = dynamic_cast<const ast::ForStmt*>(&statement)) {
      Value target = convert_parser_expr(state, *loop->target_expr, true, error);
      Value iterable = convert_parser_expr(state, *loop->iterable, false, error);
      std::vector<Value> body;
      std::vector<Value> otherwise;
      if (target.tag == ValueTag::Invalid || iterable.tag == ValueTag::Invalid ||
          !convert_parser_statements(state, loop->body, body, error) ||
          !convert_parser_statements(state, loop->else_body, otherwise, error)) return false;
      node = ast_instance(state, loop->is_async ? "AsyncFor" : "For");
      object_set_attr(node, "target", target, error);
      object_set_attr(node, "iter", iterable, error);
      object_set_attr(node, "body", Value::list(std::move(body)), error);
      object_set_attr(node, "orelse", Value::list(std::move(otherwise)), error);
      object_set_attr(node, "type_comment", Value::none(), error);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(&statement)) {
      Value test = convert_parser_expr(state, *loop->condition, false, error);
      std::vector<Value> body;
      std::vector<Value> otherwise;
      if (test.tag == ValueTag::Invalid ||
          !convert_parser_statements(state, loop->body, body, error) ||
          !convert_parser_statements(state, loop->else_body, otherwise, error)) return false;
      node = ast_instance(state, "While");
      object_set_attr(node, "test", test, error);
      object_set_attr(node, "body", Value::list(std::move(body)), error);
      object_set_attr(node, "orelse", Value::list(std::move(otherwise)), error);
    } else if (auto* attempted = dynamic_cast<const ast::TryExceptStmt*>(&statement)) {
      std::vector<Value> body;
      std::vector<Value> handlers;
      std::vector<Value> otherwise;
      std::vector<Value> finalbody;
      if (!convert_parser_statements(state, attempted->try_body, body, error) ||
          !convert_parser_statements(state, attempted->else_body, otherwise, error) ||
          !convert_parser_statements(state, attempted->finally_body, finalbody, error)) return false;
      bool starred = false;
      for (const auto& handler : attempted->handlers) {
        std::vector<Value> handler_body;
        if (!convert_parser_statements(state, handler.body, handler_body, error)) return false;
        Value type = handler.type == nullptr ? Value::none()
            : convert_parser_expr(state, *handler.type, false, error);
        if (type.tag == ValueTag::Invalid) return false;
        Value converted = ast_instance(state, "ExceptHandler");
        object_set_attr(converted, "type", type, error);
        object_set_attr(converted, "name", handler.name.empty()
            ? Value::none() : Value::string(handler.name), error);
        object_set_attr(converted, "body", Value::list(std::move(handler_body)), error);
        ast_set_location(converted, handler.line, handler.line,
            handler.column, handler.column, error);
        handlers.push_back(std::move(converted));
        starred = starred || handler.is_star;
      }
      node = ast_instance(state, starred ? "TryStar" : "Try");
      object_set_attr(node, "body", Value::list(std::move(body)), error);
      object_set_attr(node, "handlers", Value::list(std::move(handlers)), error);
      object_set_attr(node, "orelse", Value::list(std::move(otherwise)), error);
      object_set_attr(node, "finalbody", Value::list(std::move(finalbody)), error);
    } else if (auto* deleted = dynamic_cast<const ast::DelStmt*>(&statement)) {
      Value target = convert_parser_expr(state, *deleted->target, true, error);
      if (target.tag == ValueTag::Invalid) return false;
      node = ast_instance(state, "Delete");
      object_set_attr(node, "targets", Value::list({target}), error);
    } else if (auto* global = dynamic_cast<const ast::GlobalStmt*>(&statement)) {
      std::vector<Value> names;
      for (const auto& name : global->names) names.push_back(Value::string(name));
      node = ast_instance(state, "Global");
      object_set_attr(node, "names", Value::list(std::move(names)), error);
    } else if (auto* nonlocal = dynamic_cast<const ast::NonlocalStmt*>(&statement)) {
      std::vector<Value> names;
      for (const auto& name : nonlocal->names) names.push_back(Value::string(name));
      node = ast_instance(state, "Nonlocal");
      object_set_attr(node, "names", Value::list(std::move(names)), error);
    } else if (auto* with = dynamic_cast<const ast::WithStmt*>(&statement)) {
      Value manager = convert_parser_expr(state, *with->manager, false, error);
      Value target = with->target_expr == nullptr ? Value::none()
          : convert_parser_expr(state, *with->target_expr, true, error);
      std::vector<Value> body;
      if (manager.tag == ValueTag::Invalid || target.tag == ValueTag::Invalid ||
          !convert_parser_statements(state, with->body, body, error)) return false;
      Value item = ast_instance(state, "withitem");
      object_set_attr(item, "context_expr", manager, error);
      object_set_attr(item, "optional_vars", target, error);
      node = ast_instance(state, with->is_async ? "AsyncWith" : "With");
      object_set_attr(node, "items", Value::list({item}), error);
      object_set_attr(node, "body", Value::list(std::move(body)), error);
      object_set_attr(node, "type_comment", Value::none(), error);
    } else if (auto* annotated = dynamic_cast<const ast::AnnotatedAssignStmt*>(&statement)) {
      Value target = convert_parser_expr(state, *annotated->target, true, error);
      Value annotation = convert_parser_expr(state, *annotated->annotation, false, error);
      Value value = annotated->value == nullptr
          ? Value::none() : convert_parser_expr(state, *annotated->value, false, error);
      if (target.tag == ValueTag::Invalid || annotation.tag == ValueTag::Invalid ||
          value.tag == ValueTag::Invalid) return false;
      node = ast_instance(state, "AnnAssign");
      object_set_attr(node, "target", target, error);
      object_set_attr(node, "annotation", annotation, error);
      object_set_attr(node, "value", value, error);
      object_set_attr(node, "simple", Value::int64(
          dynamic_cast<const ast::NameExpr*>(annotated->target.get()) == nullptr ? 0 : 1), error);
    } else if (auto* augmented = dynamic_cast<const ast::AugAssignStmt*>(&statement)) {
      static const std::unordered_map<std::string, const char*> operators = {
          {"+", "Add"}, {"-", "Sub"}, {"*", "Mult"}, {"@", "MatMult"},
          {"/", "Div"}, {"//", "FloorDiv"}, {"%", "Mod"}, {"**", "Pow"},
          {"<<", "LShift"}, {">>", "RShift"}, {"|", "BitOr"},
          {"^", "BitXor"}, {"&", "BitAnd"}};
      auto found = operators.find(augmented->op);
      if (found == operators.end()) return false;
      node = ast_instance(state, "AugAssign");
      object_set_attr(node, "target", convert_parser_expr(state, *augmented->target, true, error), error);
      object_set_attr(node, "op", ast_instance(state, found->second), error);
      object_set_attr(node, "value", convert_parser_expr(state, *augmented->value, false, error), error);
    } else if (auto* assign = dynamic_cast<const ast::SubscriptAssignStmt*>(&statement)) {
      Value target = ast_instance(state, "Subscript");
      Value owner = convert_parser_expr(state, *assign->object, false, error);
      Value index = convert_parser_expr(state, *assign->index, false, error);
      Value value = convert_parser_expr(state, *assign->value, false, error);
      if (owner.tag == ValueTag::Invalid || index.tag == ValueTag::Invalid ||
          value.tag == ValueTag::Invalid) return false;
      object_set_attr(target, "value", owner, error);
      object_set_attr(target, "slice", index, error);
      object_set_attr(target, "ctx", parser_context(state, true), error);
      node = ast_instance(state, "Assign");
      object_set_attr(node, "targets", Value::list({target}), error);
      object_set_attr(node, "value", value, error);
      object_set_attr(node, "type_comment", Value::none(), error);
    } else if (auto* assign = dynamic_cast<const ast::AttrAssignStmt*>(&statement)) {
      Value target = ast_instance(state, "Attribute");
      Value owner = convert_parser_expr(state, *assign->object, false, error);
      Value value = convert_parser_expr(state, *assign->value, false, error);
      if (owner.tag == ValueTag::Invalid || value.tag == ValueTag::Invalid) return false;
      object_set_attr(target, "value", owner, error);
      object_set_attr(target, "attr", Value::string(assign->name), error);
      object_set_attr(target, "ctx", parser_context(state, true), error);
      node = ast_instance(state, "Assign");
      object_set_attr(node, "targets", Value::list({target}), error);
      object_set_attr(node, "value", value, error);
      object_set_attr(node, "type_comment", Value::none(), error);
    } else if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&statement)) {
      Value target = ast_make_name(state, assign->name, error);
      object_set_attr(target, "ctx", parser_context(state, true), error);
      node = ast_instance(state, "Assign");
      object_set_attr(node, "targets", Value::list({target}), error);
      object_set_attr(node, "value", convert_parser_expr(state, *assign->value, false, error), error);
      object_set_attr(node, "type_comment", Value::none(), error);
    } else if (auto* assign = dynamic_cast<const ast::UnpackAssignStmt*>(&statement)) {
      Value target = convert_parser_expr(state, *assign->target, true, error);
      Value value = convert_parser_expr(state, *assign->value, false, error);
      if (target.tag == ValueTag::Invalid || value.tag == ValueTag::Invalid) return false;
      node = ast_instance(state, "Assign");
      object_set_attr(node, "targets", Value::list({target}), error);
      object_set_attr(node, "value", value, error);
      object_set_attr(node, "type_comment", Value::none(), error);
    } else if (auto* assign = dynamic_cast<const ast::MultiAssignStmt*>(&statement)) {
      std::vector<Value> targets;
      for (const auto& target : assign->targets) {
        Value converted = convert_parser_expr(state, *target, true, error);
        if (converted.tag == ValueTag::Invalid) return false;
        targets.push_back(std::move(converted));
      }
      node = ast_instance(state, "Assign");
      object_set_attr(node, "targets", Value::list(std::move(targets)), error);
      object_set_attr(node, "value", convert_parser_expr(state, *assign->value, false, error), error);
      object_set_attr(node, "type_comment", Value::none(), error);
    } else if (auto* expression = dynamic_cast<const ast::ExprStmt*>(&statement)) {
      Value converted = convert_parser_expr(state, *expression->expr, false, error);
      if (converted.tag == ValueTag::Invalid) return false;
      node = ast_instance(state, "Expr");
      object_set_attr(node, "value", converted, error);
    } else if (auto* assertion = dynamic_cast<const ast::AssertStmt*>(&statement)) {
      node = ast_instance(state, "Assert");
      Value test = convert_parser_expr(state, *assertion->condition, false, error);
      if (test.tag == ValueTag::Invalid) return false;
      Value message = assertion->message == nullptr
          ? Value::none()
          : convert_parser_expr(state, *assertion->message, false, error);
      if (message.tag == ValueTag::Invalid) return false;
      object_set_attr(node, "test", test, error);
      object_set_attr(node, "msg", message, error);
    } else if (auto* matched = dynamic_cast<const ast::MatchStmt*>(&statement)) {
      Value subject = convert_parser_expr(state, *matched->subject, false, error);
      if (subject.tag == ValueTag::Invalid) return false;
      std::vector<Value> cases;
      for (const auto& source_case : matched->cases) {
        Value pattern;
        if (source_case.wildcard) {
          pattern = ast_instance(state, "MatchAs");
          object_set_attr(pattern, "pattern", Value::none(), error);
          object_set_attr(pattern, "name", Value::none(), error);
          set_parser_location(pattern, statement, error);
        } else if (source_case.pattern != nullptr) {
          pattern = convert_parser_pattern(state, *source_case.pattern, error);
        }
        if (pattern.tag == ValueTag::Invalid) return false;
        if (!source_case.as_name.empty()) {
          Value wrapped = ast_instance(state, "MatchAs");
          object_set_attr(wrapped, "pattern", pattern, error);
          object_set_attr(wrapped, "name", Value::string(source_case.as_name), error);
          set_parser_location(wrapped, statement, error);
          pattern = std::move(wrapped);
        }
        Value guard = source_case.guard == nullptr
            ? Value::none() : convert_parser_expr(state, *source_case.guard, false, error);
        if (guard.tag == ValueTag::Invalid) return false;
        std::vector<Value> body;
        if (!convert_parser_statements(state, source_case.body, body, error)) return false;
        Value case_node = ast_instance(state, "match_case");
        object_set_attr(case_node, "pattern", pattern, error);
        object_set_attr(case_node, "guard", guard, error);
        object_set_attr(case_node, "body", Value::list(std::move(body)), error);
        cases.push_back(std::move(case_node));
      }
      node = ast_instance(state, "Match");
      object_set_attr(node, "subject", subject, error);
      object_set_attr(node, "cases", Value::list(std::move(cases)), error);
    } else if (auto* conditional = dynamic_cast<const ast::IfStmt*>(&statement)) {
      std::vector<Value> body;
      std::vector<Value> otherwise;
      if (!convert_parser_statements(state, conditional->then_body, body, error) ||
          !convert_parser_statements(state, conditional->else_body, otherwise, error)) return false;
      node = ast_instance(state, "If");
      object_set_attr(node, "test", convert_parser_expr(state, *conditional->condition, false, error), error);
      object_set_attr(node, "body", Value::list(std::move(body)), error);
      object_set_attr(node, "orelse", Value::list(std::move(otherwise)), error);
    } else if (dynamic_cast<const ast::PassStmt*>(&statement) != nullptr) {
      node = ast_instance(state, "Pass");
    } else if (dynamic_cast<const ast::BreakStmt*>(&statement) != nullptr) {
      node = ast_instance(state, "Break");
    } else if (dynamic_cast<const ast::ContinueStmt*>(&statement) != nullptr) {
      node = ast_instance(state, "Continue");
    } else if (auto* returned = dynamic_cast<const ast::ReturnStmt*>(&statement)) {
      node = ast_instance(state, "Return");
      object_set_attr(node, "value", returned->value == nullptr
          ? Value::none() : convert_parser_expr(state, *returned->value, false, error), error);
    } else if (auto* raised = dynamic_cast<const ast::RaiseStmt*>(&statement)) {
      Value exception = raised->value == nullptr ? Value::none()
          : convert_parser_expr(state, *raised->value, false, error);
      Value cause = raised->cause == nullptr ? Value::none()
          : convert_parser_expr(state, *raised->cause, false, error);
      if (exception.tag == ValueTag::Invalid || cause.tag == ValueTag::Invalid)
        return false;
      node = ast_instance(state, "Raise");
      object_set_attr(node, "exc", exception, error);
      object_set_attr(node, "cause", cause, error);
    } else {
      return false;
    }
    if (node.tag == ValueTag::Invalid) return false;
    set_parser_location(node, statement, error);
    if (auto* conditional = dynamic_cast<const ast::IfStmt*>(&statement)) {
      const auto& ending_body = conditional->else_body.empty()
          ? conditional->then_body : conditional->else_body;
      if (!ending_body.empty()) {
        object_set_attr(node, "end_lineno", Value::int64(ending_body.back()->end_line), error);
        object_set_attr(node, "end_col_offset", Value::int64(ending_body.back()->end_column), error);
      }
    }
    out.push_back(std::move(node));
  }
  return true;
}

bool parse_with_runtime_parser_ast(
    AstState* state, const std::string& source, Value& out, std::string& error) {
  auto parsed = parse_source(source, true);
  if (!parsed.errors.empty()) return false;
  std::vector<Value> body;
  if (!convert_parser_statements(state, parsed.module.body, body, error)) return false;
  Value module = ast_instance(state, "Module");
  object_set_attr(module, "body", Value::list(std::move(body)), error);
  object_set_attr(module, "type_ignores", Value::list({}), error);
  value_assign_fast(out, module);
  return true;
}

bool ast_parse_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1 || argc > 3) {
    error = "ast.parse() expected source and optional filename/mode";
    return false;
  }
  auto* source = value_as_string(args[0]);
  if (source == nullptr) {
    error = "ast.parse() source must be str";
    return false;
  }
  std::string mode = "exec";
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string(kwargs[i].name) == "mode" && kwargs[i].value != nullptr) {
      auto* mode_string = value_as_string(*kwargs[i].value);
      if (mode_string == nullptr) {
        error = "ast.parse() mode must be str";
        return false;
      }
      mode = string_object_to_string(*mode_string);
    }
  }
  if (argc >= 3) {
    auto* mode_string = value_as_string(args[2]);
    if (mode_string == nullptr) {
      error = "ast.parse() mode must be str";
      return false;
    }
    mode = string_object_to_string(*mode_string);
  }
  auto* state = static_cast<AstState*>(user_data);
  Value parsed;
  if (mode == "exec" && parse_with_runtime_parser_ast(
          state, string_object_to_string(*source), parsed, error)) {
    value_assign_fast(out, parsed);
    return true;
  }
  if (mode == "exec" && parse_simple_function_ast(runtime, state, string_object_to_string(*source), parsed, error)) {
    value_assign_fast(out, parsed);
    return true;
  }
  if (mode == "exec" && parse_simple_module_ast(runtime, state, string_object_to_string(*source), parsed, error)) {
    value_assign_fast(out, parsed);
    return true;
  }
  if (mode == "exec" && parse_simple_flow_statement_ast(state, string_object_to_string(*source), parsed, error)) {
    value_assign_fast(out, parsed);
    return true;
  }
  if (mode == "exec" && parse_simple_expression_module_ast(runtime, state, string_object_to_string(*source), parsed, error)) {
    value_assign_fast(out, parsed);
    return true;
  }
  if (mode == "eval") {
    parsed = ast_parse_simple_expr(state, string_object_to_string(*source), error);
    if (parsed.tag != ValueTag::Invalid) {
      Value expression = ast_instance(state, "Expression");
      if (expression.tag == ValueTag::Invalid) {
        error = "missing _ast Expression class";
        return false;
      }
      object_set_attr(expression, "body", parsed, error);
      value_assign_fast(out, expression);
      return true;
    }
    error.clear();
    auto parsed_expression = parse_expression_source(string_object_to_string(*source), true);
    if (parsed_expression.errors.empty() && parsed_expression.expression != nullptr) {
      parsed = convert_parser_expr(state, *parsed_expression.expression, false, error);
      if (parsed.tag != ValueTag::Invalid) {
        Value expression = ast_instance(state, "Expression");
        if (expression.tag == ValueTag::Invalid) {
          error = "missing _ast Expression class";
          return false;
        }
        object_set_attr(expression, "body", parsed, error);
        value_assign_fast(out, expression);
        return true;
      }
    }
  }
  if (mode == "exec" || mode == "eval") {
    const std::string source_text = string_object_to_string(*source);
    std::string parse_error;
    if (mode == "exec") {
      const auto checked = parse_source(source_text);
      if (!checked.errors.empty()) parse_error = checked.errors.front();
    } else {
      const auto checked = parse_expression_source(source_text);
      if (!checked.errors.empty()) parse_error = checked.errors.front();
    }
    if (!parse_error.empty()) {
      error = parse_error;
      runtime.raise_class_error("SyntaxError", error);
      return false;
    }
  }
  error = "XLang3 _ast cannot represent this Python source";
  runtime.raise_class_error("NotImplementedError", error);
  return false;
}

bool ast_parse(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return ast_parse_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

std::string ast_dump_value(const Value& value);

std::string ast_dump_node(const Value& node) {
  auto* instance = value_as_instance(node);
  if (instance == nullptr) {
    return value_to_string(node);
  }
  std::string class_name = "AST";
  if (auto* klass = value_as_class(instance->klass)) {
    class_name = klass->name;
  }
  std::string text = class_name + "(";
  bool first = true;
  auto fields = fields_for(node);
  for (const auto& field : fields) {
    Value field_value;
    std::string ignored;
    if (!object_get_attr(node, field, field_value, ignored)) {
      continue;
    }
    if (!first) {
      text += ", ";
    }
    first = false;
    text += field;
    text += "=";
    text += ast_dump_value(field_value);
  }
  text += ")";
  return text;
}

std::string ast_dump_value(const Value& value) {
  if (value_as_instance(value) != nullptr) {
    return ast_dump_node(value);
  }
  if (auto* list = value_as_list(value)) {
    std::string text = "[";
    for (size_t i = 0; i < list->items.size(); ++i) {
      if (i != 0) {
        text += ", ";
      }
      text += ast_dump_value(list->items[i]);
    }
    text += "]";
    return text;
  }
  if (auto* tuple = value_as_tuple(value)) {
    std::string text = "(";
    for (size_t i = 0; i < tuple->items.size(); ++i) {
      if (i != 0) {
        text += ", ";
      }
      text += ast_dump_value(tuple->items[i]);
    }
    if (tuple->items.size() == 1) {
      text += ",";
    }
    text += ")";
    return text;
  }
  return value_to_string(value);
}

bool ast_dump(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "ast.dump() expected node";
    return false;
  }
  out = Value::string(ast_dump_value(args[0]));
  return true;
}

bool ast_iter_fields(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ast.iter_fields() expected node";
    return false;
  }
  std::vector<Value> items;
  for (const auto& field : fields_for(args[0])) {
    Value field_value;
    std::string ignored;
    if (object_get_attr(args[0], field, field_value, ignored)) {
      items.push_back(Value::tuple({Value::string(field), field_value}));
    }
  }
  out = Value::list(std::move(items));
  return true;
}

void enqueue_child_nodes(const Value& value, std::deque<Value>& queue) {
  if (value_as_instance(value) != nullptr) {
    queue.push_back(value);
    return;
  }
  if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      enqueue_child_nodes(item, queue);
    }
  } else if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      enqueue_child_nodes(item, queue);
    }
  }
}

bool ast_walk(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ast.walk() expected node";
    return false;
  }
  std::vector<Value> result;
  std::deque<Value> queue;
  queue.push_back(args[0]);
  while (!queue.empty()) {
    Value node = queue.front();
    queue.pop_front();
    result.push_back(node);
    for (const auto& field : fields_for(node)) {
      Value field_value;
      std::string ignored;
      if (object_get_attr(node, field, field_value, ignored)) {
        enqueue_child_nodes(field_value, queue);
      }
    }
  }
  out = Value::list(std::move(result));
  return true;
}

bool literal_from_node(const Value& node, Value& out, std::string& error) {
  auto* instance = value_as_instance(node);
  if (instance == nullptr) {
    out = node;
    return true;
  }
  auto* klass = value_as_class(instance->klass);
  const std::string name = klass == nullptr ? "" : klass->name;
  if (name == "Constant") {
    return object_get_attr(node, "value", out, error);
  }
  if (name == "List" || name == "Tuple" || name == "Set") {
    Value elts;
    if (!object_get_attr(node, "elts", elts, error)) {
      return false;
    }
    auto* list = value_as_list(elts);
    if (list == nullptr) {
      error = "literal container elts must be list";
      return false;
    }
    std::vector<Value> values;
    values.reserve(list->items.size());
    for (const auto& item : list->items) {
      Value literal;
      if (!literal_from_node(item, literal, error)) {
        return false;
      }
      values.push_back(std::move(literal));
    }
    if (name == "List") {
      out = Value::list(std::move(values));
    } else if (name == "Tuple") {
      out = Value::tuple(std::move(values));
    } else {
      out = Value::set(std::move(values));
    }
    return true;
  }
  if (name == "Dict") {
    Value keys;
    Value values_value;
    if (!object_get_attr(node, "keys", keys, error) || !object_get_attr(node, "values", values_value, error)) {
      return false;
    }
    auto* key_list = value_as_list(keys);
    auto* value_list = value_as_list(values_value);
    if (key_list == nullptr || value_list == nullptr || key_list->items.size() != value_list->items.size()) {
      error = "literal dict keys/values must be equal-size lists";
      return false;
    }
    std::vector<std::pair<Value, Value>> entries;
    entries.reserve(key_list->items.size());
    for (size_t i = 0; i < key_list->items.size(); ++i) {
      Value key;
      Value value;
      if (!literal_from_node(key_list->items[i], key, error) || !literal_from_node(value_list->items[i], value, error)) {
        return false;
      }
      entries.push_back({std::move(key), std::move(value)});
    }
    out = Value::dict(std::move(entries));
    return true;
  }
  error = "malformed node or string";
  return false;
}

bool ast_literal_eval(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ast.literal_eval() expected node";
    return false;
  }
  return literal_from_node(args[0], out, error);
}

std::string ast_node_class_name(const Value& node) {
  auto* instance = value_as_instance(node);
  if (instance == nullptr) {
    return {};
  }
  auto* klass = value_as_class(instance->klass);
  return klass == nullptr ? std::string() : klass->name;
}

bool node_visitor_generic_visit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "NodeVisitor.generic_visit() expected self and node";
    return false;
  }
  Value visit_method;
  if (!object_get_attr(args[0], "visit", visit_method, error)) {
    return false;
  }
  for (const auto& field : fields_for(args[1])) {
    Value field_value;
    std::string ignored;
    if (!object_get_attr(args[1], field, field_value, ignored)) {
      continue;
    }
    if (value_as_instance(field_value) != nullptr) {
      Value ignored_result;
      if (!runtime_call_callable(runtime, visit_method, &field_value, 1, ignored_result, error)) {
        return false;
      }
      continue;
    }
    if (auto* list = value_as_list(field_value)) {
      for (const auto& item : list->items) {
        if (value_as_instance(item) == nullptr) {
          continue;
        }
        Value ignored_result;
        if (!runtime_call_callable(runtime, visit_method, &item, 1, ignored_result, error)) {
          return false;
        }
      }
    }
  }
  value_set_none(out);
  return true;
}

bool node_visitor_visit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "NodeVisitor.visit() expected self and node";
    return false;
  }
  const auto class_name = ast_node_class_name(args[1]);
  if (!class_name.empty()) {
    Value method;
    std::string ignored;
    if (object_get_attr(args[0], "visit_" + class_name, method, ignored)) {
      return runtime_call_callable(runtime, method, &args[1], 1, out, error);
    }
  }
  Value generic_visit;
  if (!object_get_attr(args[0], "generic_visit", generic_visit, error)) {
    return false;
  }
  return runtime_call_callable(runtime, generic_visit, &args[1], 1, out, error);
}

Value make_node_visitor_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"visit", runtime.make_native_function("ast.NodeVisitor.visit", node_visitor_visit)});
  attrs.push_back({"generic_visit", runtime.make_native_function("ast.NodeVisitor.generic_visit", node_visitor_generic_visit)});
  return Value::class_object("NodeVisitor", std::move(attrs));
}

void add_class(NativeModuleBuilder& builder, AstState* state, const char* name, Value klass) {
  const auto canonical = state->classes.insert_or_assign(name, klass).first;
  state->kinds_by_class[value_as_class(klass)] = canonical->first;
  builder.value(name, std::move(klass));
}

std::string_view native_ast_node_kind(const Value& node, bool class_value, void* context) {
  auto* instance = value_as_instance(node);
  auto* klass = class_value ? value_as_class(node)
      : instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr) return {};
  const auto* state = static_cast<const AstState*>(context);
  const auto exact = state->kinds_by_class.find(klass);
  if (exact != state->kinds_by_class.end()) return exact->second;
  // Normal nodes take one identity lookup. User subclasses take the cached
  // MRO path, selecting their actual canonical node base without importing a
  // module, calling Python, or accepting an unrelated class with that name.
  const std::vector<Value>* mro = nullptr;
  std::string error;
  if (!class_get_mro_values(klass, mro, error)) return {};
  for (const auto& base : *mro) {
    const auto known = state->kinds_by_class.find(value_as_class(base));
    if (known != state->kinds_by_class.end()) return known->second;
  }
  return {};
}

struct NativeAstFieldSchema {
  const char* name;
  const char* fields;  // nullptr means the native abstract base has no schema.
};
#include "ast_field_schema.inc"

Value ast_schema_type(Runtime& runtime, AstState* state, std::string_view text) {
  if (text.size() > 6 && text.substr(0, 5) == "list[" && text.back() == ']') {
    const Value* list = runtime.find_builtin("list");
    return Value::generic_alias(*list, Value::tuple({ast_schema_type(runtime, state, text.substr(5, text.size() - 6))}));
  }
  if (text.find('|') != std::string_view::npos) {
    std::vector<Value> members;
    while (!text.empty()) {
      const auto delimiter = text.find('|');
      members.push_back(ast_schema_type(runtime, state, text.substr(0, delimiter)));
      if (delimiter == std::string_view::npos) break;
      text.remove_prefix(delimiter + 1);
    }
    return Value::union_type(std::move(members));
  }
  if (text == "NoneType") {
    Value none_type;
    runtime_type_of_value(runtime, Value::none(), none_type);
    return none_type;
  }
  auto native_class = state->classes.find(std::string(text));
  if (native_class != state->classes.end()) return native_class->second;
  const Value* builtin = runtime.find_builtin(std::string(text));
  if (builtin != nullptr) return *builtin;
  throw std::logic_error("unresolved native AST field type: " + std::string(text));
}

void install_ast_field_schemas(Runtime& runtime, AstState* state) {
  // Parse this generated native API metadata only at module initialization.
  // Constructors read ordinary mutable _field_types, preserving user subclass
  // overrides without rescanning ASDL or interpreting a schema on each node.
  std::string error;
  for (const auto& schema : kNativeAstFieldSchemas) {
    Value klass = node_class(state, schema.name);
    if (klass.tag == ValueTag::Invalid) throw std::logic_error("missing native AST class");
    object_set_attr(klass, "__module__", Value::string("ast"), error);
    if (schema.fields == nullptr) continue;
    Value types = Value::dict({});
    std::string_view fields(schema.fields);
    while (!fields.empty()) {
      const auto delimiter = fields.find(';');
      const auto field = fields.substr(0, delimiter);
      const auto equals = field.find('=');
      const std::string name(field.substr(0, equals));
      Value type = ast_schema_type(runtime, state, field.substr(equals + 1));
      mapping_set_item(types, Value::string(name), type, error);
      if (auto* alias = value_as_generic_alias(type); alias != nullptr && alias->is_union) {
        object_set_attr(klass, name, Value::none(), error);
      }
      if (delimiter == std::string_view::npos) break;
      fields.remove_prefix(delimiter + 1);
    }
    object_set_attr(klass, "_field_types", types, error);
    object_set_attr(klass, "__annotations__", types, error);
  }
  // CPython constructors reuse Load; a per-node allocation here would add an
  // avoidable cost to every Name/Attribute/Subscript constructed from Python.
  state->load_singleton = Value::instance(node_class(state, "Load"));
}

void fill_ast_module(Runtime& runtime, NativeModuleBuilder& builder, AstState* state) {
  Value object_base = runtime.find_builtin("object") != nullptr ? *runtime.find_builtin("object") : Value::invalid();
  state->ast_base = ast_class(runtime, "AST", object_base, {}, state);
  Value mod = ast_class(runtime, "mod", state->ast_base);
  Value stmt = ast_class(runtime, "stmt", state->ast_base);
  Value expr = ast_class(runtime, "expr", state->ast_base);
  Value expr_context = ast_class(runtime, "expr_context", state->ast_base);
  Value operator_type = ast_class(runtime, "operator", state->ast_base);
  Value unaryop = ast_class(runtime, "unaryop", state->ast_base);
  Value cmpop = ast_class(runtime, "cmpop", state->ast_base);
  Value boolop = ast_class(runtime, "boolop", state->ast_base);
  Value pattern = ast_class(runtime, "pattern", state->ast_base);
  Value match_case = ast_class(runtime, "match_case", state->ast_base,
                               {"pattern", "guard", "body"});
  Value type_ignore = ast_class(runtime, "type_ignore", state->ast_base);
  Value type_param = ast_class(runtime, "type_param", state->ast_base);
  Value excepthandler = ast_class(runtime, "excepthandler", state->ast_base);

  add_class(builder, state, "AST", state->ast_base);
  add_class(builder, state, "mod", mod);
  add_class(builder, state, "stmt", stmt);
  add_class(builder, state, "expr", expr);
  add_class(builder, state, "expr_context", expr_context);
  add_class(builder, state, "operator", operator_type);
  add_class(builder, state, "unaryop", unaryop);
  add_class(builder, state, "cmpop", cmpop);
  add_class(builder, state, "boolop", boolop);
  add_class(builder, state, "pattern", pattern);
  add_class(builder, state, "match_case", match_case);
  add_class(builder, state, "type_ignore", type_ignore);
  add_class(builder, state, "type_param", type_param);
  add_class(builder, state, "excepthandler", excepthandler);

  add_class(builder, state, "Module", ast_class(runtime, "Module", mod, {"body", "type_ignores"}));
  add_class(builder, state, "Interactive", ast_class(runtime, "Interactive", mod, {"body"}));
  add_class(builder, state, "Expression", ast_class(runtime, "Expression", mod, {"body"}));
  add_class(builder, state, "FunctionType", ast_class(runtime, "FunctionType", mod, {"argtypes", "returns"}));
  add_class(builder, state, "FunctionDef", ast_class(runtime, "FunctionDef", stmt, {"name", "args", "body", "decorator_list", "returns", "type_comment", "type_params"}));
  add_class(builder, state, "AsyncFunctionDef", ast_class(runtime, "AsyncFunctionDef", stmt, {"name", "args", "body", "decorator_list", "returns", "type_comment", "type_params"}));
  add_class(builder, state, "ClassDef", ast_class(runtime, "ClassDef", stmt, {"name", "bases", "keywords", "body", "decorator_list", "type_params"}));
  add_class(builder, state, "Return", ast_class(runtime, "Return", stmt, {"value"}));
  add_class(builder, state, "Delete", ast_class(runtime, "Delete", stmt, {"targets"}));
  add_class(builder, state, "Assign", ast_class(runtime, "Assign", stmt, {"targets", "value", "type_comment"}));
  add_class(builder, state, "TypeAlias", ast_class(runtime, "TypeAlias", stmt, {"name", "type_params", "value"}));
  add_class(builder, state, "AugAssign", ast_class(runtime, "AugAssign", stmt, {"target", "op", "value"}));
  add_class(builder, state, "AnnAssign", ast_class(runtime, "AnnAssign", stmt, {"target", "annotation", "value", "simple"}));
  add_class(builder, state, "For", ast_class(runtime, "For", stmt, {"target", "iter", "body", "orelse", "type_comment"}));
  add_class(builder, state, "AsyncFor", ast_class(runtime, "AsyncFor", stmt, {"target", "iter", "body", "orelse", "type_comment"}));
  add_class(builder, state, "While", ast_class(runtime, "While", stmt, {"test", "body", "orelse"}));
  add_class(builder, state, "If", ast_class(runtime, "If", stmt, {"test", "body", "orelse"}));
  add_class(builder, state, "With", ast_class(runtime, "With", stmt, {"items", "body", "type_comment"}));
  add_class(builder, state, "AsyncWith", ast_class(runtime, "AsyncWith", stmt, {"items", "body", "type_comment"}));
  add_class(builder, state, "Match", ast_class(runtime, "Match", stmt, {"subject", "cases"}));
  add_class(builder, state, "Raise", ast_class(runtime, "Raise", stmt, {"exc", "cause"}));
  add_class(builder, state, "Try", ast_class(runtime, "Try", stmt, {"body", "handlers", "orelse", "finalbody"}));
  add_class(builder, state, "TryStar", ast_class(runtime, "TryStar", stmt, {"body", "handlers", "orelse", "finalbody"}));
  add_class(builder, state, "Assert", ast_class(runtime, "Assert", stmt, {"test", "msg"}));
  add_class(builder, state, "Import", ast_class(runtime, "Import", stmt, {"names"}));
  add_class(builder, state, "ImportFrom", ast_class(runtime, "ImportFrom", stmt, {"module", "names", "level"}));
  add_class(builder, state, "Global", ast_class(runtime, "Global", stmt, {"names"}));
  add_class(builder, state, "Nonlocal", ast_class(runtime, "Nonlocal", stmt, {"names"}));
  add_class(builder, state, "Expr", ast_class(runtime, "Expr", stmt, {"value"}));
  add_class(builder, state, "Pass", ast_class(runtime, "Pass", stmt));
  add_class(builder, state, "Break", ast_class(runtime, "Break", stmt));
  add_class(builder, state, "Continue", ast_class(runtime, "Continue", stmt));
  add_class(builder, state, "BoolOp", ast_class(runtime, "BoolOp", expr, {"op", "values"}));
  add_class(builder, state, "NamedExpr", ast_class(runtime, "NamedExpr", expr, {"target", "value"}));
  add_class(builder, state, "Lambda", ast_class(runtime, "Lambda", expr, {"args", "body"}));
  add_class(builder, state, "IfExp", ast_class(runtime, "IfExp", expr, {"test", "body", "orelse"}));
  Value constant_class = ast_class(runtime, "Constant", expr, {"value", "kind"});
  if (auto* klass = value_as_class(constant_class)) {
    // ``kind`` is an optional ASDL field.  CPython leaves it out of the
    // instance dictionary when omitted, while its field descriptor returns
    // None.  A class default gives XLang3 the same observable behavior.
    klass->attrs["kind"] = Value::none();
  }
  add_class(builder, state, "Constant", std::move(constant_class));
  add_class(builder, state, "Name", ast_class(runtime, "Name", expr, {"id", "ctx"}));
  add_class(builder, state, "List", ast_class(runtime, "List", expr, {"elts", "ctx"}));
  add_class(builder, state, "Tuple", ast_class(runtime, "Tuple", expr, {"elts", "ctx"}));
  add_class(builder, state, "Dict", ast_class(runtime, "Dict", expr, {"keys", "values"}));
  add_class(builder, state, "Set", ast_class(runtime, "Set", expr, {"elts"}));
  add_class(builder, state, "ListComp", ast_class(runtime, "ListComp", expr, {"elt", "generators"}));
  add_class(builder, state, "SetComp", ast_class(runtime, "SetComp", expr, {"elt", "generators"}));
  add_class(builder, state, "DictComp", ast_class(runtime, "DictComp", expr, {"key", "value", "generators"}));
  add_class(builder, state, "GeneratorExp", ast_class(runtime, "GeneratorExp", expr, {"elt", "generators"}));
  add_class(builder, state, "Await", ast_class(runtime, "Await", expr, {"value"}));
  add_class(builder, state, "Yield", ast_class(runtime, "Yield", expr, {"value"}));
  add_class(builder, state, "YieldFrom", ast_class(runtime, "YieldFrom", expr, {"value"}));
  add_class(builder, state, "Compare", ast_class(runtime, "Compare", expr, {"left", "ops", "comparators"}));
  add_class(builder, state, "BinOp", ast_class(runtime, "BinOp", expr, {"left", "op", "right"}));
  add_class(builder, state, "UnaryOp", ast_class(runtime, "UnaryOp", expr, {"op", "operand"}));
  add_class(builder, state, "Call", ast_class(runtime, "Call", expr, {"func", "args", "keywords"}));
  add_class(builder, state, "FormattedValue", ast_class(runtime, "FormattedValue", expr, {"value", "conversion", "format_spec"}));
  add_class(builder, state, "JoinedStr", ast_class(runtime, "JoinedStr", expr, {"values"}));
  add_class(builder, state, "Attribute", ast_class(runtime, "Attribute", expr, {"value", "attr", "ctx"}));
  add_class(builder, state, "Subscript", ast_class(runtime, "Subscript", expr, {"value", "slice", "ctx"}));
  add_class(builder, state, "Starred", ast_class(runtime, "Starred", expr, {"value", "ctx"}));
  add_class(builder, state, "Slice", ast_class(runtime, "Slice", expr, {"lower", "upper", "step"}));
  add_class(builder, state, "Index", ast_class(runtime, "Index", state->ast_base));
  add_class(builder, state, "ExtSlice", ast_class(runtime, "ExtSlice", state->ast_base));
  add_class(builder, state, "comprehension", ast_class(runtime, "comprehension", state->ast_base, {"target", "iter", "ifs", "is_async"}));
  add_class(builder, state, "arguments", ast_class(runtime, "arguments", state->ast_base, {"posonlyargs", "args", "vararg", "kwonlyargs", "kw_defaults", "kwarg", "defaults"}));
  add_class(builder, state, "arg", ast_class(runtime, "arg", state->ast_base, {"arg", "annotation", "type_comment"}));
  add_class(builder, state, "keyword", ast_class(runtime, "keyword", state->ast_base, {"arg", "value"}));
  add_class(builder, state, "alias", ast_class(runtime, "alias", state->ast_base, {"name", "asname"}));
  add_class(builder, state, "withitem", ast_class(runtime, "withitem", state->ast_base, {"context_expr", "optional_vars"}));
  add_class(builder, state, "ExceptHandler", ast_class(runtime, "ExceptHandler", excepthandler, {"type", "name", "body"}));
  add_class(builder, state, "Load", ast_class(runtime, "Load", expr_context));
  add_class(builder, state, "Store", ast_class(runtime, "Store", expr_context));
  add_class(builder, state, "Del", ast_class(runtime, "Del", expr_context));
  add_class(builder, state, "And", ast_class(runtime, "And", boolop));
  add_class(builder, state, "Or", ast_class(runtime, "Or", boolop));
  add_class(builder, state, "Add", ast_class(runtime, "Add", operator_type));
  add_class(builder, state, "Sub", ast_class(runtime, "Sub", operator_type));
  add_class(builder, state, "Mult", ast_class(runtime, "Mult", operator_type));
  add_class(builder, state, "MatMult", ast_class(runtime, "MatMult", operator_type));
  add_class(builder, state, "Div", ast_class(runtime, "Div", operator_type));
  add_class(builder, state, "Mod", ast_class(runtime, "Mod", operator_type));
  add_class(builder, state, "Pow", ast_class(runtime, "Pow", operator_type));
  add_class(builder, state, "LShift", ast_class(runtime, "LShift", operator_type));
  add_class(builder, state, "RShift", ast_class(runtime, "RShift", operator_type));
  add_class(builder, state, "BitOr", ast_class(runtime, "BitOr", operator_type));
  add_class(builder, state, "BitXor", ast_class(runtime, "BitXor", operator_type));
  add_class(builder, state, "BitAnd", ast_class(runtime, "BitAnd", operator_type));
  add_class(builder, state, "FloorDiv", ast_class(runtime, "FloorDiv", operator_type));
  add_class(builder, state, "Invert", ast_class(runtime, "Invert", unaryop));
  add_class(builder, state, "Not", ast_class(runtime, "Not", unaryop));
  add_class(builder, state, "UAdd", ast_class(runtime, "UAdd", unaryop));
  add_class(builder, state, "USub", ast_class(runtime, "USub", unaryop));
  add_class(builder, state, "Eq", ast_class(runtime, "Eq", cmpop));
  add_class(builder, state, "NotEq", ast_class(runtime, "NotEq", cmpop));
  add_class(builder, state, "Lt", ast_class(runtime, "Lt", cmpop));
  add_class(builder, state, "LtE", ast_class(runtime, "LtE", cmpop));
  add_class(builder, state, "Gt", ast_class(runtime, "Gt", cmpop));
  add_class(builder, state, "GtE", ast_class(runtime, "GtE", cmpop));
  add_class(builder, state, "Is", ast_class(runtime, "Is", cmpop));
  add_class(builder, state, "IsNot", ast_class(runtime, "IsNot", cmpop));
  add_class(builder, state, "In", ast_class(runtime, "In", cmpop));
  add_class(builder, state, "NotIn", ast_class(runtime, "NotIn", cmpop));
  add_class(builder, state, "MatchValue", ast_class(runtime, "MatchValue", pattern, {"value"}));
  add_class(builder, state, "MatchSingleton", ast_class(runtime, "MatchSingleton", pattern, {"value"}));
  add_class(builder, state, "MatchSequence", ast_class(runtime, "MatchSequence", pattern, {"patterns"}));
  add_class(builder, state, "MatchMapping", ast_class(runtime, "MatchMapping", pattern, {"keys", "patterns", "rest"}));
  add_class(builder, state, "MatchClass", ast_class(runtime, "MatchClass", pattern, {"cls", "patterns", "kwd_attrs", "kwd_patterns"}));
  add_class(builder, state, "MatchStar", ast_class(runtime, "MatchStar", pattern, {"name"}));
  add_class(builder, state, "MatchAs", ast_class(runtime, "MatchAs", pattern, {"pattern", "name"}));
  add_class(builder, state, "MatchOr", ast_class(runtime, "MatchOr", pattern, {"patterns"}));
  add_class(builder, state, "TypeIgnore", ast_class(runtime, "TypeIgnore", type_ignore, {"lineno", "tag"}));
  add_class(builder, state, "TypeVar", ast_class(runtime, "TypeVar", type_param, {"name", "bound", "default_value"}));
  add_class(builder, state, "ParamSpec", ast_class(runtime, "ParamSpec", type_param, {"name", "default_value"}));
  add_class(builder, state, "TypeVarTuple", ast_class(runtime, "TypeVarTuple", type_param, {"name", "default_value"}));
  add_class(builder, state, "TemplateStr", ast_class(runtime, "TemplateStr", expr, {"values"}));
  add_class(builder, state, "Interpolation", ast_class(runtime, "Interpolation", expr, {"value", "str", "conversion", "format_spec"}));
  install_ast_field_schemas(runtime, state);

  builder.value("PyCF_ONLY_AST", Value::int64(0x0400))
      .value("PyCF_TYPE_COMMENTS", Value::int64(0x1000))
      .value("PyCF_ALLOW_TOP_LEVEL_AWAIT", Value::int64(0x2000))
      .value("PyCF_OPTIMIZED_AST", Value::int64(0x4000))
      .value("parse", runtime.make_native_function("ast.parse", ast_parse, state, nullptr, nullptr, false, ast_parse_kw))
      .function("dump", ast_dump)
      .function("iter_fields", ast_iter_fields)
      .function("walk", ast_walk)
      .function("literal_eval", ast_literal_eval)
      .value("NodeVisitor", make_node_visitor_class(runtime));
}

} // namespace

void register_ast_module(Runtime& runtime) {
  auto* private_state = new AstState();
  private_state->runtime = &runtime;
  runtime.register_native_package_cleanup(private_state, [](void* data) {
    auto* state = static_cast<AstState*>(data);
    state->runtime->register_native_ast_kind_lookup(nullptr, nullptr);
    // Release the instance while its class registry and callback state still
    // exist; a user-added Load finalizer may re-enter native AST helpers.
    value_set_none(state->load_singleton);
    delete state;
  });
  NativeModuleBuilder private_builder(runtime, "_ast");
  fill_ast_module(runtime, private_builder, private_state);
  runtime.register_native_ast_kind_lookup(native_ast_node_kind, private_state);
  Value private_module = private_builder.finish();
  // Public __module__ is "ast", but these classes are owned by native _ast.
  // Give generic cycle discovery the actual owner instead of rescanning each
  // canonical class's metadata as a local graph on every gc.collect(). The
  // collector still verifies that the owner holds the class; rebinding remains
  // observable, and Runtime teardown clears module slots before this state.
  for (const auto& entry : private_state->classes) {
    value_assign_fast(value_as_class(entry.second)->globals_module, private_module);
  }
  runtime.register_module("_ast", std::move(private_module));
}

} // namespace xlang3
