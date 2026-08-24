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

#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/value_hash.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace xlang3 {

namespace {

struct DataclassField {
  std::string name;
  Value default_value;
  bool has_default = false;
};

struct DataclassState {
  Value klass;
  std::vector<DataclassField> fields;
};

struct DataclassOptions {
  bool init = true;
  bool repr = true;
  bool eq = true;
};

void dataclass_state_cleanup(void* data) {
  delete static_cast<std::shared_ptr<DataclassState>*>(data);
}

void field_names_from_annotations(const Value& annotations, std::vector<std::string>& out) {
  auto* dict = value_as_dict(annotations);
  if (dict == nullptr) {
    return;
  }
  for (const auto& entry : dict->entries) {
    auto* key = value_as_string(entry.first);
    if (key != nullptr) {
      out.push_back(string_object_to_string(*key));
    }
  }
}

Value make_dataclass_field_object(const DataclassField& field) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("dataclasses")});
  Value klass = Value::class_object("Field", std::move(attrs));
  Value instance = Value::instance(klass);
  std::string ignored;
  object_set_attr(instance, "name", Value::string(field.name), ignored);
  object_set_attr(instance, "default", field.has_default ? field.default_value : Value::none(), ignored);
  return instance;
}

std::shared_ptr<DataclassState> collect_dataclass_state(Value klass) {
  auto state = std::make_shared<DataclassState>();
  state->klass = std::move(klass);
  Value annotations;
  std::string ignored;
  if (!object_get_attr(state->klass, "__annotations__", annotations, ignored)) {
    annotations = Value::dict({});
  }
  std::vector<std::string> names;
  field_names_from_annotations(annotations, names);
  state->fields.reserve(names.size());
  for (const auto& name : names) {
    DataclassField field;
    field.name = name;
    Value default_value;
    std::string default_error;
    if (object_get_attr(state->klass, name, default_value, default_error)) {
      field.default_value = std::move(default_value);
      field.has_default = true;
    }
    state->fields.push_back(std::move(field));
  }
  return state;
}

bool dataclass_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  auto* shared = static_cast<std::shared_ptr<DataclassState>*>(user_data);
  if (shared == nullptr || !*shared) {
    error = "invalid dataclass __init__";
    return false;
  }
  const auto& state = **shared;
  if (argc == 0) {
    error = "dataclass __init__ expected self";
    return false;
  }
  Value self = args[0];
  std::vector<bool> assigned(state.fields.size(), false);
  uint32_t positional_index = 1;
  for (size_t i = 0; i < state.fields.size() && positional_index < argc; ++i, ++positional_index) {
    if (!object_set_attr(self, state.fields[i].name, args[positional_index], error)) {
      return false;
    }
    assigned[i] = true;
  }
  if (positional_index < argc) {
    error = "dataclass __init__ got too many positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "dataclass __init__ keyword is invalid";
      return false;
    }
    bool matched = false;
    for (size_t field_index = 0; field_index < state.fields.size(); ++field_index) {
      if (state.fields[field_index].name == kwargs[i].name) {
        if (assigned[field_index]) {
          error = "dataclass __init__ got multiple values for argument '" + state.fields[field_index].name + "'";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        if (!object_set_attr(self, state.fields[field_index].name, *kwargs[i].value, error)) {
          return false;
        }
        assigned[field_index] = true;
        matched = true;
        break;
      }
    }
    if (!matched) {
      error = std::string("dataclass __init__ got unexpected keyword argument '") + kwargs[i].name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  for (size_t i = 0; i < state.fields.size(); ++i) {
    if (assigned[i]) {
      continue;
    }
    if (!state.fields[i].has_default) {
      error = "dataclass __init__ missing required argument '" + state.fields[i].name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (!object_set_attr(self, state.fields[i].name, state.fields[i].default_value, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool dataclass_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return dataclass_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool dataclass_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* shared = static_cast<std::shared_ptr<DataclassState>*>(user_data);
  if (shared == nullptr || !*shared || argc == 0) {
    error = "invalid dataclass __repr__";
    return false;
  }
  const auto& state = **shared;
  auto* klass = value_as_class(state.klass);
  std::string text = klass == nullptr ? "<dataclass>" : klass->name;
  text += "(";
  for (size_t i = 0; i < state.fields.size(); ++i) {
    if (i != 0) {
      text += ", ";
    }
    Value value;
    std::string attr_error;
    object_get_attr(args[0], state.fields[i].name, value, attr_error);
    text += state.fields[i].name;
    text += "=";
    text += value_to_string(value);
  }
  text += ")";
  out = Value::string(std::move(text));
  return true;
}

bool dataclass_eq(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* shared = static_cast<std::shared_ptr<DataclassState>*>(user_data);
  if (shared == nullptr || !*shared || argc != 2) {
    error = "invalid dataclass __eq__";
    return false;
  }
  const auto& state = **shared;
  auto* left = value_as_instance(args[0]);
  auto* right = value_as_instance(args[1]);
  if (left == nullptr || right == nullptr || !value_is(left->klass, right->klass)) {
    value_set_bool(out, false);
    return true;
  }
  for (const auto& field : state.fields) {
    Value lhs;
    Value rhs;
    std::string attr_error;
    if (!object_get_attr(args[0], field.name, lhs, attr_error) ||
        !object_get_attr(args[1], field.name, rhs, attr_error)) {
      value_set_bool(out, false);
      return true;
    }
    Value equal;
    std::string compare_error;
    if (!value_compare("==", lhs, rhs, equal, compare_error) || !value_truthy(equal)) {
      value_set_bool(out, false);
      return true;
    }
  }
  value_set_bool(out, true);
  return true;
}

bool apply_dataclass(Runtime& runtime, Value klass, const DataclassOptions& options, Value& out, std::string& error) {
  auto* class_object = value_as_class(klass);
  if (class_object == nullptr) {
    error = "dataclass() expected a class";
    return false;
  }
  auto state = collect_dataclass_state(klass);
  std::vector<std::pair<Value, Value>> field_entries;
  field_entries.reserve(state->fields.size());
  for (const auto& field : state->fields) {
    field_entries.push_back({Value::string(field.name), make_dataclass_field_object(field)});
  }
  if (!object_set_attr(klass, "__dataclass_fields__", Value::dict(std::move(field_entries)), error)) {
    return false;
  }
  auto make_state_data = [&state]() {
    return new std::shared_ptr<DataclassState>(state);
  };
  if (options.init && class_object->attrs.find("__init__") == class_object->attrs.end()) {
    Value init = runtime.make_native_function(
        "dataclasses.__init__",
        dataclass_init,
        make_state_data(),
        dataclass_state_cleanup,
        nullptr,
        false,
        dataclass_init_kw);
    if (!object_set_attr(klass, "__init__", init, error)) {
      return false;
    }
  }
  if (options.repr && class_object->attrs.find("__repr__") == class_object->attrs.end()) {
    Value repr = runtime.make_native_function("dataclasses.__repr__", dataclass_repr, make_state_data(), dataclass_state_cleanup);
    if (!object_set_attr(klass, "__repr__", repr, error)) {
      return false;
    }
  }
  if (options.eq && class_object->attrs.find("__eq__") == class_object->attrs.end()) {
    Value eq = runtime.make_native_function("dataclasses.__eq__", dataclass_eq, make_state_data(), dataclass_state_cleanup);
    if (!object_set_attr(klass, "__eq__", eq, error)) {
      return false;
    }
  }
  value_assign_fast(out, klass);
  return true;
}

bool dataclass_apply(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "dataclass decorator expected one class";
    return false;
  }
  DataclassOptions options;
  if (user_data != nullptr) {
    options = *static_cast<DataclassOptions*>(user_data);
  }
  return apply_dataclass(runtime, args[0], options, out, error);
}

void dataclass_options_cleanup(void* data) {
  delete static_cast<DataclassOptions*>(data);
}

bool dataclass_entry(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc > 1) {
    error = "dataclass() expected optional class";
    return false;
  }
  DataclassOptions options;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "dataclass() keyword is invalid";
      return false;
    }
    const std::string name(kwargs[i].name);
    if (name == "init") {
      options.init = value_truthy(*kwargs[i].value);
    } else if (name == "repr") {
      options.repr = value_truthy(*kwargs[i].value);
    } else if (name == "eq") {
      options.eq = value_truthy(*kwargs[i].value);
    }
  }
  if (argc == 1) {
    return apply_dataclass(runtime, args[0], options, out, error);
  }
  auto* stored_options = new DataclassOptions(options);
  out = runtime.make_native_function(
      "dataclasses.dataclass.<decorator>",
      dataclass_apply,
      stored_options,
      dataclass_options_cleanup);
  return true;
}

bool dataclass_entry_positional(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return dataclass_entry(runtime, args, argc, nullptr, 0, out, error, nullptr);
}

bool field_entry(
    Runtime&,
    const Value*,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "field() expected keyword arguments";
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("dataclasses")});
  Value klass = Value::class_object("Field", std::move(attrs));
  out = Value::instance(klass);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "field() keyword is invalid";
      return false;
    }
    if (!object_set_attr(out, kwargs[i].name, *kwargs[i].value, error)) {
      return false;
    }
  }
  return true;
}

bool field_entry_positional(Runtime&, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  if (argc != 0) {
    error = "field() expected keyword arguments";
    return false;
  }
  error = "field() requires keyword-call path";
  return false;
}

} // namespace

void register_dataclasses_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "dataclasses");
  Value field_class = Value::class_object("Field", {{"__module__", Value::string("dataclasses")}});
  builder.value(
      "dataclass",
      runtime.make_native_function(
          "dataclasses.dataclass",
          dataclass_entry_positional,
          nullptr,
          nullptr,
          nullptr,
          false,
          dataclass_entry));
  builder.value("Field", field_class)
      .value("MISSING", Value::string("<dataclasses._MISSING_TYPE object>"));
  builder.value(
      "field",
      runtime.make_native_function(
          "dataclasses.field",
          field_entry_positional,
          nullptr,
          nullptr,
          nullptr,
          false,
          field_entry));
  runtime.register_module("dataclasses", builder.finish());
}

} // namespace xlang3
