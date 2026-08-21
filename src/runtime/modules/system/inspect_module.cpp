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

#include "xlang3/generator.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <algorithm>

namespace xlang3 {

namespace {

bool inspect_return_bool(bool value, Value& out) {
  value_set_bool(out, value);
  return true;
}

bool inspect_arity_one(const char* name, uint32_t argc, std::string& error) {
  if (argc == 1) {
    return true;
  }
  error = std::string("inspect.") + name + "() expected one argument";
  return false;
}

bool inspect_ismodule(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("ismodule", argc, error) && inspect_return_bool(value_as_module(args[0]) != nullptr, out);
}

bool inspect_isclass(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isclass", argc, error) && inspect_return_bool(value_as_class(args[0]) != nullptr, out);
}

bool inspect_isfunction(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isfunction", argc, error) && inspect_return_bool(value_as_function(args[0]) != nullptr, out);
}

bool inspect_isbuiltin(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isbuiltin", argc, error) && inspect_return_bool(value_as_native_function(args[0]) != nullptr, out);
}

bool inspect_ismethod(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("ismethod", argc, error) && inspect_return_bool(value_as_bound_method(args[0]) != nullptr, out);
}

bool inspect_isroutine(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("isroutine", argc, error)) {
    return false;
  }
  const bool is_routine = value_as_function(args[0]) != nullptr || value_as_native_function(args[0]) != nullptr ||
                          value_as_bound_method(args[0]) != nullptr;
  return inspect_return_bool(is_routine, out);
}

bool inspect_isgenerator(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isgenerator", argc, error) && inspect_return_bool(value_as_generator(args[0]) != nullptr, out);
}

bool inspect_iscode(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("iscode", argc, error) && inspect_return_bool(value_as_code(args[0]) != nullptr, out);
}

bool inspect_isframe(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isframe", argc, error) && inspect_return_bool(value_as_frame(args[0]) != nullptr, out);
}

bool inspect_istraceback(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("istraceback", argc, error) && inspect_return_bool(value_as_traceback(args[0]) != nullptr, out);
}

bool inspect_currentframe(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "inspect.currentframe() expected no arguments";
    return false;
  }
  out = runtime.current_frame_snapshot();
  return true;
}

bool inspect_stack(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "inspect.stack() expected optional context";
    return false;
  }
  out = Value::list({});
  return true;
}

bool inspect_getmodule(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "inspect.getmodule() expected object";
    return false;
  }
  value_set_none(out);
  return true;
}

bool inspect_getfile(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "inspect.getfile() expected object";
    return false;
  }
  if (auto* module = value_as_module(args[0])) {
    Value file;
    if (module_get_attr(args[0], "__file__", file, error)) {
      value_assign_fast(out, file);
      return true;
    }
    out = Value::string(module->name);
    return true;
  }
  Value file;
  std::string ignored;
  if (object_get_attr(args[0], "__file__", file, ignored)) {
    value_assign_fast(out, file);
    return true;
  }
  out = Value::string("<xlang3>");
  return true;
}

void append_member(std::vector<std::pair<std::string, Value>>& members, const std::string& name, const Value& value) {
  if (!name.empty() && name[0] == '#') {
    return;
  }
  members.push_back({name, value});
}

bool collect_members(const Value& object, std::vector<std::pair<std::string, Value>>& members) {
  if (auto* module = value_as_module(object)) {
    for (const auto& entry : module->name_to_slot) {
      if (entry.second < module->slots.size()) {
        append_member(members, entry.first, module->slots[entry.second]);
      }
    }
    return true;
  }
  if (auto* klass = value_as_class(object)) {
    for (const auto& entry : klass->attrs) {
      append_member(members, entry.first, entry.second);
    }
    return true;
  }
  if (auto* instance = value_as_instance(object)) {
    for (const auto& entry : instance->attrs) {
      append_member(members, entry.first, entry.second);
    }
    if (auto* klass = value_as_class(instance->klass)) {
      for (const auto& entry : klass->attrs) {
        append_member(members, entry.first, entry.second);
      }
    }
    return true;
  }
  return false;
}

bool member_matches_predicate(Runtime& runtime, const Value& predicate, const Value& member, std::string& error) {
  auto* native = value_as_native_function(predicate);
  if (native == nullptr || native->callback == nullptr) {
    error = "inspect.getmembers() predicate must be a native callable in this runtime foundation";
    return false;
  }
  Value predicate_result;
  if (!native->callback(runtime, &member, 1, predicate_result, error, native->user_data)) {
    return false;
  }
  return value_truthy(predicate_result);
}

bool inspect_getmembers(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "inspect.getmembers() expected object and optional predicate";
    return false;
  }
  std::vector<std::pair<std::string, Value>> members;
  collect_members(args[0], members);
  std::sort(
      members.begin(),
      members.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
      });
  std::vector<Value> result;
  result.reserve(members.size());
  for (const auto& member : members) {
    if (argc == 2 && !member_matches_predicate(runtime, args[1], member.second, error)) {
      if (!error.empty()) {
        return false;
      }
      continue;
    }
    result.push_back(Value::tuple({Value::string(member.first), member.second}));
  }
  out = Value::list(std::move(result));
  return true;
}

} // namespace

void register_inspect_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "inspect");
  builder.function("ismodule", inspect_ismodule)
      .function("isclass", inspect_isclass)
      .function("isfunction", inspect_isfunction)
      .function("isbuiltin", inspect_isbuiltin)
      .function("ismethod", inspect_ismethod)
      .function("isroutine", inspect_isroutine)
      .function("isgenerator", inspect_isgenerator)
      .function("iscode", inspect_iscode)
      .function("isframe", inspect_isframe)
      .function("istraceback", inspect_istraceback)
      .function("currentframe", inspect_currentframe)
      .function("stack", inspect_stack)
      .function("getmodule", inspect_getmodule)
      .function("getfile", inspect_getfile)
      .function("getsourcefile", inspect_getfile)
      .function("getmembers", inspect_getmembers);
  runtime.register_module("inspect", builder.finish());
}

} // namespace xlang3
