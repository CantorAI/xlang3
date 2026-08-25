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
#include "xlang3/module_object.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace xlang3 {

namespace {

Value g_parse_result_class;
Value g_split_result_class;

bool value_sequence_to_vector(const Value& value, std::vector<Value>& out, std::string& error) {
  Value iterator;
  if (!sequence_get_iter(value, iterator, error)) {
    Value parts;
    std::string ignored;
    if (object_get_attr(value, "_parts", parts, ignored)) {
      return value_sequence_to_vector(parts, out, error);
    }
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    out.push_back(std::move(item));
  }
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool is_unreserved(unsigned char ch, const std::string& safe) {
  return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
         safe.find(static_cast<char>(ch)) != std::string::npos;
}

std::string quote_impl(const std::string& text, const std::string& safe, bool plus_spaces) {
  std::string out;
  char hex[4] = {};
  for (unsigned char ch : text) {
    if (plus_spaces && ch == ' ') {
      out.push_back('+');
    } else if (is_unreserved(ch, safe)) {
      out.push_back(static_cast<char>(ch));
    } else {
      std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned int>(ch));
      out.append(hex);
    }
  }
  return out;
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

std::string unquote_impl(const std::string& text, bool plus_spaces) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (plus_spaces && text[i] == '+') {
      out.push_back(' ');
    } else if (text[i] == '%' && i + 2 < text.size()) {
      const int hi = hex_value(text[i + 1]);
      const int lo = hex_value(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        out.push_back(text[i]);
      }
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

std::vector<std::string> split_on_char(const std::string& text, char sep) {
  std::vector<std::string> out;
  std::string current;
  for (char ch : text) {
    if (ch == sep) {
      out.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  out.push_back(current);
  return out;
}

std::string join_strings(const std::vector<std::string>& items, const char* sep) {
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i != 0) {
      out += sep;
    }
    out += items[i];
  }
  return out;
}

struct UrlParts {
  std::string scheme;
  std::string netloc;
  std::string path;
  std::string params;
  std::string query;
  std::string fragment;
};

bool is_scheme_char(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '+' || ch == '-' || ch == '.';
}

bool has_scheme_prefix(const std::string& url, size_t colon) {
  if (colon == std::string::npos || colon == 0) {
    return false;
  }
  if (!std::isalpha(static_cast<unsigned char>(url[0]))) {
    return false;
  }
  for (size_t i = 1; i < colon; ++i) {
    if (!is_scheme_char(url[i])) {
      return false;
    }
  }
  return true;
}

UrlParts split_url(const std::string& input, bool allow_fragments) {
  UrlParts parts;
  std::string rest = input;
  const size_t colon = rest.find(':');
  if (has_scheme_prefix(rest, colon)) {
    parts.scheme = rest.substr(0, colon);
    rest.erase(0, colon + 1);
  }
  if (allow_fragments) {
    const size_t hash = rest.find('#');
    if (hash != std::string::npos) {
      parts.fragment = rest.substr(hash + 1);
      rest.resize(hash);
    }
  }
  const size_t query = rest.find('?');
  if (query != std::string::npos) {
    parts.query = rest.substr(query + 1);
    rest.resize(query);
  }
  if (rest.rfind("//", 0) == 0) {
    rest.erase(0, 2);
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) {
      parts.netloc = rest;
      rest.clear();
    } else {
      parts.netloc = rest.substr(0, slash);
      rest.erase(0, slash);
    }
  }
  const size_t semicolon = rest.find(';');
  if (semicolon != std::string::npos) {
    parts.path = rest.substr(0, semicolon);
    parts.params = rest.substr(semicolon + 1);
  } else {
    parts.path = rest;
  }
  return parts;
}

std::string unsplit_url(const UrlParts& parts, bool include_params) {
  std::string out;
  if (!parts.scheme.empty()) {
    out += parts.scheme;
    out.push_back(':');
  }
  if (!parts.netloc.empty()) {
    out += "//";
    out += parts.netloc;
  }
  out += parts.path;
  if (include_params && !parts.params.empty()) {
    out.push_back(';');
    out += parts.params;
  }
  if (!parts.query.empty()) {
    out.push_back('?');
    out += parts.query;
  }
  if (!parts.fragment.empty()) {
    out.push_back('#');
    out += parts.fragment;
  }
  return out;
}

std::string remove_dot_segments(const std::string& path) {
  const bool absolute = !path.empty() && path[0] == '/';
  const bool trailing = !path.empty() && path.back() == '/';
  std::vector<std::string> stack;
  for (const auto& part : split_on_char(path, '/')) {
    if (part.empty() || part == ".") {
      continue;
    }
    if (part == "..") {
      if (!stack.empty()) {
        stack.pop_back();
      }
      continue;
    }
    stack.push_back(part);
  }
  std::string out = absolute ? "/" : "";
  out += join_strings(stack, "/");
  if (trailing && (out.empty() || out.back() != '/')) {
    out.push_back('/');
  }
  return out.empty() && absolute ? "/" : out;
}

std::string dirname_url_path(const std::string& path) {
  const size_t slash = path.rfind('/');
  if (slash == std::string::npos) {
    return "";
  }
  return path.substr(0, slash + 1);
}

Value make_url_result(const UrlParts& parts, bool split) {
  Value klass = split ? g_split_result_class : g_parse_result_class;
  if (klass.tag != ValueTag::Object || value_as_class(klass) == nullptr) {
    klass = Value::class_object(split ? "SplitResult" : "ParseResult", {{"__module__", Value::string("urllib.parse")}});
  }
  Value result = Value::instance(klass);
  std::string ignored;
  object_set_attr(result, "scheme", Value::string(parts.scheme), ignored);
  object_set_attr(result, "netloc", Value::string(parts.netloc), ignored);
  object_set_attr(result, "path", Value::string(parts.path), ignored);
  if (!split) {
    object_set_attr(result, "params", Value::string(parts.params), ignored);
  }
  object_set_attr(result, "query", Value::string(parts.query), ignored);
  object_set_attr(result, "fragment", Value::string(parts.fragment), ignored);
  object_set_attr(result, "_parts", split
      ? Value::list({Value::string(parts.scheme), Value::string(parts.netloc), Value::string(parts.path), Value::string(parts.query), Value::string(parts.fragment)})
      : Value::list({Value::string(parts.scheme), Value::string(parts.netloc), Value::string(parts.path), Value::string(parts.params), Value::string(parts.query), Value::string(parts.fragment)}),
      ignored);
  object_set_attr(result, "_split", Value::boolean(split), ignored);
  return result;
}

bool get_url_parts_from_sequence(const Value& value, UrlParts& parts, bool split, std::string& error) {
  std::vector<Value> items;
  if (!value_sequence_to_vector(value, items, error)) {
    return false;
  }
  const size_t expected = split ? 5 : 6;
  if (items.size() != expected) {
    error = split ? "urlunsplit() expected a 5-item sequence" : "urlunparse() expected a 6-item sequence";
    return false;
  }
  std::vector<std::string*> targets = split
      ? std::vector<std::string*>{&parts.scheme, &parts.netloc, &parts.path, &parts.query, &parts.fragment}
      : std::vector<std::string*>{&parts.scheme, &parts.netloc, &parts.path, &parts.params, &parts.query, &parts.fragment};
  for (size_t i = 0; i < expected; ++i) {
    if (!get_string_arg(items[i], "url component", *targets[i], error)) {
      return false;
    }
  }
  return true;
}

std::string value_to_query_text(const Value& value) {
  if (auto* text = value_as_string(value)) {
    return string_object_to_string(*text);
  }
  return value_to_string(value);
}

bool result_get_parts(const Value& self, Value& parts, std::string& error) {
  if (!object_get_attr(self, "_parts", parts, error)) {
    error = "invalid urllib.parse result";
    return false;
  }
  return true;
}

bool url_result_len(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "URL result __len__ expected self";
    return false;
  }
  Value parts;
  if (!result_get_parts(args[0], parts, error)) {
    return false;
  }
  return sequence_len(parts, out, error);
}

bool url_result_getitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "URL result __getitem__ expected index";
    return false;
  }
  Value parts;
  if (!result_get_parts(args[0], parts, error)) {
    return false;
  }
  return sequence_get_item(parts, args[1], out, error);
}

bool url_result_geturl(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "URL result geturl expected self";
    return false;
  }
  UrlParts parts;
  Value value;
  if (object_get_attr(args[0], "scheme", value, error) && value_as_string(value) != nullptr) parts.scheme = string_object_to_string(*value_as_string(value));
  if (object_get_attr(args[0], "netloc", value, error) && value_as_string(value) != nullptr) parts.netloc = string_object_to_string(*value_as_string(value));
  if (object_get_attr(args[0], "path", value, error) && value_as_string(value) != nullptr) parts.path = string_object_to_string(*value_as_string(value));
  std::string ignored;
  if (object_get_attr(args[0], "params", value, ignored) && value_as_string(value) != nullptr) parts.params = string_object_to_string(*value_as_string(value));
  if (object_get_attr(args[0], "query", value, error) && value_as_string(value) != nullptr) parts.query = string_object_to_string(*value_as_string(value));
  if (object_get_attr(args[0], "fragment", value, error) && value_as_string(value) != nullptr) parts.fragment = string_object_to_string(*value_as_string(value));
  Value split_value;
  const bool split = object_get_attr(args[0], "_split", split_value, ignored) && split_value.tag == ValueTag::Bool && split_value.as.b;
  out = Value::string(unsplit_url(parts, !split));
  return true;
}

bool urllib_quote(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "urllib.parse.quote() expected string and optional safe";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "quote string", text, error)) {
    return false;
  }
  std::string safe = "/";
  if (argc == 2 && !get_string_arg(args[1], "quote safe", safe, error)) {
    return false;
  }
  out = Value::string(quote_impl(text, safe, false));
  return true;
}

bool urllib_quote_plus(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "urllib.parse.quote_plus() expected string and optional safe";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "quote_plus string", text, error)) {
    return false;
  }
  std::string safe;
  if (argc == 2 && !get_string_arg(args[1], "quote_plus safe", safe, error)) {
    return false;
  }
  out = Value::string(quote_impl(text, safe, true));
  return true;
}

bool urllib_unquote_plus(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "urllib.parse.unquote_plus() expected one string";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "unquote_plus string", text, error)) {
    return false;
  }
  out = Value::string(unquote_impl(text, true));
  return true;
}

bool urllib_unquote(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "urllib.parse.unquote() expected one string";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "unquote string", text, error)) {
    return false;
  }
  out = Value::string(unquote_impl(text, false));
  return true;
}

bool urllib_urlparse(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "urllib.parse.urlparse() expected url, optional scheme, optional allow_fragments";
    return false;
  }
  std::string url;
  if (!get_string_arg(args[0], "urlparse url", url, error)) {
    return false;
  }
  UrlParts parts = split_url(url, argc < 3 || value_truthy(args[2]));
  if (argc >= 2 && parts.scheme.empty() && args[1].tag != ValueTag::None) {
    get_string_arg(args[1], "urlparse scheme", parts.scheme, error);
  }
  out = make_url_result(parts, false);
  return true;
}

bool urllib_urlsplit(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "urllib.parse.urlsplit() expected url, optional scheme, optional allow_fragments";
    return false;
  }
  std::string url;
  if (!get_string_arg(args[0], "urlsplit url", url, error)) {
    return false;
  }
  UrlParts parts = split_url(url, argc < 3 || value_truthy(args[2]));
  if (argc >= 2 && parts.scheme.empty() && args[1].tag != ValueTag::None) {
    get_string_arg(args[1], "urlsplit scheme", parts.scheme, error);
  }
  out = make_url_result(parts, true);
  return true;
}

bool urllib_urlunparse(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "urllib.parse.urlunparse() expected one sequence";
    return false;
  }
  UrlParts parts;
  if (!get_url_parts_from_sequence(args[0], parts, false, error)) {
    return false;
  }
  out = Value::string(unsplit_url(parts, true));
  return true;
}

bool urllib_urlunsplit(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "urllib.parse.urlunsplit() expected one sequence";
    return false;
  }
  UrlParts parts;
  if (!get_url_parts_from_sequence(args[0], parts, true, error)) {
    return false;
  }
  out = Value::string(unsplit_url(parts, false));
  return true;
}

bool urllib_urljoin(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "urllib.parse.urljoin() expected base and url";
    return false;
  }
  std::string base_text;
  std::string url_text;
  if (!get_string_arg(args[0], "urljoin base", base_text, error) ||
      !get_string_arg(args[1], "urljoin url", url_text, error)) {
    return false;
  }
  UrlParts rel = split_url(url_text, true);
  if (!rel.scheme.empty()) {
    out = Value::string(url_text);
    return true;
  }
  UrlParts base = split_url(base_text, true);
  UrlParts result;
  result.scheme = base.scheme;
  if (!rel.netloc.empty()) {
    result.netloc = rel.netloc;
    result.path = remove_dot_segments(rel.path);
    result.query = rel.query;
  } else {
    result.netloc = base.netloc;
    if (rel.path.empty()) {
      result.path = base.path;
      result.query = rel.query.empty() ? base.query : rel.query;
    } else if (!rel.path.empty() && rel.path[0] == '/') {
      result.path = remove_dot_segments(rel.path);
      result.query = rel.query;
    } else {
      result.path = remove_dot_segments(dirname_url_path(base.path) + rel.path);
      result.query = rel.query;
    }
  }
  result.fragment = rel.fragment;
  out = Value::string(unsplit_url(result, false));
  return true;
}

bool urllib_parse_qsl(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "urllib.parse.parse_qsl() expected query string";
    return false;
  }
  std::string query;
  if (!get_string_arg(args[0], "parse_qsl qs", query, error)) {
    return false;
  }
  std::vector<Value> pairs;
  for (const auto& part : split_on_char(query, '&')) {
    if (part.empty()) {
      continue;
    }
    const size_t eq = part.find('=');
    const std::string key = eq == std::string::npos ? part : part.substr(0, eq);
    const std::string value = eq == std::string::npos ? "" : part.substr(eq + 1);
    pairs.push_back(Value::tuple({Value::string(unquote_impl(key, true)), Value::string(unquote_impl(value, true))}));
  }
  out = Value::list(std::move(pairs));
  return true;
}

bool urllib_parse_qs(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  Value qsl;
  if (!urllib_parse_qsl(runtime, args, argc, qsl, error, data)) {
    return false;
  }
  std::vector<std::pair<Value, Value>> dict_items;
  std::vector<Value> pairs;
  value_sequence_to_vector(qsl, pairs, error);
  for (const auto& pair_value : pairs) {
    std::vector<Value> pair;
    value_sequence_to_vector(pair_value, pair, error);
    if (pair.size() != 2) {
      continue;
    }
    bool found = false;
    for (auto& item : dict_items) {
      if (value_to_string(item.first) == value_to_string(pair[0])) {
        auto* list = value_as_list(item.second);
        if (list != nullptr) {
          list->items.push_back(pair[1]);
        }
        found = true;
        break;
      }
    }
    if (!found) {
      dict_items.push_back({pair[0], Value::list({pair[1]})});
    }
  }
  out = Value::dict(std::move(dict_items));
  return true;
}

bool urllib_urlencode(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "urllib.parse.urlencode() expected query object";
    return false;
  }
  std::vector<std::pair<std::string, std::string>> pairs;
  if (auto* dict = value_as_dict(args[0])) {
    for (const auto& entry : dict->entries) {
      pairs.push_back({value_to_query_text(entry.first), value_to_query_text(entry.second)});
    }
  } else {
    std::vector<Value> entries;
    if (!value_sequence_to_vector(args[0], entries, error)) {
      return false;
    }
    for (const auto& entry : entries) {
      std::vector<Value> pair;
      if (!value_sequence_to_vector(entry, pair, error) || pair.size() != 2) {
        error = "urlencode() query sequence items must be pairs";
        return false;
      }
      pairs.push_back({value_to_query_text(pair[0]), value_to_query_text(pair[1])});
    }
  }
  std::vector<std::string> encoded;
  for (const auto& pair : pairs) {
    encoded.push_back(quote_impl(pair.first, "", true) + "=" + quote_impl(pair.second, "", true));
  }
  out = Value::string(join_strings(encoded, "&"));
  return true;
}

} // namespace

void register_urllib_module(Runtime& runtime) {
  g_parse_result_class = Value::class_object(
      "ParseResult",
      {
          {"__module__", Value::string("urllib.parse")},
          {"__len__", runtime.make_native_function("urllib.parse.ParseResult.__len__", url_result_len)},
          {"__getitem__", runtime.make_native_function("urllib.parse.ParseResult.__getitem__", url_result_getitem)},
          {"geturl", runtime.make_native_function("urllib.parse.ParseResult.geturl", url_result_geturl)},
      });
  g_split_result_class = Value::class_object(
      "SplitResult",
      {
          {"__module__", Value::string("urllib.parse")},
          {"__len__", runtime.make_native_function("urllib.parse.SplitResult.__len__", url_result_len)},
          {"__getitem__", runtime.make_native_function("urllib.parse.SplitResult.__getitem__", url_result_getitem)},
          {"geturl", runtime.make_native_function("urllib.parse.SplitResult.geturl", url_result_geturl)},
      });
  NativeModuleBuilder parse_builder(runtime, "urllib.parse");
  parse_builder.function("quote", urllib_quote)
      .function("quote_plus", urllib_quote_plus)
      .function("unquote", urllib_unquote)
      .function("unquote_plus", urllib_unquote_plus)
      .function("urlparse", urllib_urlparse)
      .function("urlsplit", urllib_urlsplit)
      .function("urlunparse", urllib_urlunparse)
      .function("urlunsplit", urllib_urlunsplit)
      .function("urljoin", urllib_urljoin)
      .function("parse_qs", urllib_parse_qs)
      .function("parse_qsl", urllib_parse_qsl)
      .function("urlencode", urllib_urlencode)
      .value("ParseResult", g_parse_result_class)
      .value("SplitResult", g_split_result_class);
  Value parse = parse_builder.finish();
  runtime.register_module("urllib.parse", parse);

  NativeModuleBuilder builder(runtime, "urllib");
  builder.value("parse", parse);
  runtime.register_module("urllib", builder.finish());
}

} // namespace xlang3
