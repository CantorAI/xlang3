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
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cctype>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3 {

namespace {

struct AstState {
  Value ast_base;
  std::unordered_map<std::string, Value> classes;
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

bool ast_node_init_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "AST.__init__() missing self";
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  auto fields = fields_for(self);
  if (argc - 1 > fields.size()) {
    error = "AST constructor got too many positional arguments";
    return false;
  }
  for (uint32_t i = 1; i < argc; ++i) {
    if (!object_set_attr(self, fields[i - 1], args[i], error)) {
      return false;
    }
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (!object_set_attr(self, kwargs[i].name, *kwargs[i].value, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool ast_node_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return ast_node_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

Value ast_class(Runtime& runtime, const char* name, const Value& base, std::initializer_list<const char*> fields = {}) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"_fields", Value::tuple(field_tuple(fields))});
  attrs.push_back({"_field_types", Value::dict({})});
  attrs.push_back({"__match_args__", Value::tuple(field_tuple(fields))});
  if (std::string(name) == "AST") {
    attrs.push_back({"__init__", runtime.make_native_function("_ast.AST.__init__", ast_node_init, nullptr, nullptr, nullptr, false, ast_node_init_kw)});
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
      Value function = ast_parse_simple_expr(
          state, source.substr(0, call_open), error, source_line, column_offset);
      if (function.tag == ValueTag::Invalid) {
        error.clear();
        function = ast_make_constant(state, Value::none(), error);
        ast_set_location(
            function, source_line, source_line, column_offset,
            column_offset + static_cast<uint32_t>(call_open), error);
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
    Value constant = ast_make_constant(state, Value::string(std::string(source.substr(1, source.size() - 2))), error);
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
  }
  Value klass = mode == "eval" ? node_class(state, "Expression") : node_class(state, "Module");
  out = Value::instance(klass);
  object_set_attr(out, "source", args[0], error);
  if (mode == "eval") {
    object_set_attr(out, "body", Value::none(), error);
  } else {
    object_set_attr(out, "body", Value::list({}), error);
    object_set_attr(out, "type_ignores", Value::list({}), error);
  }
  return true;
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
  state->classes[name] = klass;
  builder.value(name, std::move(klass));
}

void fill_ast_module(Runtime& runtime, NativeModuleBuilder& builder, AstState* state) {
  Value object_base = runtime.find_builtin("object") != nullptr ? *runtime.find_builtin("object") : Value::invalid();
  state->ast_base = ast_class(runtime, "AST", object_base);
  Value mod = ast_class(runtime, "mod", state->ast_base);
  Value stmt = ast_class(runtime, "stmt", state->ast_base);
  Value expr = ast_class(runtime, "expr", state->ast_base);
  Value expr_context = ast_class(runtime, "expr_context", state->ast_base);
  Value operator_type = ast_class(runtime, "operator", state->ast_base);
  Value unaryop = ast_class(runtime, "unaryop", state->ast_base);
  Value cmpop = ast_class(runtime, "cmpop", state->ast_base);
  Value boolop = ast_class(runtime, "boolop", state->ast_base);
  Value pattern = ast_class(runtime, "pattern", state->ast_base);
  Value type_ignore = ast_class(runtime, "type_ignore", state->ast_base);
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
  add_class(builder, state, "type_ignore", type_ignore);
  add_class(builder, state, "excepthandler", excepthandler);

  add_class(builder, state, "Module", ast_class(runtime, "Module", mod, {"body", "type_ignores"}));
  add_class(builder, state, "Interactive", ast_class(runtime, "Interactive", mod, {"body"}));
  add_class(builder, state, "Expression", ast_class(runtime, "Expression", mod, {"body"}));
  add_class(builder, state, "FunctionType", ast_class(runtime, "FunctionType", mod, {"argtypes", "returns"}));
  add_class(builder, state, "FunctionDef", ast_class(runtime, "FunctionDef", stmt, {"name", "args", "body", "decorator_list", "returns", "type_comment"}));
  add_class(builder, state, "AsyncFunctionDef", ast_class(runtime, "AsyncFunctionDef", stmt, {"name", "args", "body", "decorator_list", "returns", "type_comment"}));
  add_class(builder, state, "ClassDef", ast_class(runtime, "ClassDef", stmt, {"name", "bases", "keywords", "body", "decorator_list"}));
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
  add_class(builder, state, "Constant", ast_class(runtime, "Constant", expr, {"value", "kind"}));
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
  runtime.register_native_package_cleanup(private_state, [](void* data) { delete static_cast<AstState*>(data); });
  NativeModuleBuilder private_builder(runtime, "_ast");
  fill_ast_module(runtime, private_builder, private_state);
  runtime.register_module("_ast", private_builder.finish());
}

} // namespace xlang3
