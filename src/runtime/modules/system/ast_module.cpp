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

#include "xlang3/module_object.h"

namespace xlang3 {

namespace {

Value ast_class(const char* name, const Value& base) {
  return Value::class_object(name, {}, base);
}

} // namespace

void register_ast_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_ast");

  Value object_base = runtime.find_builtin("object") != nullptr ? *runtime.find_builtin("object") : Value::invalid();
  Value ast = ast_class("AST", object_base);
  Value mod = ast_class("mod", ast);
  Value stmt = ast_class("stmt", ast);
  Value expr = ast_class("expr", ast);
  Value expr_context = ast_class("expr_context", ast);
  Value operator_type = ast_class("operator", ast);
  Value unaryop = ast_class("unaryop", ast);
  Value cmpop = ast_class("cmpop", ast);
  Value boolop = ast_class("boolop", ast);
  Value comprehension = ast_class("comprehension", ast);
  Value arguments = ast_class("arguments", ast);
  Value arg = ast_class("arg", ast);
  Value keyword = ast_class("keyword", ast);
  Value alias = ast_class("alias", ast);
  Value withitem = ast_class("withitem", ast);
  Value excepthandler = ast_class("excepthandler", ast);
  Value pattern = ast_class("pattern", ast);
  Value type_ignore = ast_class("type_ignore", ast);

  builder.value("AST", ast)
      .value("mod", mod)
      .value("stmt", stmt)
      .value("expr", expr)
      .value("expr_context", expr_context)
      .value("operator", operator_type)
      .value("unaryop", unaryop)
      .value("cmpop", cmpop)
      .value("boolop", boolop)
      .value("comprehension", comprehension)
      .value("arguments", arguments)
      .value("arg", arg)
      .value("keyword", keyword)
      .value("alias", alias)
      .value("withitem", withitem)
      .value("excepthandler", excepthandler)
      .value("pattern", pattern)
      .value("type_ignore", type_ignore)
      .value("PyCF_ONLY_AST", Value::int64(0x0400))
      .value("PyCF_TYPE_COMMENTS", Value::int64(0x1000))
      .value("PyCF_ALLOW_TOP_LEVEL_AWAIT", Value::int64(0x2000))
      .value("PyCF_OPTIMIZED_AST", Value::int64(0x4000));

  const std::pair<const char*, const Value*> classes[] = {
      {"Module", &mod}, {"Interactive", &mod}, {"Expression", &mod}, {"FunctionType", &mod},
      {"FunctionDef", &stmt}, {"AsyncFunctionDef", &stmt}, {"ClassDef", &stmt}, {"Return", &stmt},
      {"Delete", &stmt}, {"Assign", &stmt}, {"TypeAlias", &stmt}, {"AugAssign", &stmt}, {"AnnAssign", &stmt},
      {"For", &stmt}, {"AsyncFor", &stmt}, {"While", &stmt}, {"If", &stmt}, {"With", &stmt},
      {"AsyncWith", &stmt}, {"Match", &stmt}, {"Raise", &stmt}, {"Try", &stmt}, {"TryStar", &stmt},
      {"Assert", &stmt}, {"Import", &stmt}, {"ImportFrom", &stmt}, {"Global", &stmt},
      {"Nonlocal", &stmt}, {"Expr", &stmt}, {"Pass", &stmt}, {"Break", &stmt}, {"Continue", &stmt},
      {"BoolOp", &expr}, {"NamedExpr", &expr}, {"BinOp", &expr}, {"UnaryOp", &expr}, {"Lambda", &expr},
      {"IfExp", &expr}, {"Dict", &expr}, {"Set", &expr}, {"ListComp", &expr}, {"SetComp", &expr},
      {"DictComp", &expr}, {"GeneratorExp", &expr}, {"Await", &expr}, {"Yield", &expr},
      {"YieldFrom", &expr}, {"Compare", &expr}, {"Call", &expr}, {"FormattedValue", &expr},
      {"JoinedStr", &expr}, {"Constant", &expr}, {"Attribute", &expr}, {"Subscript", &expr},
      {"Starred", &expr}, {"Name", &expr}, {"List", &expr}, {"Tuple", &expr}, {"Slice", &expr},
      {"Load", &expr_context}, {"Store", &expr_context}, {"Del", &expr_context},
      {"And", &boolop}, {"Or", &boolop}, {"Add", &operator_type}, {"Sub", &operator_type},
      {"Mult", &operator_type}, {"MatMult", &operator_type}, {"Div", &operator_type}, {"Mod", &operator_type},
      {"Pow", &operator_type}, {"LShift", &operator_type}, {"RShift", &operator_type}, {"BitOr", &operator_type},
      {"BitXor", &operator_type}, {"BitAnd", &operator_type}, {"FloorDiv", &operator_type},
      {"Invert", &unaryop}, {"Not", &unaryop}, {"UAdd", &unaryop}, {"USub", &unaryop},
      {"Eq", &cmpop}, {"NotEq", &cmpop}, {"Lt", &cmpop}, {"LtE", &cmpop}, {"Gt", &cmpop},
      {"GtE", &cmpop}, {"Is", &cmpop}, {"IsNot", &cmpop}, {"In", &cmpop}, {"NotIn", &cmpop},
      {"ExceptHandler", &excepthandler}, {"MatchValue", &pattern}, {"MatchSingleton", &pattern},
      {"MatchSequence", &pattern}, {"MatchMapping", &pattern}, {"MatchClass", &pattern},
      {"MatchStar", &pattern}, {"MatchAs", &pattern}, {"MatchOr", &pattern}, {"TypeIgnore", &type_ignore},
  };
  for (const auto& entry : classes) {
    builder.value(entry.first, ast_class(entry.first, *entry.second));
  }

  runtime.register_module("_ast", builder.finish());
}

} // namespace xlang3
