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

#include <cstdlib>
#include <string>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kArgumentParserNativeType = "argparse.ArgumentParser";

struct ArgSpec {
  std::string option;
  std::string dest;
  Value default_value = Value::none();
  Value type_value = Value::none();
  bool store_true = false;
};

struct ArgumentParserState {
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

ArgumentParserState* parser_state(const Value& self, std::string& error) {
  auto* state = static_cast<ArgumentParserState*>(instance_get_native_data(self, kArgumentParserNativeType));
  if (state == nullptr) {
    error = "invalid argparse.ArgumentParser object";
  }
  return state;
}

bool parser_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "ArgumentParser.__init__ expected self";
    return false;
  }
  auto* state = new ArgumentParserState();
  if (!instance_set_native_data(args[0], kArgumentParserNativeType, state, parser_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
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
  if (!string_value(args[1], spec.option)) {
    error = "ArgumentParser.add_argument() option must be str";
    return false;
  }
  spec.dest = option_to_dest(spec.option);
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      continue;
    }
    if (name == "default") {
      value_assign_fast(spec.default_value, *kwargs[i].value);
    } else if (name == "type") {
      value_assign_fast(spec.type_value, *kwargs[i].value);
    } else if (name == "action") {
      std::string action;
      if (string_value(*kwargs[i].value, action) && action == "store_true") {
        spec.store_true = true;
        spec.default_value = Value::boolean(false);
      }
    } else if (name == "dest") {
      std::string dest;
      if (string_value(*kwargs[i].value, dest)) {
        spec.dest = std::move(dest);
      }
    }
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
  out = Value::string(text);
  return true;
}

bool parser_parse_args(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ArgumentParser.parse_args() expected optional args";
    return false;
  }
  auto* state = parser_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> argv;
  if (argc == 2) {
    if (auto* list = value_as_list(args[1])) {
      argv = list->items;
    } else if (auto* tuple = value_as_tuple(args[1])) {
      argv = tuple->items;
    } else {
      error = "ArgumentParser.parse_args() args must be list or tuple";
      return false;
    }
  }

  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("argparse")});
  Value ns_class = Value::class_object("Namespace", std::move(attrs));
  out = Value::instance(ns_class);
  for (const auto& spec : state->args) {
    object_set_attr(out, spec.dest, spec.default_value, error);
  }
  for (size_t i = 0; i < argv.size(); ++i) {
    std::string option;
    if (!string_value(argv[i], option)) {
      continue;
    }
    for (const auto& spec : state->args) {
      if (option != spec.option) {
        continue;
      }
      if (spec.store_true) {
        object_set_attr(out, spec.dest, Value::boolean(true), error);
      } else {
        if (i + 1 >= argv.size()) {
          error = "argument " + option + " expected one value";
          return false;
        }
        std::string text;
        if (!string_value(argv[++i], text)) {
          error = "argument " + option + " value must be str";
          return false;
        }
        Value converted;
        convert_arg_value(spec, text, converted);
        object_set_attr(out, spec.dest, converted, error);
      }
      break;
    }
  }
  return true;
}

bool parser_error(Runtime&, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  error = argc >= 2 ? "argument error" : "argument error";
  return false;
}

Value make_argument_parser_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("argparse.ArgumentParser.__init__", parser_init)});
  attrs.push_back({"add_argument", runtime.make_native_function("argparse.ArgumentParser.add_argument", parser_add_argument, nullptr, nullptr, nullptr, false, parser_add_argument_kw)});
  attrs.push_back({"parse_args", runtime.make_native_function("argparse.ArgumentParser.parse_args", parser_parse_args)});
  attrs.push_back({"error", runtime.make_native_function("argparse.ArgumentParser.error", parser_error)});
  return Value::class_object("ArgumentParser", std::move(attrs));
}

} // namespace

void register_argparse_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "argparse");
  builder.value("SUPPRESS", Value::string("==SUPPRESS=="))
      .value("ArgumentParser", make_argument_parser_class(runtime));
  runtime.register_module("argparse", builder.finish());
}

} // namespace xlang3
