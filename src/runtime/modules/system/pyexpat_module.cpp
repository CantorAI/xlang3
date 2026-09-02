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

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kParserNativeType = "pyexpat.XMLParserType";
constexpr int64_t kXmlErrorSyntax = 2;
constexpr int64_t kXmlErrorNoElements = 3;
constexpr int64_t kXmlErrorInvalidToken = 4;
constexpr int64_t kXmlErrorUnclosedToken = 5;
constexpr int64_t kXmlErrorTagMismatch = 7;
constexpr int64_t kXmlParamEntityParsingNever = 0;
constexpr int64_t kXmlParamEntityParsingUnlessStandalone = 1;
constexpr int64_t kXmlParamEntityParsingAlways = 2;

struct PyExpatParserState {
  std::string encoding;
  std::string namespace_separator;
  std::string buffer;
  std::vector<std::string> element_stack;
  int64_t current_line = 1;
  int64_t current_column = 0;
  int64_t current_byte = 0;
  int64_t error_code = 0;
  int64_t error_line = 1;
  int64_t error_column = 0;
  int64_t error_byte = 0;
  bool finished = false;
  bool reparse_deferral = false;
  bool foreign_dtd = false;
  int64_t param_entity_parsing = kXmlParamEntityParsingNever;
  std::string base;
};

struct XmlAttr {
  std::string name;
  std::string value;
};

void parser_cleanup(void* data) {
  delete static_cast<PyExpatParserState*>(data);
}

PyExpatParserState* parser_state(const Value& self, std::string& error) {
  auto* state = static_cast<PyExpatParserState*>(instance_get_native_data(self, kParserNativeType));
  if (state == nullptr) {
    error = "invalid pyexpat parser object";
  }
  return state;
}

bool bytes_or_string_arg(const Value& value, std::string& out, std::string& error) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  error = "Parse() argument must be str or bytes-like";
  return false;
}

std::string decode_xml_entities(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '&') {
      out.push_back(text[i]);
      continue;
    }
    const size_t semi = text.find(';', i + 1);
    if (semi == std::string_view::npos) {
      out.push_back(text[i]);
      continue;
    }
    const auto entity = text.substr(i + 1, semi - i - 1);
    if (entity == "amp") {
      out.push_back('&');
    } else if (entity == "lt") {
      out.push_back('<');
    } else if (entity == "gt") {
      out.push_back('>');
    } else if (entity == "apos") {
      out.push_back('\'');
    } else if (entity == "quot") {
      out.push_back('"');
    } else if (!entity.empty() && entity[0] == '#') {
      char* end = nullptr;
      const int base = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X') ? 16 : 10;
      const char* begin = entity.data() + (base == 16 ? 2 : 1);
      const unsigned long codepoint = std::strtoul(begin, &end, base);
      if (end == entity.data() + entity.size() && codepoint <= 0x7f) {
        out.push_back(static_cast<char>(codepoint));
      } else {
        out.append(text.substr(i, semi - i + 1));
      }
    } else {
      out.append(text.substr(i, semi - i + 1));
    }
    i = semi;
  }
  return out;
}

void advance_position(PyExpatParserState& state, std::string_view text) {
  for (char ch : text) {
    ++state.current_byte;
    if (ch == '\n') {
      ++state.current_line;
      state.current_column = 0;
    } else {
      ++state.current_column;
    }
  }
}

void set_parse_error(PyExpatParserState& state, int64_t code) {
  state.error_code = code;
  state.error_line = state.current_line;
  state.error_column = state.current_column;
  state.error_byte = state.current_byte;
}

bool is_name_char(char ch) {
  const auto c = static_cast<unsigned char>(ch);
  return std::isalnum(c) || ch == '_' || ch == ':' || ch == '-' || ch == '.';
}

void skip_spaces(std::string_view text, size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
}

bool parse_name(std::string_view text, size_t& pos, std::string& out) {
  const size_t start = pos;
  while (pos < text.size() && is_name_char(text[pos])) {
    ++pos;
  }
  if (pos == start) {
    return false;
  }
  out.assign(text.substr(start, pos - start));
  return true;
}

bool parse_attr_value(std::string_view text, size_t& pos, std::string& out) {
  if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
    return false;
  }
  const char quote = text[pos++];
  const size_t start = pos;
  while (pos < text.size() && text[pos] != quote) {
    ++pos;
  }
  if (pos >= text.size()) {
    return false;
  }
  out = decode_xml_entities(text.substr(start, pos - start));
  ++pos;
  return true;
}

bool parse_start_tag(std::string_view inner, std::string& name, std::vector<XmlAttr>& attrs, bool& self_closing) {
  size_t pos = 0;
  skip_spaces(inner, pos);
  if (!parse_name(inner, pos, name)) {
    return false;
  }
  for (;;) {
    skip_spaces(inner, pos);
    if (pos >= inner.size()) {
      self_closing = false;
      return true;
    }
    if (inner[pos] == '/') {
      ++pos;
      skip_spaces(inner, pos);
      self_closing = pos == inner.size();
      return self_closing;
    }
    XmlAttr attr;
    if (!parse_name(inner, pos, attr.name)) {
      return false;
    }
    skip_spaces(inner, pos);
    if (pos >= inner.size() || inner[pos] != '=') {
      return false;
    }
    ++pos;
    skip_spaces(inner, pos);
    if (!parse_attr_value(inner, pos, attr.value)) {
      return false;
    }
    attrs.push_back(std::move(attr));
  }
}

bool instance_bool_attr(const Value& object, const char* name) {
  Value value;
  std::string ignored;
  return object_get_attr(object, name, value, ignored) && value_truthy(value);
}

Value attrs_value_for_parser(const Value& parser, const std::vector<XmlAttr>& attrs) {
  if (instance_bool_attr(parser, "ordered_attributes")) {
    std::vector<Value> items;
    items.reserve(attrs.size() * 2);
    for (const auto& attr : attrs) {
      items.push_back(Value::string(attr.name));
      items.push_back(Value::string(attr.value));
    }
    return Value::list(std::move(items));
  }
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(attrs.size());
  for (const auto& attr : attrs) {
    entries.push_back({Value::string(attr.name), Value::string(attr.value)});
  }
  return Value::dict(std::move(entries));
}

bool call_handler(Runtime& runtime, const Value& parser, const char* name, const std::vector<Value>& args, std::string& error) {
  Value handler;
  std::string attr_error;
  if (!object_get_attr(parser, name, handler, attr_error) || handler.tag == ValueTag::None || handler.tag == ValueTag::Invalid) {
    return true;
  }
  Value ignored;
  return runtime_call_callable(runtime, handler, args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()), ignored, error);
}

bool emit_text(Runtime& runtime, const Value& parser, PyExpatParserState& state, std::string_view text, std::string& error) {
  if (text.empty()) {
    return true;
  }
  const std::string decoded = decode_xml_entities(text);
  advance_position(state, text);
  if (decoded.empty()) {
    return true;
  }
  std::vector<Value> args;
  args.push_back(Value::string(decoded));
  return call_handler(runtime, parser, "CharacterDataHandler", args, error);
}

bool raise_expat_error(Runtime& runtime, PyExpatParserState& state, int64_t code, std::string message, std::string& error) {
  set_parse_error(state, code);
  Value exception_class;
  Value pyexpat;
  std::string ignored;
  if (runtime.import_module("pyexpat", pyexpat, ignored) && module_get_attr(pyexpat, "ExpatError", exception_class, ignored)) {
    Value exception = runtime.make_exception_from_class(exception_class, message);
    object_set_attr(exception, "code", Value::int64(code), ignored);
    object_set_attr(exception, "lineno", Value::int64(state.error_line), ignored);
    object_set_attr(exception, "offset", Value::int64(state.error_column), ignored);
    runtime.set_pending_exception(std::move(exception));
  } else {
    runtime.raise_class_error("SyntaxError", message);
  }
  error = std::move(message);
  return false;
}

bool parse_document(Runtime& runtime, const Value& parser, PyExpatParserState& state, bool final, std::string& error) {
  std::string_view text(state.buffer);
  size_t pos = 0;
  bool saw_element = false;
  state.current_line = 1;
  state.current_column = 0;
  state.current_byte = 0;
  state.element_stack.clear();

  while (pos < text.size()) {
    const size_t lt = text.find('<', pos);
    if (lt == std::string_view::npos) {
      if (final && !emit_text(runtime, parser, state, text.substr(pos), error)) {
        return false;
      }
      break;
    }
    if (!emit_text(runtime, parser, state, text.substr(pos, lt - pos), error)) {
      return false;
    }
    if (text.substr(lt, 4) == "<!--") {
      const size_t end = text.find("-->", lt + 4);
      if (end == std::string_view::npos) {
        if (!final) {
          break;
        }
        return raise_expat_error(runtime, state, kXmlErrorUnclosedToken, "unclosed XML comment", error);
      }
      advance_position(state, text.substr(lt, end + 3 - lt));
      pos = end + 3;
      continue;
    }
    if (text.substr(lt, 9) == "<![CDATA[") {
      const size_t end = text.find("]]>", lt + 9);
      if (end == std::string_view::npos) {
        if (!final) {
          break;
        }
        return raise_expat_error(runtime, state, kXmlErrorUnclosedToken, "unclosed CDATA section", error);
      }
      if (!emit_text(runtime, parser, state, text.substr(lt + 9, end - lt - 9), error)) {
        return false;
      }
      advance_position(state, text.substr(end, 3));
      pos = end + 3;
      continue;
    }
    const size_t gt = text.find('>', lt + 1);
    if (gt == std::string_view::npos) {
      if (!final) {
        break;
      }
      return raise_expat_error(runtime, state, kXmlErrorUnclosedToken, "unclosed XML token", error);
    }
    const auto token = text.substr(lt + 1, gt - lt - 1);
    if (!token.empty() && token[0] == '?') {
      advance_position(state, text.substr(lt, gt + 1 - lt));
      pos = gt + 1;
      continue;
    }
    if (!token.empty() && token[0] == '!') {
      advance_position(state, text.substr(lt, gt + 1 - lt));
      pos = gt + 1;
      continue;
    }
    if (!token.empty() && token[0] == '/') {
      size_t name_pos = 1;
      skip_spaces(token, name_pos);
      std::string name;
      if (!parse_name(token, name_pos, name) || state.element_stack.empty() || state.element_stack.back() != name) {
        return raise_expat_error(runtime, state, kXmlErrorTagMismatch, "mismatched XML tag", error);
      }
      state.element_stack.pop_back();
      std::vector<Value> args;
      args.push_back(Value::string(name));
      if (!call_handler(runtime, parser, "EndElementHandler", args, error)) {
        return false;
      }
      advance_position(state, text.substr(lt, gt + 1 - lt));
      pos = gt + 1;
      continue;
    }
    std::string name;
    std::vector<XmlAttr> attrs;
    bool self_closing = false;
    if (!parse_start_tag(token, name, attrs, self_closing)) {
      return raise_expat_error(runtime, state, kXmlErrorInvalidToken, "invalid XML start tag", error);
    }
    saw_element = true;
    std::vector<Value> args;
    args.push_back(Value::string(name));
    args.push_back(attrs_value_for_parser(parser, attrs));
    if (!call_handler(runtime, parser, "StartElementHandler", args, error)) {
      return false;
    }
    if (self_closing) {
      std::vector<Value> end_args;
      end_args.push_back(Value::string(name));
      if (!call_handler(runtime, parser, "EndElementHandler", end_args, error)) {
        return false;
      }
    } else {
      state.element_stack.push_back(std::move(name));
    }
    advance_position(state, text.substr(lt, gt + 1 - lt));
    pos = gt + 1;
  }

  if (final) {
    if (!saw_element) {
      return raise_expat_error(runtime, state, kXmlErrorNoElements, "no element found", error);
    }
    if (!state.element_stack.empty()) {
      return raise_expat_error(runtime, state, kXmlErrorUnclosedToken, "unclosed XML token", error);
    }
    state.finished = true;
  }
  return true;
}

bool parser_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "XMLParserType.__init__ expected self";
    return false;
  }
  if (parser_state(args[0], error) == nullptr) {
    auto* state = new PyExpatParserState();
    if (!instance_set_native_data(args[0], kParserNativeType, state, parser_cleanup, error)) {
      delete state;
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool parser_parse(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "Parse() expected data and optional isfinal";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string chunk;
  if (!bytes_or_string_arg(args[1], chunk, error)) {
    return false;
  }
  const bool final = argc >= 3 && value_truthy(args[2]);
  state->buffer.append(chunk);
  if (!parse_document(runtime, args[0], *state, final, error)) {
    return false;
  }
  out = Value::int64(1);
  return true;
}

bool parser_parse_file(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "ParseFile() expected a file object";
    return false;
  }
  Value read_method;
  if (!object_get_attr(args[1], "read", read_method, error)) {
    return false;
  }
  Value data;
  if (!runtime_call_callable(runtime, read_method, nullptr, 0, data, error)) {
    return false;
  }
  Value parse_args[3] = {args[0], data, Value::boolean(true)};
  return parser_parse(runtime, parse_args, 3, out, error, user_data);
}

bool parser_set_base(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "SetBase() expected base";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (auto* text = value_as_string(args[1])) {
    state->base = string_object_to_string(*text);
  } else if (args[1].tag == ValueTag::None) {
    state->base.clear();
  } else {
    error = "SetBase() argument must be str or None";
    return false;
  }
  out = Value::int64(1);
  return true;
}

bool parser_get_base(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "GetBase() expected no arguments";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = state->base.empty() ? Value::none() : Value::string(state->base);
  return true;
}

bool parser_set_param_entity_parsing(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "SetParamEntityParsing() expected integer flag";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->param_entity_parsing = args[1].as.i64;
  out = Value::int64(1);
  return true;
}

bool parser_external_entity_parser_create(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* parser_class_ptr) {
  if (argc < 1 || argc > 3) {
    error = "ExternalEntityParserCreate() expected optional context and encoding";
    return false;
  }
  Value parser_class;
  value_assign_fast(parser_class, *static_cast<Value*>(parser_class_ptr));
  out = Value::instance(parser_class);
  auto* state = new PyExpatParserState();
  std::string ignored;
  if (argc >= 3) {
    if (auto* encoding = value_as_string(args[2])) {
      state->encoding = string_object_to_string(*encoding);
    }
  }
  if (!instance_set_native_data(out, kParserNativeType, state, parser_cleanup, error)) {
    delete state;
    return false;
  }
  object_set_attr(out, "ordered_attributes", Value::boolean(false), ignored);
  object_set_attr(out, "specified_attributes", Value::boolean(false), ignored);
  object_set_attr(out, "namespace_prefixes", Value::boolean(false), ignored);
  (void)runtime;
  return true;
}

bool parser_use_foreign_dtd(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "UseForeignDTD() expected optional flag";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->foreign_dtd = argc == 1 || value_truthy(args[1]);
  out = Value::none();
  return true;
}

bool parser_get_reparse_deferral_enabled(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "GetReparseDeferralEnabled() expected no arguments";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::boolean(state->reparse_deferral);
  return true;
}

bool parser_set_reparse_deferral_enabled(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "SetReparseDeferralEnabled() expected flag";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->reparse_deferral = value_truthy(args[1]);
  value_set_none(out);
  return true;
}

bool parser_noop_setter(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool parser_get_input_context(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "GetInputContext() expected no arguments";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::bytes(state->buffer);
  return true;
}

bool parser_get_attr(const Value& self, const std::string& name, Value& out, std::string& error) {
  auto* state = parser_state(self, error);
  if (state == nullptr) {
    return false;
  }
  if (name == "CurrentLineNumber") {
    out = Value::int64(state->current_line);
  } else if (name == "CurrentColumnNumber") {
    out = Value::int64(state->current_column);
  } else if (name == "CurrentByteIndex") {
    out = Value::int64(state->current_byte);
  } else if (name == "ErrorLineNumber") {
    out = Value::int64(state->error_line);
  } else if (name == "ErrorColumnNumber") {
    out = Value::int64(state->error_column);
  } else if (name == "ErrorByteIndex") {
    out = Value::int64(state->error_byte);
  } else if (name == "ErrorCode") {
    out = Value::int64(state->error_code);
  } else if (name == "buffer_used") {
    out = Value::int64(0);
  } else {
    return false;
  }
  return true;
}

bool pyexpat_error_string(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "ErrorString() expected integer error code";
    return false;
  }
  switch (args[0].as.i64) {
    case kXmlErrorNoElements:
      out = Value::string("no element found");
      return true;
    case kXmlErrorInvalidToken:
      out = Value::string("not well-formed (invalid token)");
      return true;
    case kXmlErrorUnclosedToken:
      out = Value::string("unclosed token");
      return true;
    case kXmlErrorTagMismatch:
      out = Value::string("mismatched tag");
      return true;
    default:
      out = Value::string("XML parse error");
      return true;
  }
}

bool pyexpat_parser_create_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* parser_class_ptr) {
  if (argc > 3) {
    error = "ParserCreate() expected at most 3 arguments";
    return false;
  }
  std::string encoding;
  std::string namespace_separator;
  auto read_optional_string = [&](const Value& value, std::string& target, const char* label) -> bool {
    if (value.tag == ValueTag::None || value.tag == ValueTag::Invalid) {
      return true;
    }
    auto* string = value_as_string(value);
    if (string == nullptr) {
      error = std::string(label) + " must be str or None";
      return false;
    }
    target = string_object_to_string(*string);
    return true;
  };
  if (argc >= 1 && !read_optional_string(args[0], encoding, "encoding")) {
    return false;
  }
  if (argc >= 2 && !read_optional_string(args[1], namespace_separator, "namespace_separator")) {
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name == "encoding") {
      if (!read_optional_string(*kwargs[i].value, encoding, "encoding")) {
        return false;
      }
    } else if (name == "namespace_separator") {
      if (!read_optional_string(*kwargs[i].value, namespace_separator, "namespace_separator")) {
        return false;
      }
    } else if (name == "intern") {
      continue;
    } else {
      error = "ParserCreate() got an unexpected keyword argument '" + name + "'";
      return false;
    }
  }

  Value parser_class;
  value_assign_fast(parser_class, *static_cast<Value*>(parser_class_ptr));
  out = Value::instance(parser_class);
  auto* state = new PyExpatParserState();
  state->encoding = std::move(encoding);
  state->namespace_separator = std::move(namespace_separator);
  if (!instance_set_native_data(out, kParserNativeType, state, parser_cleanup, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_attr_hooks(out, parser_get_attr, nullptr, nullptr, error)) {
    return false;
  }
  std::string ignored;
  const char* handler_names[] = {
      "StartElementHandler", "EndElementHandler", "CharacterDataHandler", "ProcessingInstructionHandler",
      "UnparsedEntityDeclHandler", "NotationDeclHandler", "StartNamespaceDeclHandler", "EndNamespaceDeclHandler",
      "CommentHandler", "StartCdataSectionHandler", "EndCdataSectionHandler", "DefaultHandler",
      "DefaultHandlerExpand", "NotStandaloneHandler", "ExternalEntityRefHandler", "StartDoctypeDeclHandler",
      "EndDoctypeDeclHandler", "EntityDeclHandler", "XmlDeclHandler", "ElementDeclHandler",
      "AttlistDeclHandler", "SkippedEntityHandler"};
  for (const char* handler_name : handler_names) {
    object_set_attr(out, handler_name, Value::none(), ignored);
  }
  object_set_attr(out, "buffer_text", Value::boolean(false), ignored);
  object_set_attr(out, "buffer_size", Value::int64(8192), ignored);
  object_set_attr(out, "ordered_attributes", Value::boolean(false), ignored);
  object_set_attr(out, "specified_attributes", Value::boolean(false), ignored);
  object_set_attr(out, "namespace_prefixes", Value::boolean(false), ignored);
  object_set_attr(out, "intern", Value::dict({}), ignored);
  (void)runtime;
  return true;
}

bool pyexpat_parser_create(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* parser_class_ptr) {
  return pyexpat_parser_create_kw(runtime, args, argc, nullptr, 0, out, error, parser_class_ptr);
}

Value make_parser_class(Runtime& runtime) {
  auto* parser_class_holder = new Value(Value::invalid());
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("pyexpat")});
  attrs.push_back({"__init__", runtime.make_native_function("pyexpat.XMLParserType.__init__", parser_init)});
  attrs.push_back({"Parse", runtime.make_native_function("pyexpat.XMLParserType.Parse", parser_parse)});
  attrs.push_back({"ParseFile", runtime.make_native_function("pyexpat.XMLParserType.ParseFile", parser_parse_file)});
  attrs.push_back({"SetBase", runtime.make_native_function("pyexpat.XMLParserType.SetBase", parser_set_base)});
  attrs.push_back({"GetBase", runtime.make_native_function("pyexpat.XMLParserType.GetBase", parser_get_base)});
  attrs.push_back({"SetParamEntityParsing", runtime.make_native_function("pyexpat.XMLParserType.SetParamEntityParsing", parser_set_param_entity_parsing)});
  attrs.push_back({"ExternalEntityParserCreate", runtime.make_native_function("pyexpat.XMLParserType.ExternalEntityParserCreate", parser_external_entity_parser_create, parser_class_holder)});
  attrs.push_back({"UseForeignDTD", runtime.make_native_function("pyexpat.XMLParserType.UseForeignDTD", parser_use_foreign_dtd)});
  attrs.push_back({"GetInputContext", runtime.make_native_function("pyexpat.XMLParserType.GetInputContext", parser_get_input_context)});
  attrs.push_back({"GetReparseDeferralEnabled", runtime.make_native_function("pyexpat.XMLParserType.GetReparseDeferralEnabled", parser_get_reparse_deferral_enabled)});
  attrs.push_back({"SetReparseDeferralEnabled", runtime.make_native_function("pyexpat.XMLParserType.SetReparseDeferralEnabled", parser_set_reparse_deferral_enabled)});
  attrs.push_back({"SetBillionLaughsAttackProtectionMaximumAmplification", runtime.make_native_function("pyexpat.XMLParserType.SetBillionLaughsAttackProtectionMaximumAmplification", parser_noop_setter)});
  attrs.push_back({"SetBillionLaughsAttackProtectionActivationThreshold", runtime.make_native_function("pyexpat.XMLParserType.SetBillionLaughsAttackProtectionActivationThreshold", parser_noop_setter)});
  attrs.push_back({"SetAllocTrackerMaximumAmplification", runtime.make_native_function("pyexpat.XMLParserType.SetAllocTrackerMaximumAmplification", parser_noop_setter)});
  attrs.push_back({"SetAllocTrackerActivationThreshold", runtime.make_native_function("pyexpat.XMLParserType.SetAllocTrackerActivationThreshold", parser_noop_setter)});
  *parser_class_holder = Value::class_object("XMLParserType", std::move(attrs));
  return *parser_class_holder;
}

Value make_errors_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "pyexpat.errors");
  builder.value("XML_ERROR_NONE", Value::int64(0))
      .value("XML_ERROR_NO_ELEMENTS", Value::int64(kXmlErrorNoElements))
      .value("XML_ERROR_INVALID_TOKEN", Value::int64(kXmlErrorInvalidToken))
      .value("XML_ERROR_UNCLOSED_TOKEN", Value::int64(kXmlErrorUnclosedToken))
      .value("XML_ERROR_TAG_MISMATCH", Value::int64(kXmlErrorTagMismatch))
      .value("XML_ERROR_SYNTAX", Value::int64(kXmlErrorSyntax))
      .value("messages", Value::dict({
                             {Value::int64(kXmlErrorNoElements), Value::string("no element found")},
                             {Value::int64(kXmlErrorInvalidToken), Value::string("not well-formed (invalid token)")},
                             {Value::int64(kXmlErrorUnclosedToken), Value::string("unclosed token")},
                             {Value::int64(kXmlErrorTagMismatch), Value::string("mismatched tag")},
                         }))
      .value("codes", Value::dict({
                          {Value::string("no element found"), Value::int64(kXmlErrorNoElements)},
                          {Value::string("not well-formed (invalid token)"), Value::int64(kXmlErrorInvalidToken)},
                          {Value::string("unclosed token"), Value::int64(kXmlErrorUnclosedToken)},
                          {Value::string("mismatched tag"), Value::int64(kXmlErrorTagMismatch)},
                      }));
  return builder.finish();
}

Value make_model_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "pyexpat.model");
  builder.value("XML_CTYPE_EMPTY", Value::int64(1))
      .value("XML_CTYPE_ANY", Value::int64(2))
      .value("XML_CTYPE_MIXED", Value::int64(3))
      .value("XML_CTYPE_NAME", Value::int64(4))
      .value("XML_CTYPE_CHOICE", Value::int64(5))
      .value("XML_CTYPE_SEQ", Value::int64(6))
      .value("XML_CQUANT_NONE", Value::int64(0))
      .value("XML_CQUANT_OPT", Value::int64(1))
      .value("XML_CQUANT_REP", Value::int64(2))
      .value("XML_CQUANT_PLUS", Value::int64(3));
  return builder.finish();
}

} // namespace

void register_pyexpat_module(Runtime& runtime) {
  Value exception_base = runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::invalid();
  Value expat_error = Value::class_object("ExpatError", {{"__module__", Value::string("pyexpat")}}, exception_base);
  Value parser_class = make_parser_class(runtime);
  Value errors = make_errors_module(runtime);
  Value model = make_model_module(runtime);

  auto* parser_class_holder = new Value(parser_class);
  NativeModuleBuilder builder(runtime, "pyexpat");
  builder.value("EXPAT_VERSION", Value::string("expat_2.8.2"))
      .value("version_info", Value::tuple({Value::int64(2), Value::int64(8), Value::int64(2)}))
      .value("native_encoding", Value::string("UTF-8"))
      .value("features",
             Value::list({
                 Value::tuple({Value::string("sizeof(XML_Char)"), Value::int64(1)}),
                 Value::tuple({Value::string("sizeof(XML_LChar)"), Value::int64(1)}),
                 Value::tuple({Value::string("XML_DTD"), Value::int64(0)}),
                 Value::tuple({Value::string("XML_CONTEXT_BYTES"), Value::int64(1024)}),
                 Value::tuple({Value::string("XML_NS"), Value::int64(0)}),
             }))
      .value("XML_PARAM_ENTITY_PARSING_NEVER", Value::int64(kXmlParamEntityParsingNever))
      .value("XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE", Value::int64(kXmlParamEntityParsingUnlessStandalone))
      .value("XML_PARAM_ENTITY_PARSING_ALWAYS", Value::int64(kXmlParamEntityParsingAlways))
      .value("XMLParserType", parser_class)
      .value("ExpatError", expat_error)
      .value("error", expat_error)
      .value("errors", errors)
      .value("model", model)
      .value("expat_CAPI", Value::none())
      .function("ErrorString", pyexpat_error_string)
      .value(
          "ParserCreate",
          runtime.make_native_function(
              "pyexpat.ParserCreate",
              pyexpat_parser_create,
              parser_class_holder,
              nullptr,
              nullptr,
              false,
              pyexpat_parser_create_kw));
  runtime.register_module("pyexpat.errors", errors);
  runtime.register_module("pyexpat.model", model);
  runtime.register_module("pyexpat", builder.finish());
  (void)parser_class_holder;
}

} // namespace xlang3
