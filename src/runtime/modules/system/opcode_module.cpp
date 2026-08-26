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

Value make_opcode_int_list(std::initializer_list<int64_t> values) {
  std::vector<Value> items;
  items.reserve(values.size());
  for (int64_t value : values) {
    items.push_back(Value::int64(value));
  }
  return Value::list(std::move(items));
}

Value make_cmp_op() {
  return Value::list({
      Value::string("<"),
      Value::string("<="),
      Value::string("=="),
      Value::string("!="),
      Value::string(">"),
      Value::string(">="),
  });
}

Value make_opcode_map() {
  return Value::dict({
      {Value::string("CACHE"), Value::int64(0)},
      {Value::string("NOP"), Value::int64(27)},
      {Value::string("RETURN_VALUE"), Value::int64(35)},
      {Value::string("BINARY_OP"), Value::int64(44)},
      {Value::string("CALL"), Value::int64(52)},
      {Value::string("CALL_KW"), Value::int64(55)},
      {Value::string("EXTENDED_ARG"), Value::int64(69)},
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
      {69, "EXTENDED_ARG"},
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
      .value("EXTENDED_ARG", Value::int64(69))
      .value("cmp_op", make_cmp_op())
      .value("opmap", make_opcode_map())
      .value("opname", make_opcode_names())
      .value("hasarg",
          make_opcode_int_list({128, 255, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
              58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,
              76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91,
              92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106,
              107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
              237, 239, 241, 242, 243, 244, 245, 247, 248, 249, 250, 251, 253, 257,
              258, 259, 260, 261, 263, 264, 265, 266}))
      .value("hasconst", make_opcode_int_list({82}))
      .value("hasname", make_opcode_int_list({61, 64, 65, 72, 73, 80, 91, 92, 93, 96, 110, 115, 116, 249}))
      .value("hasjrel", make_opcode_int_list({68, 70, 75, 76, 77, 100, 101, 102, 103, 106, 237, 248, 257, 258, 259, 260}))
      .value("hasjabs", Value::list({}))
      .value("haslocal", make_opcode_int_list({63, 83, 84, 85, 86, 87, 88, 89, 112, 113, 114, 261, 266}))
      .value("hascompare", make_opcode_int_list({56}))
      .value("hasfree", make_opcode_int_list({62, 90, 97, 111}))
      .value("hasexc", make_opcode_int_list({263, 264, 265}))
      .function("stack_effect", opcode_zero);
  runtime.register_module("opcode", public_builder.finish());
}

} // namespace xlang3
