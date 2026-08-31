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
  size_t close = text.find(')', name_end + 1);
  if (close == std::string_view::npos) {
    return false;
  }
  std::string_view params = text.substr(name_end + 1, close - name_end - 1);
  if (params.find_first_not_of(" \t\r\n") != std::string_view::npos) {
    return false;
  }

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
