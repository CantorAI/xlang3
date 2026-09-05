#include "xlang3/expression.h"
#include "xlang3/ast.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "serialize/block_stream.h"
#include "serialize/ipc_value_marshal.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace xlang3 {
namespace {
ExpressionNode capture(const ast::Expr& expr, unsigned depth) {
  if (depth > 128) return {"error", Value::string("expression nesting exceeds 128"), {}};
  if (auto* p = dynamic_cast<const ast::NameExpr*>(&expr)) return {"name", Value::string(p->name), {}};
  if (auto* p = dynamic_cast<const ast::LiteralExpr*>(&expr)) {
    Value v;
    switch (p->kind) {
      case ast::LiteralExpr::Kind::None: v = Value::none(); break;
      case ast::LiteralExpr::Kind::Bool: v = Value::boolean(p->bool_value); break;
      case ast::LiteralExpr::Kind::String: v = Value::string(p->text); break;
      case ast::LiteralExpr::Kind::Int:
        try {
          auto text = p->text;
          text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
          int base = 10;
          if (text.size() > 2 && text[0] == '0') {
            const char prefix = text[1];
            if (prefix == 'x' || prefix == 'X') base = 16;
            if (prefix == 'b' || prefix == 'B') base = 2;
            if (prefix == 'o' || prefix == 'O') base = 8;
            if (base != 10) text.erase(0, 2);
          }
          size_t used = 0;
          v = Value::int64(std::stoll(text, &used, base));
          if (used != text.size()) throw std::invalid_argument("integer");
        }
        catch (...) { return {"error", Value::string("unsupported expression integer"), {}}; }
        break;
      case ast::LiteralExpr::Kind::Double: {
        auto text = p->text;
        text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
        try { v = Value::number(std::stod(text)); }
        catch (...) { return {"error", Value::string("unsupported expression number"), {}}; }
        break;
      }
      default: return {"error", Value::string("unsupported expression literal"), {}};
    }
    return {"literal", std::move(v), {}};
  }
  if (auto* p = dynamic_cast<const ast::UnaryExpr*>(&expr))
    return {p->op, Value::none(), {capture(*p->expr, depth + 1)}};
  if (auto* p = dynamic_cast<const ast::BinaryExpr*>(&expr))
    return {p->op, Value::none(), {capture(*p->lhs, depth + 1), capture(*p->rhs, depth + 1)}};
  if (auto* p = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
    ExpressionNode n{"chain", Value::none(), {capture(*p->first, depth + 1)}};
    for (const auto& item : p->comparisons)
      n.children.push_back({item.first, Value::none(), {capture(*item.second, depth + 1)}});
    return n;
  }
  return {"error", Value::string("unsupported captured expression syntax"), {}};
}

void assign_first(ExpressionNode& node, const std::string& name) {
  if ((node.op == "and" || node.op == "or") && node.children.size() == 2) {
    assign_first(node.children[0], name);
  } else {
    ExpressionNode old = std::move(node);
    node = {"reserve", Value::string(name), {std::move(old)}};
  }
}

Value own(ExpressionNode node) {
  auto* object = new ExpressionObject();
  object->root = std::move(node);
  Value result;
  result.tag = ValueTag::Object;
  result.as.obj = &object->header;
  return result;
}

bool read_node(serialize::BlockStream& stream, ExpressionNode& node, unsigned depth,
               unsigned& count, std::string& error) {
  if (depth > 128 || ++count > 4096) { error = "expression exceeds structural limits"; return false; }
  auto scalar = [&](serialize::IpcWireValue& value) {
    if (!stream.CopyTo(reinterpret_cast<char*>(&value.kind), sizeof(value.kind))) return false;
    switch (value.kind) {
      case serialize::IpcWireValueKind::None: return true;
      case serialize::IpcWireValueKind::Bool: {
        uint8_t flag = 0;
        if (!stream.CopyTo(reinterpret_cast<char*>(&flag), 1) || flag > 1) return false;
        value.bool_value = flag != 0;
        return true;
      }
      case serialize::IpcWireValueKind::Int64: return stream.CopyTo(reinterpret_cast<char*>(&value.int_value), sizeof(int64_t));
      case serialize::IpcWireValueKind::Double: return stream.CopyTo(reinterpret_cast<char*>(&value.double_value), sizeof(double));
      case serialize::IpcWireValueKind::String: {
        uint32_t size = 0;
        if (!stream.CopyTo(reinterpret_cast<char*>(&size), sizeof(size)) || size > 1024 * 1024) return false;
        value.bytes.resize(size);
        return stream.CopyTo(value.bytes.data(), size);
      }
      default: return false;
    }
  };
  serialize::IpcWireValue op, value;
  if (!scalar(op) || op.kind != serialize::IpcWireValueKind::String) return false;
  node.op = op.bytes;
  if (!scalar(value)) return false;
  switch (value.kind) {
    case serialize::IpcWireValueKind::None: node.value = Value::none(); break;
    case serialize::IpcWireValueKind::Bool: node.value = Value::boolean(value.bool_value); break;
    case serialize::IpcWireValueKind::Int64: node.value = Value::int64(value.int_value); break;
    case serialize::IpcWireValueKind::Double: node.value = Value::number(value.double_value); break;
    case serialize::IpcWireValueKind::String: node.value = Value::string(value.bytes); break;
    default: error = "invalid expression literal"; return false;
  }
  uint32_t children = 0;
  if (!stream.CopyTo(reinterpret_cast<char*>(&children), sizeof(children)) || children > 4096 - count) return false;
  node.children.resize(children);
  for (auto& child : node.children)
    if (!read_node(stream, child, depth + 1, count, error)) return false;
  return true;
}

bool lookup(const Value& dict, const Value& key, Value& out, std::string& error) {
  return mapping_get_item(dict, key, out, error);
}

bool eval(const ExpressionNode& n, const Value& bindings, Value& pending, Value& out, std::string& error) {
  if (n.op == "literal") { out = n.value; return true; }
  if (n.op == "name") {
    if (!lookup(bindings, n.value, out, error)) return false;
    Value used;
    std::string missing;
    if (lookup(pending, n.value, used, missing)) {
      Value remaining;
      if (!value_sub(out, used, remaining, error)) return false;
      out = std::move(remaining);
    }
    return true;
  }
  if (n.op == "error") { error = value_to_string(n.value); return false; }
  if (n.children.empty()) { error = "invalid expression node"; return false; }
  Value left;
  Value before;
  if (n.op == "or") before = mapping_copy(pending);
  if (!eval(n.children[0], bindings, pending, left, error)) return false;
  if (n.op == "reserve" && n.children.size() == 1) {
    Value available, used = Value::int64(0);
    if (!lookup(bindings, n.value, available, error)) return false;
    std::string missing;
    Value previous;
    if (lookup(pending, n.value, previous, missing)) used = previous;
    auto numeric = [](const Value& v) { return v.tag == ValueTag::Int64 || v.tag == ValueTag::Double; };
    if (!numeric(left) || !numeric(available) || !numeric(used)) { error = "reservation requires numeric resources"; return false; }
    const double amount = left.tag == ValueTag::Double ? left.as.f64 : static_cast<double>(left.as.i64);
    if (!std::isfinite(amount) || amount < 0) { error = "invalid reservation amount"; return false; }
    Value total, fits;
    if (!value_add(used, left, total, error) || !value_compare("<=", total, available, fits, error)) return false;
    out = fits;
    return !value_truthy(fits) || mapping_set_item(pending, n.value, total, error);
  }
  if (n.op == "chain") {
    for (size_t i = 1; i < n.children.size(); ++i) {
      const auto& comparison = n.children[i];
      if (comparison.children.size() != 1) { error = "invalid comparison chain"; return false; }
      Value right, matched;
      if (!eval(comparison.children[0], bindings, pending, right, error) ||
          !value_compare(comparison.op, left, right, matched, error)) return false;
      if (!value_truthy(matched)) { out = Value::boolean(false); return true; }
      left = std::move(right);
    }
    out = Value::boolean(true); return true;
  }
  if (n.children.size() == 1) {
    if (n.op == "not") { out = Value::boolean(!value_truthy(left)); return true; }
    if (n.op == "-") return value_sub(Value::int64(0), left, out, error);
    if (n.op == "+") { out = left; return true; }
  }
  if (n.children.size() != 2) { error = "invalid expression arity"; return false; }
  if (n.op == "and" && !value_truthy(left)) { out = left; return true; }
  if (n.op == "or" && value_truthy(left)) { out = left; return true; }
  if (n.op == "or") pending = std::move(before);
  Value right;
  if (!eval(n.children[1], bindings, pending, right, error)) return false;
  if (n.op == "and" || n.op == "or") { out = right; return true; }
  if (n.op == "+") return value_add(left, right, out, error);
  if (n.op == "-") return value_sub(left, right, out, error);
  if (n.op == "*") return value_mul(left, right, out, error);
  if (n.op == "/") return value_div(left, right, out, error);
  if (n.op == "//") return value_floor_div(left, right, out, error);
  if (n.op == "%") return value_mod(left, right, out, error);
  if (n.op == "**") return value_pow(left, right, out, error);
  return value_compare(n.op, left, right, out, error);
}
}

Value capture_expression(const ast::Expr& expr, const std::string& assignment, bool expansion) {
  if (expansion) return own({"error", Value::string("argument expansion is not supported in captured expressions"), {}});
  auto node = capture(expr, 0);
  if (!assignment.empty()) assign_first(node, assignment);
  return own(std::move(node));
}

bool expression_capture_enabled(const Value& callable) {
  if (auto* f = value_as_native_function(callable)) return f->capture_expressions;
  if (auto* m = value_as_bound_method(callable)) return expression_capture_enabled(m->function);
  if (auto* i = value_as_instance(callable)) {
    for (const auto& attr : i->attrs)
      if (attr.first == "__xlang3_capture_expressions__")
        return attr.second.tag == ValueTag::Bool && attr.second.as.b;
  }
  return false;
}

bool encode_expression(const Value& expression, std::string& bytes, std::string& error) {
  if (expression.tag != ValueTag::Object || !expression.as.obj || expression.as.obj->kind != ObjectKind::Expression) return false;
  serialize::BlockStream stream;
  stream << uint32_t{1};
  // Keep node strings tagged so the standard decoder validates their lengths.
  auto write = [&](auto&& self, const ExpressionNode& n) -> bool {
    if (!stream.MarshalToBytes(Value::string(n.op), {}, error) || !stream.MarshalToBytes(n.value, {}, error)) return false;
    stream << static_cast<uint32_t>(n.children.size());
    for (const auto& c : n.children) if (!self(self, c)) return false;
    return true;
  };
  if (!write(write, reinterpret_cast<ExpressionObject*>(expression.as.obj)->root)) return false;
  bytes.resize(static_cast<size_t>(stream.Size()));
  return stream.FullCopyTo(bytes.data(), stream.Size());
}

bool decode_expression(const std::string& bytes, Value& expression, std::string& error) {
  serialize::BlockStream stream(const_cast<char*>(bytes.data()), bytes.size(), false);
  uint32_t version = 0;
  if (!stream.CopyTo(reinterpret_cast<char*>(&version), sizeof(version)) || version != 1) { error = "invalid expression version"; return false; }
  ExpressionNode root;
  unsigned count = 0;
  if (!read_node(stream, root, 0, count, error)) { if (error.empty()) error = "invalid expression encoding"; return false; }
  char extra;
  if (stream.CopyTo(&extra, 1)) { error = "trailing expression bytes"; return false; }
  expression = own(std::move(root));
  return true;
}

bool evaluate_expression(const Value& expression, const Value& bindings, Value& result, Value& reservations, std::string& error) {
  auto is_expression = [](const Value& v) { return v.tag == ValueTag::Object && v.as.obj && v.as.obj->kind == ObjectKind::Expression; };
  const auto* list = value_as_list(expression);
  if ((!is_expression(expression) && !list) || !value_as_dict(bindings)) {
    error = "expected expression and bindings dictionary"; return false;
  }
  Value pending = Value::dict({});
  if (list) {
    result = Value::boolean(true);
    for (const auto& item : list->items) {
      if (!is_expression(item)) { error = "expected expression list"; return false; }
      if (!eval(reinterpret_cast<ExpressionObject*>(item.as.obj)->root, bindings, pending, result, error)) return false;
      if (!value_truthy(result)) break;
    }
  } else if (!eval(reinterpret_cast<ExpressionObject*>(expression.as.obj)->root, bindings, pending, result, error)) return false;
  reservations = value_truthy(result) ? std::move(pending) : Value::dict({});
  return true;
}

bool inspect_expression(const Value& expression, Value& result, std::string& error) {
  if (expression.tag != ValueTag::Object || !expression.as.obj || expression.as.obj->kind != ObjectKind::Expression) {
    error = "expected expression"; return false;
  }
  const auto& node = reinterpret_cast<ExpressionObject*>(expression.as.obj)->root;
  std::vector<Value> children;
  for (const auto& child : node.children) children.push_back(own(child));
  result = Value::dict({{Value::string("op"), Value::string(node.op)},
                        {Value::string("value"), node.value},
                        {Value::string("children"), Value::list(std::move(children))}});
  return true;
}
}
