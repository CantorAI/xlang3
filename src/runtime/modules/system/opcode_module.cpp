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

#include <utility>
#include <vector>

namespace xlang3 {

namespace {

bool opcode_false(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_bool(out, false);
  return true;
}

bool opcode_zero(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::int64(0);
  return true;
}

bool opcode_empty_list(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::list({});
  return true;
}

Value make_opcode_map() {
  return Value::dict({
      {Value::string("CACHE"), Value::int64(0)},
      {Value::string("NOP"), Value::int64(27)},
      {Value::string("RETURN_VALUE"), Value::int64(35)},
      {Value::string("BINARY_OP"), Value::int64(44)},
      {Value::string("CALL"), Value::int64(52)},
      {Value::string("CALL_KW"), Value::int64(55)},
      {Value::string("LOAD_CONST"), Value::int64(82)},
      {Value::string("LOAD_FAST"), Value::int64(84)},
      {Value::string("LOAD_NAME"), Value::int64(93)},
      {Value::string("STORE_FAST"), Value::int64(112)},
      {Value::string("RESUME"), Value::int64(128)},
  });
}

Value make_opcode_names() {
  std::vector<Value> names;
  names.reserve(267);
  for (int64_t i = 0; i < 267; ++i) {
    names.push_back(Value::string("<" + std::to_string(i) + ">"));
  }
  const std::pair<int, const char*> known[] = {
      {0, "CACHE"},
      {27, "NOP"},
      {35, "RETURN_VALUE"},
      {44, "BINARY_OP"},
      {52, "CALL"},
      {55, "CALL_KW"},
      {82, "LOAD_CONST"},
      {84, "LOAD_FAST"},
      {93, "LOAD_NAME"},
      {112, "STORE_FAST"},
      {128, "RESUME"},
  };
  for (const auto& item : known) {
    names[static_cast<size_t>(item.first)] = Value::string(item.second);
  }
  return Value::list(std::move(names));
}

} // namespace

void register_opcode_module(Runtime& runtime) {
  NativeModuleBuilder private_builder(runtime, "_opcode");
  private_builder.function("stack_effect", opcode_zero)
      .function("has_arg", opcode_false)
      .function("has_const", opcode_false)
      .function("has_name", opcode_false)
      .function("has_jump", opcode_false)
      .function("has_free", opcode_false)
      .function("has_local", opcode_false)
      .function("has_exc", opcode_false)
      .function("get_intrinsic1_descs", opcode_empty_list)
      .function("get_intrinsic2_descs", opcode_empty_list)
      .function("get_special_method_names", opcode_empty_list)
      .function("get_nb_ops", opcode_empty_list);
  runtime.register_module("_opcode", private_builder.finish());

  NativeModuleBuilder public_builder(runtime, "opcode");
  public_builder.value("HAVE_ARGUMENT", Value::int64(43))
      .value("opmap", make_opcode_map())
      .value("opname", make_opcode_names())
      .value("hasconst", Value::list({Value::int64(82)}))
      .value("hasname", Value::list({Value::int64(93)}))
      .value("hasjrel", Value::list({}))
      .value("hasjabs", Value::list({}))
      .value("haslocal", Value::list({Value::int64(84), Value::int64(112)}))
      .value("hascompare", Value::list({}))
      .function("stack_effect", opcode_zero);
  runtime.register_module("opcode", public_builder.finish());
}

} // namespace xlang3
