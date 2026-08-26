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
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kArgumentParserNativeType = "argparse.ArgumentParser";

struct ArgSpec {
  std::vector<std::string> options;
  std::string dest;
  Value default_value = Value::none();
  Value const_value = Value::none();
  Value type_value = Value::none();
  Value choices = Value::none();
  std::string nargs;
  std::string metavar;
  std::string help;
  std::string action = "store";
  bool store_true = false;
  bool store_false = false;
  bool append = false;
  bool count = false;
  bool required = false;
  bool positional = false;
};

struct ArgumentParserState {
  std::string prog;
  std::string description;
  bool add_help = true;
  std::vector<ArgSpec> args;
};

void parser_cleanup(void* data) {
  delete static_cast<ArgumentParserState*>(data);
}

bool string_value(const Value& value, std::string& out) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
  }
  if (value.tag == ValueTag::Int64) {
    out = std::to_string(value.as.i64);
    return true;
  }
  return false;
}

bool list_like_values(const Value& value, std::vector<Value>& out) {
  if (auto* list = value_as_list(value)) {
    out = list->items;
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    out = tuple->items;
    return true;
  }
  return false;
}

std::string option_to_dest(const std::string& option) {
  size_t start = 0;
  while (start < option.size() && option[start] == '-') {
    ++start;
  }
  std::string dest = option.substr(start);
  for (char& ch : dest) {
    if (ch == '-') {
      ch = '_';
    }
  }
  return dest;
}

bool looks_like_option(const std::string& value) {
  return value.size() > 1 && value[0] == '-';
}

std::string uppercase(std::string text) {
  for (char& ch : text) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return text;
}

bool same_value(const Value& lhs, const Value& rhs) {
  std::string ignored;
  Value equal;
  return value_compare("==", lhs, rhs, equal, ignored) && value_truthy(equal);
}

bool value_in_choices(const Value& value, const Value& choices) {
  std::vector<Value> values;
  if (!list_like_values(choices, values)) {
    return true;
  }
  for (const auto& item : values) {
    if (same_value(value, item)) {
      return true;
    }
  }
  return false;
}

uint32_t fixed_nargs_count(const std::string& nargs) {
  if (nargs.empty() || nargs == "?" || nargs == "*" || nargs == "+") {
    return 0;
  }
  char* end = nullptr;
  const auto value = std::strtoul(nargs.c_str(), &end, 10);
  return end != nullptr && *end == '\0' ? static_cast<uint32_t>(value) : 0;
}

ArgumentParserState* parser_state(const Value& self, std::string& error) {
  auto* state = static_cast<ArgumentParserState*>(instance_get_native_data(self, kArgumentParserNativeType));
  if (state == nullptr) {
    error = "invalid argparse.ArgumentParser object";
  }
  return state;
}

bool parser_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ArgumentParser.__init__ expected self";
    return false;
  }
  auto* state = new ArgumentParserState();
  if (argc == 2) {
    string_value(args[1], state->prog);
  }
  if (!instance_set_native_data(args[0], kArgumentParserNativeType, state, parser_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool parser_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!parser_init(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "ArgumentParser.__init__ got invalid keyword argument";
      return false;
    }
    const std::string name(kwargs[i].name);
    if (name == "prog") {
      string_value(*kwargs[i].value, state->prog);
    } else if (name == "description") {
      string_value(*kwargs[i].value, state->description);
    } else if (name == "add_help") {
      state->add_help = value_truthy(*kwargs[i].value);
    } else if (name == "exit_on_error" || name == "allow_abbrev" || name == "formatter_class") {
      continue;
    } else {
      error = "ArgumentParser.__init__ got unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  return true;
}

bool parser_add_argument_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2) {
    error = "ArgumentParser.add_argument() expected option";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  ArgSpec spec;
  for (uint32_t i = 1; i < argc; ++i) {
    std::string option;
    if (!string_value(args[i], option)) {
      error = "ArgumentParser.add_argument() option must be str";
      return false;
    }
    spec.options.push_back(std::move(option));
  }
  if (spec.options.empty()) {
    error = "ArgumentParser.add_argument() expected option";
    return false;
  }
  spec.positional = !looks_like_option(spec.options.front());
  spec.dest = option_to_dest(spec.options.back());
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      continue;
    }
    if (name == "default") {
      value_assign_fast(spec.default_value, *kwargs[i].value);
    } else if (name == "const") {
      value_assign_fast(spec.const_value, *kwargs[i].value);
    } else if (name == "type") {
      value_assign_fast(spec.type_value, *kwargs[i].value);
    } else if (name == "choices") {
      value_assign_fast(spec.choices, *kwargs[i].value);
    } else if (name == "action") {
      std::string action;
      if (string_value(*kwargs[i].value, action)) {
        spec.action = action;
        if (action == "store_true") {
          spec.store_true = true;
          spec.default_value = Value::boolean(false);
        } else if (action == "store_false") {
          spec.store_false = true;
          spec.default_value = Value::boolean(true);
        } else if (action == "append") {
          spec.append = true;
          spec.default_value = Value::list({});
        } else if (action == "count") {
          spec.count = true;
          spec.default_value = Value::int64(0);
        } else if (action == "store_const") {
          spec.default_value = Value::none();
        }
      }
    } else if (name == "dest") {
      std::string dest;
      if (string_value(*kwargs[i].value, dest)) {
        spec.dest = std::move(dest);
      }
    } else if (name == "required") {
      spec.required = value_truthy(*kwargs[i].value);
    } else if (name == "nargs") {
      string_value(*kwargs[i].value, spec.nargs);
    } else if (name == "metavar") {
      string_value(*kwargs[i].value, spec.metavar);
    } else if (name == "help") {
      string_value(*kwargs[i].value, spec.help);
    }
  }
  if (spec.action == "store_const" && spec.const_value.tag == ValueTag::Invalid) {
    spec.const_value = Value::none();
  }
  state->args.push_back(std::move(spec));
  value_assign_fast(out, args[0]);
  return true;
}

bool parser_add_argument(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return parser_add_argument_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool convert_arg_value(const ArgSpec& spec, const std::string& text, Value& out) {
  auto* type_class = value_as_class(spec.type_value);
  if (type_class != nullptr && type_class->name == "int") {
    value_set_int64(out, std::strtoll(text.c_str(), nullptr, 10));
    return true;
  }
  if (type_class != nullptr && type_class->name == "float") {
    out = Value::number(std::strtod(text.c_str(), nullptr));
    return true;
  }
  if (type_class != nullptr && type_class->name == "str") {
    out = Value::string(text);
    return true;
  }
  out = Value::string(text);
  return true;
}

bool make_namespace_instance(Runtime& runtime, Value& out, std::string& error) {
  Value argparse_module;
  if (!runtime.import_module("argparse", argparse_module, error)) {
    return false;
  }
  Value ns_class;
  if (!module_get_attr(argparse_module, "Namespace", ns_class, error)) {
    return false;
  }
  out = Value::instance(ns_class);
  return true;
}

bool set_namespace_attr(Value& ns, const ArgSpec& spec, const Value& value, std::string& error) {
  if (spec.append) {
    Value current;
    std::string ignored;
    if (!object_get_attr(ns, spec.dest, current, ignored) || value_as_list(current) == nullptr) {
      current = Value::list({});
    }
    sequence_list_append(current, value, error);
    return object_set_attr(ns, spec.dest, current, error);
  }
  if (spec.count) {
    Value current;
    std::string ignored;
    int64_t count = 0;
    if (object_get_attr(ns, spec.dest, current, ignored) && current.tag == ValueTag::Int64) {
      count = current.as.i64;
    }
    return object_set_attr(ns, spec.dest, Value::int64(count + 1), error);
  }
  return object_set_attr(ns, spec.dest, value, error);
}

Value parser_default_value(const ArgSpec& spec) {
  if (spec.append) {
    std::vector<Value> items;
    if (auto* list = value_as_list(spec.default_value)) {
      items = list->items;
    }
    return Value::list(std::move(items));
  }
  return spec.default_value;
}

bool read_argv(const Value& value, std::vector<Value>& argv, std::string& error) {
  if (list_like_values(value, argv)) {
    return true;
  }
  error = "ArgumentParser args must be list or tuple";
  return false;
}

size_t consume_values(
    const std::vector<Value>& argv,
    size_t start,
    const ArgSpec& spec,
    std::vector<Value>& values,
    std::string& error) {
  const bool many = spec.nargs == "*" || spec.nargs == "+";
  const bool optional = spec.nargs == "?" || spec.nargs == "*";
  const uint32_t fixed_count = fixed_nargs_count(spec.nargs);
  size_t i = start;
  while (i < argv.size()) {
    std::string text;
    if (!string_value(argv[i], text) || (looks_like_option(text) && !spec.positional)) {
      break;
    }
    if (fixed_count != 0 && values.size() >= fixed_count) {
      break;
    }
    if (!spec.positional && !values.empty() && !many && fixed_count == 0) {
      break;
    }
    Value converted;
    convert_arg_value(spec, text, converted);
    if (!value_in_choices(converted, spec.choices)) {
      error = "invalid choice: " + text;
      return start;
    }
    values.push_back(std::move(converted));
    ++i;
    if (!many && spec.nargs != "*" && spec.nargs != "+" && fixed_count == 0) {
      break;
    }
  }
  if (fixed_count != 0 && values.size() != fixed_count) {
    error = "expected " + std::to_string(fixed_count) + " arguments";
    return start;
  }
  if (values.empty() && !optional && fixed_count == 0) {
    error = "expected one argument";
    return start;
  }
  if (spec.nargs == "+" && values.empty()) {
    error = "expected at least one argument";
    return start;
  }
  return i;
}

bool parser_parse_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    bool allow_unknown) {
  if (argc < 1 || argc > 3) {
    error = allow_unknown ? "ArgumentParser.parse_known_args() expected optional args and namespace" : "ArgumentParser.parse_args() expected optional args and namespace";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> argv;
  Value namespace_value;
  if (argc >= 2 && args[1].tag != ValueTag::None) {
    if (!read_argv(args[1], argv, error)) {
      return false;
    }
  }
  if (argc == 3) {
    value_assign_fast(namespace_value, args[2]);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "ArgumentParser parse got invalid keyword argument";
      return false;
    }
    const std::string name(kwargs[i].name);
    if (name == "args") {
      argv.clear();
      if (!read_argv(*kwargs[i].value, argv, error)) {
        return false;
      }
    } else if (name == "namespace") {
      value_assign_fast(namespace_value, *kwargs[i].value);
    } else {
      error = "ArgumentParser parse got unexpected keyword argument '" + name + "'";
      return false;
    }
  }

  Value ns;
  if (namespace_value.tag == ValueTag::Invalid) {
    if (!make_namespace_instance(runtime, ns, error)) {
      return false;
    }
  } else {
    value_assign_fast(ns, namespace_value);
  }
  for (const auto& spec : state->args) {
    if (spec.default_value.tag != ValueTag::Invalid) {
      object_set_attr(ns, spec.dest, parser_default_value(spec), error);
    }
  }
  size_t positional_index = 0;
  std::vector<Value> unknown;
  std::vector<std::string> seen;
  for (size_t i = 0; i < argv.size(); ++i) {
    std::string option;
    if (!string_value(argv[i], option)) {
      continue;
    }
    if (!looks_like_option(option)) {
      while (positional_index < state->args.size() && !state->args[positional_index].positional) {
        ++positional_index;
      }
      if (positional_index < state->args.size()) {
        const auto& spec = state->args[positional_index];
        std::vector<Value> values;
        const size_t next = consume_values(argv, i, spec, values, error);
        if (!error.empty()) {
          return false;
        }
        if (spec.nargs == "*" || spec.nargs == "+" || fixed_nargs_count(spec.nargs) != 0) {
          set_namespace_attr(ns, spec, Value::list(std::move(values)), error);
        } else if (spec.nargs == "?" && values.empty()) {
          set_namespace_attr(ns, spec, spec.const_value.tag == ValueTag::Invalid ? spec.default_value : spec.const_value, error);
        } else if (!values.empty()) {
          set_namespace_attr(ns, spec, values[0], error);
        }
        seen.push_back(spec.dest);
        ++positional_index;
        i = next == 0 ? i : next - 1;
      } else if (allow_unknown) {
        unknown.push_back(argv[i]);
      } else {
        error = "unrecognized arguments: " + option;
        return false;
      }
      continue;
    }
    bool consumed = false;
    for (const auto& spec : state->args) {
      bool matched = false;
      std::string inline_value;
      for (const auto& candidate : spec.options) {
        if (option == candidate) {
          matched = true;
          break;
        }
        if (option.rfind(candidate + "=", 0) == 0) {
          matched = true;
          inline_value = option.substr(candidate.size() + 1);
          break;
        }
      }
      if (!matched) {
        continue;
      }
      if (spec.store_true) {
        object_set_attr(ns, spec.dest, Value::boolean(true), error);
      } else if (spec.store_false) {
        object_set_attr(ns, spec.dest, Value::boolean(false), error);
      } else if (spec.count) {
        set_namespace_attr(ns, spec, Value::int64(1), error);
      } else if (spec.action == "store_const") {
        set_namespace_attr(ns, spec, spec.const_value, error);
      } else {
        std::vector<Value> values;
        if (!inline_value.empty()) {
          Value converted;
          convert_arg_value(spec, inline_value, converted);
          if (!value_in_choices(converted, spec.choices)) {
            error = "invalid choice: " + inline_value;
            return false;
          }
          values.push_back(std::move(converted));
        } else {
          const size_t next = consume_values(argv, i + 1, spec, values, error);
          if (!error.empty()) {
            error = "argument " + option + " " + error;
            return false;
          }
          i = next == 0 ? i : next - 1;
        }
        if (values.empty()) {
          error = "argument " + option + " expected one value";
          return false;
        }
        if (spec.nargs == "*" || spec.nargs == "+" || fixed_nargs_count(spec.nargs) != 0) {
          set_namespace_attr(ns, spec, Value::list(std::move(values)), error);
        } else {
          set_namespace_attr(ns, spec, values[0], error);
        }
      }
      seen.push_back(spec.dest);
      consumed = true;
      break;
    }
    if (!consumed) {
      if (allow_unknown) {
        unknown.push_back(argv[i]);
        if (i + 1 < argv.size()) {
          std::string next;
          if (string_value(argv[i + 1], next) && !looks_like_option(next)) {
            unknown.push_back(argv[++i]);
          }
        }
      } else {
        error = "unrecognized arguments: " + option;
        return false;
      }
    }
  }
  for (const auto& spec : state->args) {
    if (spec.required && std::find(seen.begin(), seen.end(), spec.dest) == seen.end()) {
      error = "missing required argument: " + spec.dest;
      return false;
    }
  }
  if (allow_unknown) {
    out = Value::tuple({ns, Value::list(std::move(unknown))});
  } else {
    out = ns;
  }
  return true;
}

bool parser_parse_args(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return parser_parse_impl(runtime, args, argc, nullptr, 0, out, error, false);
}

bool parser_parse_args_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return parser_parse_impl(runtime, args, argc, kwargs, kwargc, out, error, false);
}

bool parser_parse_known_args(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return parser_parse_impl(runtime, args, argc, nullptr, 0, out, error, true);
}

bool parser_parse_known_args_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return parser_parse_impl(runtime, args, argc, kwargs, kwargc, out, error, true);
}

bool parser_format_usage(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ArgumentParser.format_usage() expected no arguments";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string usage = "usage: " + (state->prog.empty() ? std::string("prog") : state->prog);
  for (const auto& spec : state->args) {
    if (spec.positional) {
      usage += " " + (spec.metavar.empty() ? spec.dest : spec.metavar);
    } else {
      usage += spec.required ? " " : " [";
      usage += spec.options.empty() ? spec.dest : spec.options.back();
      if (!spec.store_true && !spec.store_false && !spec.count && spec.action != "store_const") {
        usage += " " + (spec.metavar.empty() ? uppercase(spec.dest) : spec.metavar);
      }
      if (!spec.required) {
        usage += "]";
      }
    }
  }
  usage += "\n";
  out = Value::string(std::move(usage));
  return true;
}

bool parser_format_help(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  Value usage;
  if (!parser_format_usage(runtime, args, argc, usage, error, user_data)) {
    return false;
  }
  std::string text = string_object_to_string(*value_as_string(usage));
  auto* state = parser_state(args[0], error);
  if (state != nullptr && !state->description.empty()) {
    text += "\n" + state->description + "\n";
  }
  out = Value::string(std::move(text));
  return true;
}

bool parser_error(Runtime&, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  error = argc >= 2 ? "argument error" : "argument error";
  return false;
}

bool namespace_init_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "Namespace.__init__ expected self";
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "Namespace.__init__ got invalid keyword argument";
      return false;
    }
    Value self = args[0];
    if (!object_set_attr(self, kwargs[i].name, *kwargs[i].value, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool namespace_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return namespace_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

Value make_namespace_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("argparse")});
  attrs.push_back({"__init__", runtime.make_native_function("argparse.Namespace.__init__", namespace_init, nullptr, nullptr, nullptr, false, namespace_init_kw)});
  return Value::class_object("Namespace", std::move(attrs));
}

Value make_argument_parser_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("argparse.ArgumentParser.__init__", parser_init, nullptr, nullptr, nullptr, false, parser_init_kw)});
  attrs.push_back({"add_argument", runtime.make_native_function("argparse.ArgumentParser.add_argument", parser_add_argument, nullptr, nullptr, nullptr, false, parser_add_argument_kw)});
  attrs.push_back({"parse_args", runtime.make_native_function("argparse.ArgumentParser.parse_args", parser_parse_args, nullptr, nullptr, nullptr, false, parser_parse_args_kw)});
  attrs.push_back({"parse_known_args", runtime.make_native_function("argparse.ArgumentParser.parse_known_args", parser_parse_known_args, nullptr, nullptr, nullptr, false, parser_parse_known_args_kw)});
  attrs.push_back({"format_usage", runtime.make_native_function("argparse.ArgumentParser.format_usage", parser_format_usage)});
  attrs.push_back({"format_help", runtime.make_native_function("argparse.ArgumentParser.format_help", parser_format_help)});
  attrs.push_back({"error", runtime.make_native_function("argparse.ArgumentParser.error", parser_error)});
  return Value::class_object("ArgumentParser", std::move(attrs));
}

} // namespace

void register_argparse_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "argparse");
  builder.value("SUPPRESS", Value::string("==SUPPRESS=="))
      .value("Namespace", make_namespace_class(runtime))
      .value("ArgumentParser", make_argument_parser_class(runtime));
  runtime.register_module("argparse", builder.finish());
}

} // namespace xlang3
