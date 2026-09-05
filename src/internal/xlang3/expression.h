#pragma once

#include "xlang3/value.h"

namespace xlang3 {
namespace ast { struct Expr; }

struct ExpressionNode {
  std::string op;
  Value value;
  std::vector<ExpressionNode> children;
};

struct ExpressionObject {
  Object header{ObjectKind::Expression, 1};
  ExpressionNode root;
};

Value capture_expression(const ast::Expr& expr, const std::string& assignment = {}, bool expansion = false);
bool expression_capture_enabled(const Value& callable);
bool encode_expression(const Value& expression, std::string& bytes, std::string& error);
bool decode_expression(const std::string& bytes, Value& expression, std::string& error);
bool inspect_expression(const Value& expression, Value& result, std::string& error);
bool evaluate_expression(const Value& expression, const Value& bindings,
                         Value& result, Value& reservations, std::string& error);
}
