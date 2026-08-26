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
#include "xlang3/dap_session.h"

#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value.h"
#include "xlang3/interpreter.h"

#include "json.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace xlang3::dap {

namespace {

using Json = nlohmann::json;

static constexpr int64_t kVariableRefBase = 1000000;

std::string reason_name(RuntimePauseReason reason) {
  switch (reason) {
    case RuntimePauseReason::Breakpoint:
      return "breakpoint";
    case RuntimePauseReason::Step:
    case RuntimePauseReason::StepOver:
    case RuntimePauseReason::StepOut:
      return "step";
    case RuntimePauseReason::PauseRequest:
      return "pause";
    default:
      return "entry";
  }
}

int64_t request_seq(const Json& request) {
  return request.value("seq", 0);
}

std::string request_command(const Json& request) {
  return request.value("command", "");
}

std::string value_dap_type_name(const Value& value) {
  switch (value.tag) {
    case ValueTag::None:
      return "NoneType";
    case ValueTag::Bool:
      return "bool";
    case ValueTag::Int64:
      return "int";
    case ValueTag::Double:
      return "float";
    case ValueTag::Object:
      if (value.as.obj == nullptr) {
        return "object";
      }
      switch (value.as.obj->kind) {
        case ObjectKind::String:
          return "str";
        case ObjectKind::Bytes:
          return "bytes";
        case ObjectKind::ByteArray:
          return "bytearray";
        case ObjectKind::MemoryView:
          return "memoryview";
        case ObjectKind::Slice:
          return "slice";
        case ObjectKind::Tuple:
          return "tuple";
        case ObjectKind::List:
          return "list";
        case ObjectKind::Dict:
          return "dict";
        case ObjectKind::Set:
          return "set";
        case ObjectKind::Range:
          return "range";
        case ObjectKind::Module:
          return "module";
        case ObjectKind::Function:
          return "function";
        case ObjectKind::NativeFunction:
          return "native_function";
        case ObjectKind::Class:
          return "type";
        case ObjectKind::Instance:
          return "object";
        case ObjectKind::BoundMethod:
          return "method";
        case ObjectKind::Code:
          return "code";
        case ObjectKind::Frame:
          return "frame";
        case ObjectKind::Traceback:
          return "traceback";
        case ObjectKind::File:
          return "file";
        default:
          return "object";
      }
    default:
      return "invalid";
  }
}

bool read_file_text(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return true;
}

bool value_has_dap_children(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return false;
  }

  switch (value.as.obj->kind) {
    case ObjectKind::List:
    case ObjectKind::Tuple:
    case ObjectKind::Dict:
    case ObjectKind::Set:
    case ObjectKind::Module:
    case ObjectKind::Class:
    case ObjectKind::Instance:
    case ObjectKind::Function:
    case ObjectKind::BoundMethod:
    case ObjectKind::Frame:
      return true;
    default:
      return false;
  }
}

Json value_to_dap_variable(const std::string& name, const Value& value, int64_t variables_reference) {
  Json item;
  item["name"] = name;
  item["value"] = value_to_string(value);
  item["type"] = value_dap_type_name(value);
  item["variablesReference"] = variables_reference;
  return item;
}

int64_t value_int_or_zero(const Value& value) {
  if (value.tag == ValueTag::Int64) {
    return value.as.i64;
  }
  return 0;
}

bool frame_attr(const Value& frame, const char* name, Value& out) {
  std::string ignored;
  return object_get_attr(frame, name, out, ignored);
}

bool dict_lookup_string_key(const Value& mapping, const std::string& name, Value& out) {
  auto* dict = value_as_dict(mapping);
  if (dict == nullptr) {
    return false;
  }
  for (const auto& entry : dict->entries) {
    auto* key = value_as_string(entry.first);
    if (key != nullptr && string_object_view(*key) == name) {
      value_assign_fast(out, entry.second);
      return true;
    }
  }
  return false;
}

Value frame_by_id(Value frame, int64_t frame_id) {
  if (frame_id <= 0) {
    return Value::invalid();
  }

  for (int64_t current = 1; current < frame_id; ++current) {
    Value back;
    if (!frame_attr(frame, "f_back", back) || back.tag == ValueTag::None || value_as_frame(back) == nullptr) {
      return Value::invalid();
    }
    frame = std::move(back);
  }
  return frame;
}

std::string frame_code_string_attr(const Value& frame, const char* attr, const std::string& fallback) {
  Value code;
  if (!frame_attr(frame, "f_code", code)) {
    return fallback;
  }
  Value value;
  if (!frame_attr(code, attr, value)) {
    return fallback;
  }
  return value_to_string(value);
}

int64_t frame_line(const Value& frame, uint32_t fallback) {
  Value line;
  if (!frame_attr(frame, "f_lineno", line)) {
    return fallback;
  }
  const int64_t value = value_int_or_zero(line);
  return value == 0 ? fallback : value;
}

bool lookup_name_in_mapping(const Value& mapping, const std::string& name, Value& out) {
  if (dict_lookup_string_key(mapping, name, out)) {
    return true;
  }
  std::string ignored;
  return module_get_attr(mapping, name, out, ignored);
}

bool parse_i64(const std::string& text, int64_t& out) {
  const char* first = text.data();
  const char* last = text.data() + text.size();
  auto result = std::from_chars(first, last, out);
  return result.ec == std::errc() && result.ptr == last;
}

bool parse_f64(const std::string& text, double& out) {
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  return end != nullptr && *end == '\0';
}

class DebugExpressionEvaluator {
public:
  DebugExpressionEvaluator(Runtime& runtime, const Value& frame)
      : runtime_(runtime) {
    frame_attr(frame, "f_locals", locals_);
    frame_attr(frame, "f_globals", globals_);
  }

  bool eval(const ast::Expr& expr, Value& out, std::string& error) {
    if (auto* literal = dynamic_cast<const ast::LiteralExpr*>(&expr)) {
      return eval_literal(*literal, out, error);
    }
    if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
      return eval_name(name->name, out, error);
    }
    if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
      Value object;
      if (!eval(*attr->object, object, error)) {
        return false;
      }
      return object_get_attr(object, attr->name, out, error);
    }
    if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
      Value object;
      Value index;
      if (!eval(*subscript->object, object, error) || !eval(*subscript->index, index, error)) {
        return false;
      }
      return sequence_get_item(object, index, out, error);
    }
    if (auto* slice = dynamic_cast<const ast::SliceExpr*>(&expr)) {
      Value start = Value::none();
      Value stop = Value::none();
      Value step = Value::none();
      if (slice->start != nullptr && !eval(*slice->start, start, error)) {
        return false;
      }
      if (slice->stop != nullptr && !eval(*slice->stop, stop, error)) {
        return false;
      }
      if (slice->step != nullptr && !eval(*slice->step, step, error)) {
        return false;
      }
      out = Value::slice(std::move(start), std::move(stop), std::move(step));
      return true;
    }
    if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      return eval_unary(*unary, out, error);
    }
    if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
      return eval_binary(*binary, out, error);
    }
    if (auto* compare = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
      return eval_compare_chain(*compare, out, error);
    }
    if (auto* conditional = dynamic_cast<const ast::ConditionalExpr*>(&expr)) {
      Value condition;
      if (!eval(*conditional->condition, condition, error)) {
        return false;
      }
      return eval(value_truthy(condition) ? *conditional->then_expr : *conditional->else_expr, out, error);
    }
    if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
      return eval_call(*call, out, error);
    }
    if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
      return eval_sequence(tuple->items, true, out, error);
    }
    if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
      return eval_sequence(list->items, false, out, error);
    }
    if (auto* dict = dynamic_cast<const ast::DictExpr*>(&expr)) {
      std::vector<std::pair<Value, Value>> entries;
      entries.reserve(dict->entries.size());
      for (const auto& entry : dict->entries) {
        Value key;
        Value value;
        if (!eval(*entry.first, key, error) || !eval(*entry.second, value, error)) {
          return false;
        }
        entries.push_back({std::move(key), std::move(value)});
      }
      out = Value::dict(std::move(entries));
      return true;
    }
    if (auto* set = dynamic_cast<const ast::SetExpr*>(&expr)) {
      std::vector<Value> items;
      items.reserve(set->items.size());
      for (const auto& item_expr : set->items) {
        Value item;
        if (!eval(*item_expr, item, error)) {
          return false;
        }
        items.push_back(std::move(item));
      }
      out = Value::set(std::move(items));
      return true;
    }
    error = "unsupported debug expression";
    return false;
  }

private:
  bool eval_literal(const ast::LiteralExpr& literal, Value& out, std::string& error) {
    switch (literal.kind) {
      case ast::LiteralExpr::Kind::None:
        out = Value::none();
        return true;
      case ast::LiteralExpr::Kind::Bool:
        out = Value::boolean(literal.bool_value);
        return true;
      case ast::LiteralExpr::Kind::Int: {
        int64_t value = 0;
        if (!parse_i64(literal.text, value)) {
          error = "invalid integer literal";
          return false;
        }
        out = Value::int64(value);
        return true;
      }
      case ast::LiteralExpr::Kind::Double: {
        double value = 0.0;
        if (!parse_f64(literal.text, value)) {
          error = "invalid float literal";
          return false;
        }
        out = Value::number(value);
        return true;
      }
      case ast::LiteralExpr::Kind::String:
        out = Value::string(literal.text);
        return true;
      case ast::LiteralExpr::Kind::Bytes:
        out = Value::bytes(literal.text);
        return true;
      case ast::LiteralExpr::Kind::Ellipsis:
        error = "ellipsis is not implemented yet";
        return false;
    }
    error = "unsupported literal";
    return false;
  }

  bool eval_name(const std::string& name, Value& out, std::string& error) {
    if (lookup_name_in_mapping(locals_, name, out) || lookup_name_in_mapping(globals_, name, out)) {
      return true;
    }
    if (const auto* builtin = runtime_.find_builtin(name)) {
      value_assign_fast(out, *builtin);
      return true;
    }
    error = "name not found: " + name;
    return false;
  }

  bool eval_unary(const ast::UnaryExpr& unary, Value& out, std::string& error) {
    Value value;
    if (!eval(*unary.expr, value, error)) {
      return false;
    }
    if (unary.op == "not") {
      out = Value::boolean(!value_truthy(value));
      return true;
    }
    if (unary.op == "~") {
      return value_invert(value, out, error);
    }
    if (unary.op == "+") {
      value_assign_fast(out, value);
      return true;
    }
    if (unary.op == "-") {
      if (value.tag == ValueTag::Int64) {
        out = Value::int64(-value.as.i64);
        return true;
      }
      if (value.tag == ValueTag::Double) {
        out = Value::number(-value.as.f64);
        return true;
      }
    }
    error = "unsupported unary operator: " + unary.op;
    return false;
  }

  bool eval_binary(const ast::BinaryExpr& binary, Value& out, std::string& error) {
    if (binary.op == "and") {
      Value lhs;
      if (!eval(*binary.lhs, lhs, error)) {
        return false;
      }
      if (!value_truthy(lhs)) {
        value_assign_fast(out, lhs);
        return true;
      }
      return eval(*binary.rhs, out, error);
    }
    if (binary.op == "or") {
      Value lhs;
      if (!eval(*binary.lhs, lhs, error)) {
        return false;
      }
      if (value_truthy(lhs)) {
        value_assign_fast(out, lhs);
        return true;
      }
      return eval(*binary.rhs, out, error);
    }

    Value lhs;
    Value rhs;
    if (!eval(*binary.lhs, lhs, error) || !eval(*binary.rhs, rhs, error)) {
      return false;
    }
    if (binary.op == "+") return value_add(lhs, rhs, out, error);
    if (binary.op == "-") return value_sub(lhs, rhs, out, error);
    if (binary.op == "*") return value_mul(lhs, rhs, out, error);
    if (binary.op == "/") return value_div(lhs, rhs, out, error);
    if (binary.op == "//") return value_floor_div(lhs, rhs, out, error);
    if (binary.op == "%") return value_mod(lhs, rhs, out, error);
    if (binary.op == "**") return value_pow(lhs, rhs, out, error);
    if (binary.op == "&") return value_bit_and(lhs, rhs, out, error);
    if (binary.op == "|") return value_bit_or(lhs, rhs, out, error);
    if (binary.op == "^") return value_bit_xor(lhs, rhs, out, error);
    if (binary.op == "<<") return value_shift_left(lhs, rhs, out, error);
    if (binary.op == ">>") return value_shift_right(lhs, rhs, out, error);
    if (binary.op == "in" || binary.op == "not in") {
      bool contains = false;
      if (!value_contains(rhs, lhs, contains, error)) {
        return false;
      }
      out = Value::boolean(binary.op == "in" ? contains : !contains);
      return true;
    }
    return value_compare(binary.op, lhs, rhs, out, error);
  }

  bool eval_compare_chain(const ast::CompareChainExpr& compare, Value& out, std::string& error) {
    Value lhs;
    if (!eval(*compare.first, lhs, error)) {
      return false;
    }
    for (const auto& item : compare.comparisons) {
      Value rhs;
      Value comparison;
      if (!eval(*item.second, rhs, error)) {
        return false;
      }
      if (item.first == "is") {
        comparison = Value::boolean(value_is(lhs, rhs));
      } else if (item.first == "is not") {
        comparison = Value::boolean(!value_is(lhs, rhs));
      } else if (item.first == "in" || item.first == "not in") {
        bool contains = false;
        if (!value_contains(rhs, lhs, contains, error)) {
          return false;
        }
        comparison = Value::boolean(item.first == "in" ? contains : !contains);
      } else if (!value_compare(item.first, lhs, rhs, comparison, error)) {
        return false;
      }
      if (!value_truthy(comparison)) {
        out = Value::boolean(false);
        return true;
      }
      lhs = std::move(rhs);
    }
    out = Value::boolean(true);
    return true;
  }

  bool eval_call(const ast::CallExpr& call, Value& out, std::string& error) {
    Value callee;
    if (!eval(*call.callee, callee, error)) {
      return false;
    }
    std::vector<Value> args;
    if (!call.call_args.empty()) {
      for (const auto& arg : call.call_args) {
        if (!arg.name.empty() || arg.star || arg.kw_star) {
          error = "keyword and unpacked debug calls are not implemented yet";
          return false;
        }
        Value value;
        if (!eval(*arg.value, value, error)) {
          return false;
        }
        args.push_back(std::move(value));
      }
    } else {
      for (const auto& arg_expr : call.args) {
        Value value;
        if (!eval(*arg_expr, value, error)) {
          return false;
        }
        args.push_back(std::move(value));
      }
    }
    return call_value(callee, args, out, error);
  }

  bool call_value(const Value& callee, std::vector<Value>& args, Value& out, std::string& error) {
    if (auto* bound = value_as_bound_method(callee)) {
      std::vector<Value> bound_args;
      bound_args.reserve(args.size() + 1);
      bound_args.push_back(bound->self);
      bound_args.insert(bound_args.end(), args.begin(), args.end());
      return call_value(bound->function, bound_args, out, error);
    }
    if (auto* native = value_as_native_function(callee)) {
      if (native->callback == nullptr) {
        error = "native function is not callable through debug evaluate";
        return false;
      }
      return native->callback(runtime_, args.data(), static_cast<uint32_t>(args.size()), out, error, native->user_data);
    }
    if (auto* function = value_as_function(callee)) {
      CallArgsView view;
      view.leading = args.data();
      view.leading_count = static_cast<uint32_t>(args.size());
      Interpreter interpreter(runtime_);
      RuntimeResult result = interpreter.run_function_value(function, view);
      if (!result.errors.empty()) {
        error = result.errors.front();
        return false;
      }
      value_assign_fast(out, result.value);
      return true;
    }
    error = "object is not callable";
    return false;
  }

  bool eval_sequence(const std::vector<ast::ExprPtr>& exprs, bool tuple, Value& out, std::string& error) {
    std::vector<Value> items;
    items.reserve(exprs.size());
    for (const auto& item_expr : exprs) {
      Value item;
      if (!eval(*item_expr, item, error)) {
        return false;
      }
      items.push_back(std::move(item));
    }
    out = tuple ? Value::tuple(std::move(items)) : Value::list(std::move(items));
    return true;
  }

  Runtime& runtime_;
  Value locals_ = Value::invalid();
  Value globals_ = Value::invalid();
};

Json variables_from_frame_attr(const Value& frame, const char* attr, const DapSession& session) {
  Json variables = Json::array();
  Value mapping;
  if (!frame_attr(frame, attr, mapping)) {
    return variables;
  }
  if (auto* dict = value_as_dict(mapping)) {
    for (const auto& entry : dict->entries) {
      variables.push_back(value_to_dap_variable(
          value_to_string(entry.first),
          entry.second,
          session.register_variable_ref(entry.second)));
    }
  } else if (auto* module = value_as_module(mapping)) {
    std::vector<std::pair<std::string, uint32_t>> names;
    names.reserve(module->name_to_slot.size());
    for (const auto& entry : module->name_to_slot) {
      if (!entry.first.empty() && entry.first[0] != '#' && entry.second < module->slots.size() &&
          module->slots[entry.second].tag != ValueTag::Invalid) {
        names.push_back(entry);
      }
    }
    std::sort(names.begin(), names.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.second < rhs.second;
    });
    for (const auto& entry : names) {
      const Value& slot = module->slots[entry.second];
      variables.push_back(value_to_dap_variable(entry.first, slot, session.register_variable_ref(slot)));
    }
    variables.push_back(value_to_dap_variable("__name__", Value::string(module->name), 0));
  }
  return variables;
}

int64_t make_scope_ref(int64_t frame_id, int64_t scope_kind) {
  return frame_id * 10 + scope_kind;
}

int64_t scope_frame_id(int64_t variables_reference) {
  return variables_reference / 10;
}

int64_t scope_kind(int64_t variables_reference) {
  return variables_reference % 10;
}

} // namespace

bool try_read_framed_message(std::string& buffer, FramedMessage& out, std::string& error) {
  const size_t header_end = buffer.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return false;
  }

  size_t content_length = 0;
  bool found_length = false;
  size_t line_start = 0;
  while (line_start < header_end) {
    const size_t line_end = buffer.find("\r\n", line_start);
    const size_t actual_end = line_end == std::string::npos || line_end > header_end ? header_end : line_end;
    std::string_view line(buffer.data() + line_start, actual_end - line_start);
    static constexpr std::string_view prefix = "Content-Length:";
    if (line.substr(0, prefix.size()) == prefix) {
      line.remove_prefix(prefix.size());
      while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
        line.remove_prefix(1);
      }
      const char* begin = line.data();
      const char* end = line.data() + line.size();
      auto parsed = std::from_chars(begin, end, content_length);
      if (parsed.ec != std::errc()) {
        error = "invalid DAP Content-Length";
        return false;
      }
      found_length = true;
    }
    line_start = actual_end + 2;
  }

  if (!found_length) {
    error = "missing DAP Content-Length";
    return false;
  }

  const size_t payload_start = header_end + 4;
  if (buffer.size() < payload_start + content_length) {
    return false;
  }
  out.payload = buffer.substr(payload_start, content_length);
  buffer.erase(0, payload_start + content_length);
  return true;
}

std::string make_framed_message(const std::string& payload) {
  return "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
}

DapSession::DapSession(std::ostream& program_output) : debug_(program_output) {}

std::vector<std::string> DapSession::handle_framed_input(std::string& input_buffer, std::string& error) {
  std::vector<std::string> responses;
  FramedMessage message;
  while (try_read_framed_message(input_buffer, message, error)) {
    for (const auto& payload : handle_payload(message.payload)) {
      responses.push_back(make_framed_message(payload));
    }
  }
  return responses;
}

std::vector<std::string> DapSession::handle_payload(const std::string& payload) {
  Json request;
  try {
    request = Json::parse(payload);
  } catch (const std::exception& ex) {
    return {make_response(0, "", false, std::string("invalid DAP JSON: ") + ex.what())};
  }

  const int64_t seq = request_seq(request);
  const std::string command = request_command(request);
  const Json args = request.value("arguments", Json::object());

  if (command == "initialize") {
    Json body = {
        {"supportsConfigurationDoneRequest", true},
        {"supportsTerminateRequest", true},
        {"supportsTerminateDebuggee", true},
        {"supportsStepBack", false},
        {"supportsSetVariable", false},
        {"supportsEvaluateForHovers", true},
        {"supportsExceptionOptions", false},
        {"supportsExceptionInfoRequest", false},
        {"supportsDelayedStackTraceLoading", false},
    };
    return {make_response_body(seq, command, true, "", body.dump()), initialized_event()};
  }
  if (command == "launch") {
    std::string program = args.value("program", "");
    std::string source = args.value("source", "");
    if (source.empty() && !program.empty() && !read_file_text(program, source)) {
      return {make_response(seq, command, false, "cannot open launch program")};
    }
    if (program.empty()) {
      program = "<dap>";
    }
    std::string error;
    if (!debug_.load_source(program, source, error)) {
      return {make_response(seq, command, false, error)};
    }
    if (args.value("stopAtEntry", false) || args.value("stopOnEntry", false)) {
      debug_.request_pause();
    }
    if (args.contains("args") && args["args"].is_array()) {
      std::vector<std::string> argv;
      argv.push_back(program);
      for (const auto& arg : args["args"]) {
        argv.push_back(arg.get<std::string>());
      }
      if (!debug_.set_argv(argv, error)) {
        return {make_response(seq, command, false, error)};
      }
    }
    return {make_response(seq, command, true, "")};
  }
  if (command == "setBreakpoints") {
    const Json source_obj = args.value("source", Json::object());
    const std::string path = source_obj.value("path", "");
    debug_.clear_breakpoints();
    Json breakpoints = Json::array();
    for (const auto& bp : args.value("breakpoints", Json::array())) {
      const uint32_t line = bp.value("line", 0);
      if (line != 0) {
        debug_.add_breakpoint(path, line);
      }
      breakpoints.push_back({{"verified", line != 0}, {"line", line}});
    }
    Json body = {{"breakpoints", breakpoints}};
    return {make_response_body(seq, command, true, "", body.dump())};
  }
  if (command == "setExceptionBreakpoints") {
    Json body = {{"breakpoints", Json::array()}};
    return {make_response_body(seq, command, true, "", body.dump())};
  }
  if (command == "configurationDone") {
    std::string error;
    if (!debug_.launch(error)) {
      return {make_response(seq, command, false, error)};
    }
    const std::string response = make_response(seq, command, true, "");
    if (debug_.status().paused) {
      return {response, stopped_event()};
    }
    if (debug_.status().finished) {
      return {response, terminated_event()};
    }
    return {response};
  }
  if (command == "continue") {
    std::string error;
    if (!debug_.continue_execution(error)) {
      return {make_response(seq, command, false, error)};
    }
    Json body = {{"allThreadsContinued", true}};
    const std::string response = make_response_body(seq, command, true, "", body.dump());
    if (debug_.status().paused) {
      return {response, stopped_event()};
    }
    if (debug_.status().finished) {
      return {response, terminated_event()};
    }
    return {response};
  }
  if (command == "next" || command == "stepIn" || command == "stepOut") {
    std::string error;
    bool ok = false;
    if (command == "next") {
      ok = debug_.step_over(error);
    } else if (command == "stepIn") {
      ok = debug_.step_into(error);
    } else {
      ok = debug_.step_out(error);
    }
    if (!ok) {
      return {make_response(seq, command, false, error)};
    }
    const std::string response = make_response(seq, command, true, "");
    if (debug_.status().paused) {
      return {response, stopped_event()};
    }
    if (debug_.status().finished) {
      return {response, terminated_event()};
    }
    return {response};
  }
  if (command == "stackTrace") {
    return {make_response_body(seq, command, true, "", stack_trace_body())};
  }
  if (command == "threads") {
    return {make_response_body(seq, command, true, "", threads_body())};
  }
  if (command == "scopes") {
    const int64_t frame_id = args.value("frameId", 1);
    return {make_response_body(seq, command, true, "", scopes_body(frame_id))};
  }
  if (command == "variables") {
    const int64_t ref = args.value("variablesReference", 0);
    return {make_response_body(seq, command, true, "", variables_body(ref))};
  }
  if (command == "evaluate") {
    bool ok = false;
    std::string error;
    const std::string body = evaluate_body(
        args.value("expression", ""),
        args.value("frameId", 1),
        ok,
        error);
    return {make_response_body(seq, command, ok, error, body)};
  }
  if (command == "disconnect" || command == "terminate") {
    return {make_response(seq, command, true, "")};
  }
  return {make_response(seq, command, false, "unsupported DAP command: " + command)};
}

std::string DapSession::make_response(int64_t request_seq, const std::string& command, bool success, const std::string& message) {
  return make_response_body(request_seq, command, success, message, "{}");
}

std::string DapSession::make_response_body(
    int64_t request_seq,
    const std::string& command,
    bool success,
    const std::string& message,
    const std::string& body_json) {
  Json response = {
      {"seq", next_seq_++},
      {"type", "response"},
      {"request_seq", request_seq},
      {"success", success},
      {"command", command},
  };
  if (!message.empty()) {
    response["message"] = message;
  }
  response["body"] = Json::parse(body_json);
  return response.dump();
}

std::string DapSession::make_event(const std::string& event, const std::string& body_json) {
  Json message = {
      {"seq", next_seq_++},
      {"type", "event"},
      {"event", event},
      {"body", Json::parse(body_json)},
  };
  return message.dump();
}

std::string DapSession::make_output_event(const std::string& output) {
  Json body = {
      {"category", "stdout"},
      {"output", output},
  };
  return make_event("output", body.dump());
}

std::string DapSession::initialized_event() {
  return make_event("initialized", "{}");
}

std::string DapSession::stopped_event() {
  Json body = {
      {"reason", reason_name(debug_.status().reason)},
      {"threadId", 1},
      {"allThreadsStopped", true},
  };
  return make_event("stopped", body.dump());
}

std::string DapSession::terminated_event() {
  return make_event("terminated", "{}");
}

std::string DapSession::status_body() const {
  Json body = {
      {"loaded", debug_.status().loaded},
      {"running", debug_.status().running},
      {"paused", debug_.status().paused},
      {"finished", debug_.status().finished},
      {"file", debug_.status().file},
      {"line", debug_.status().line},
  };
  return body.dump();
}

std::string DapSession::threads_body() const {
  Json threads = Json::array();
  threads.push_back({
      {"id", 1},
      {"name", "MainThread"},
  });
  return Json({{"threads", threads}}).dump();
}

std::string DapSession::stack_trace_body() const {
  Json frames = Json::array();
  if (debug_.status().paused) {
    Value current = debug_.status().frame;
    int64_t frame_id = 1;
    while (value_as_frame(current) != nullptr && frame_id < 128) {
      const std::string file = frame_code_string_attr(current, "co_filename", debug_.status().file);
      Json frame = {
          {"id", frame_id},
          {"name", frame_code_string_attr(current, "co_name", "<module>")},
          {"line", frame_line(current, frame_id == 1 ? debug_.status().line : 0)},
          {"column", 1},
          {"source", {{"path", file}, {"name", file}}},
      };
      frames.push_back(std::move(frame));

      Value back;
      if (!frame_attr(current, "f_back", back) || back.tag == ValueTag::None) {
        break;
      }
      current = std::move(back);
      ++frame_id;
    }
  }
  Json body = {
      {"stackFrames", frames},
      {"totalFrames", frames.size()},
  };
  return body.dump();
}

std::string DapSession::scopes_body(int64_t frame_id) const {
  Json scopes = Json::array();
  if (debug_.status().paused && value_as_frame(frame_by_id(debug_.status().frame, frame_id)) != nullptr) {
    scopes.push_back({
        {"name", "Locals"},
        {"variablesReference", make_scope_ref(frame_id, 1)},
        {"expensive", false},
    });
    scopes.push_back({
        {"name", "Globals"},
        {"variablesReference", make_scope_ref(frame_id, 2)},
        {"expensive", false},
    });
  }
  return Json({{"scopes", scopes}}).dump();
}

std::string DapSession::variables_body(int64_t variables_reference) const {
  if (is_variable_ref(variables_reference)) {
    return variable_ref_body(variables_reference);
  }

  Json variables = Json::array();
  if (debug_.status().paused) {
    const Value frame = frame_by_id(debug_.status().frame, scope_frame_id(variables_reference));
    if (value_as_frame(frame) != nullptr) {
      if (scope_kind(variables_reference) == 1) {
        variables = variables_from_frame_attr(frame, "f_locals", *this);
        if (variables.empty() && frame_code_string_attr(frame, "co_name", "") == "<module>") {
          variables = variables_from_frame_attr(frame, "f_globals", *this);
        }
      } else if (scope_kind(variables_reference) == 2) {
        variables = variables_from_frame_attr(frame, "f_globals", *this);
      }
    }
  }
  return Json({{"variables", variables}}).dump();
}

std::string DapSession::evaluate_body(const std::string& expression, int64_t frame_id, bool& ok, std::string& error) {
  ok = false;
  error.clear();
  if (!debug_.status().paused) {
    error = "debuggee is not paused";
    return "{}";
  }

  auto parsed = parse_expression_source(expression);
  if (!parsed.errors.empty() || parsed.expression == nullptr) {
    error = parsed.errors.empty() ? "invalid expression" : parsed.errors.front();
    return "{}";
  }

  const Value frame = frame_by_id(debug_.status().frame, frame_id);
  if (value_as_frame(frame) == nullptr) {
    error = "invalid frame";
    return "{}";
  }

  Value current;
  DebugExpressionEvaluator evaluator(debug_.runtime(), frame);
  if (!evaluator.eval(*parsed.expression, current, error)) {
    return "{}";
  }

  ok = true;
  Json body = {
      {"result", value_to_string(current)},
      {"type", value_dap_type_name(current)},
      {"variablesReference", register_variable_ref(current)},
  };
  return body.dump();
}

int64_t DapSession::register_variable_ref(const Value& value) const {
  if (!value_has_dap_children(value)) {
    return 0;
  }
  variable_refs_.push_back(value);
  return kVariableRefBase + static_cast<int64_t>(variable_refs_.size());
}

bool DapSession::is_variable_ref(int64_t variables_reference) const {
  const int64_t index = variables_reference - kVariableRefBase;
  return index > 0 && static_cast<size_t>(index) <= variable_refs_.size();
}

std::string DapSession::variable_ref_body(int64_t variables_reference) const {
  Json variables = Json::array();
  const int64_t index = variables_reference - kVariableRefBase;
  if (index <= 0 || static_cast<size_t>(index) > variable_refs_.size()) {
    return Json({{"variables", variables}}).dump();
  }

  const Value& value = variable_refs_[static_cast<size_t>(index - 1)];
  if (auto* instance = value_as_instance(value)) {
    if (auto* klass = value_as_class(instance->klass)) {
      for (uint32_t i = 0; i < instance->slot_count && i < klass->instance_slot_names.size(); ++i) {
        const Value& slot = instance_slot_at(instance, i);
        variables.push_back(value_to_dap_variable(klass->instance_slot_names[i], slot, register_variable_ref(slot)));
      }
    }
    for (const auto& attr : instance->attrs) {
      variables.push_back(value_to_dap_variable(attr.first, attr.second, register_variable_ref(attr.second)));
    }
    variables.push_back(value_to_dap_variable("__class__", instance->klass, register_variable_ref(instance->klass)));
  } else if (auto* klass = value_as_class(value)) {
    variables.push_back(value_to_dap_variable("__name__", Value::string(klass->name), 0));
    for (const auto& attr : klass->attrs) {
      variables.push_back(value_to_dap_variable(attr.first, attr.second, register_variable_ref(attr.second)));
    }
  } else if (auto* list = value_as_list(value)) {
    for (size_t i = 0; i < list->items.size(); ++i) {
      const Value& item = list->items[i];
      variables.push_back(value_to_dap_variable("[" + std::to_string(i) + "]", item, register_variable_ref(item)));
    }
  } else if (auto* tuple = value_as_tuple(value)) {
    for (size_t i = 0; i < tuple->items.size(); ++i) {
      const Value& item = tuple->items[i];
      variables.push_back(value_to_dap_variable("[" + std::to_string(i) + "]", item, register_variable_ref(item)));
    }
  } else if (auto* dict = value_as_dict(value)) {
    for (const auto& entry : dict->entries) {
      variables.push_back(value_to_dap_variable(value_to_string(entry.first), entry.second, register_variable_ref(entry.second)));
    }
  } else if (auto* set = value_as_set(value)) {
    for (size_t i = 0; i < set->items.size(); ++i) {
      const Value& item = set->items[i];
      variables.push_back(value_to_dap_variable("[" + std::to_string(i) + "]", item, register_variable_ref(item)));
    }
  } else if (auto* module = value_as_module(value)) {
    for (const auto& entry : module->name_to_slot) {
      if (entry.second < module->slots.size()) {
        const Value& slot = module->slots[entry.second];
        variables.push_back(value_to_dap_variable(entry.first, slot, register_variable_ref(slot)));
      }
    }
  } else if (auto* function = value_as_function(value)) {
    variables.push_back(value_to_dap_variable("__name__", Value::string(value_to_string(value)), 0));
    Value defaults = Value::tuple(function->defaults);
    variables.push_back(value_to_dap_variable("__defaults__", defaults, register_variable_ref(defaults)));
  } else if (auto* method = value_as_bound_method(value)) {
    variables.push_back(value_to_dap_variable("__self__", method->self, register_variable_ref(method->self)));
    variables.push_back(value_to_dap_variable("__func__", method->function, register_variable_ref(method->function)));
  } else if (auto* frame = value_as_frame(value)) {
    variables.push_back(value_to_dap_variable("f_locals", frame->locals, register_variable_ref(frame->locals)));
    variables.push_back(value_to_dap_variable("f_globals", frame->globals_module, register_variable_ref(frame->globals_module)));
    variables.push_back(value_to_dap_variable("f_back", frame->back, register_variable_ref(frame->back)));
  }

  return Json({{"variables", variables}}).dump();
}

} // namespace xlang3::dap
