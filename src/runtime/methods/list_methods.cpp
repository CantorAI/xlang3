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
#include "xlang3/builtin_methods.h"

#include "xlang3/sequence.h"

namespace xlang3 {

namespace {

bool list_append_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 2, "list.append", error)) {
    return false;
  }
  Value list = args[0];
  if (!sequence_list_append(list, args[1], error)) {
    return false;
  }
  out = Value::none();
  return true;
}

bool list_pop_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 1, "list.pop", error)) {
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.pop target is not a list";
    return false;
  }
  if (list->items.empty()) {
    error = "pop from empty list";
    return false;
  }
  out = list->items.back();
  list->items.pop_back();
  return true;
}

} // namespace

bool list_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_list(object) == nullptr) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"append", "list.append", list_append_method},
      {"pop", "list.pop", list_pop_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
