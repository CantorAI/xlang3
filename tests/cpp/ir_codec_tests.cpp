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
#include "xlang3/ir_codec.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/expression.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

} // namespace

int main() {
  const std::string source =
      "def add(a, b=3, /, c=4, *items, scale=1, **extra):\n"
      "    return (a + b + c + items[0] + extra['z']) * scale\n"
      "\n"
      "args = (5,)\n"
      "kwargs = {'z': 6}\n"
      "x = add(2, *args, scale=2, **kwargs)\n"
      "def gen():\n"
      "    yield x\n"
      "    yield from [1, 2]\n"
      "\n"
      "for item in gen():\n"
      "    x = x + item\n"
      "print('sum', x)\n"
      "@Task(NPU=1 and OS == 'Windows')\n"
      "def captured():\n"
      "    pass\n";

  auto parsed = xlang3::parse_source(source.c_str());
  require(parsed.errors.empty(), "parse failed");
  auto lowered = xlang3::lower_to_ir(parsed.module);
  require(lowered.errors.empty(), "lower failed");
  lowered.module.functions[0].constants.push_back(xlang3::Value::tuple({
      xlang3::Value::int64(42), xlang3::Value::tuple({xlang3::Value::string("nested"), xlang3::Value::invalid()})}));

  const uint64_t hash = xlang3::ir::source_hash64(
      reinterpret_cast<const uint8_t*>(source.data()),
      source.size());
  xlang3::ir::EncodedModule encoded;
  std::string error;
  require(xlang3::ir::encode_module(lowered.module, hash, encoded, error), error.c_str());
  require(!encoded.bytes.empty(), "encoded IR is empty");

  xlang3::ir::Module decoded;
  require(
      xlang3::ir::decode_module(encoded.bytes.data(), encoded.bytes.size(), hash, decoded, error),
      error.c_str());
  require(xlang3::ir::dump_module(decoded) == xlang3::ir::dump_module(lowered.module), "IR round-trip mismatch");
  auto* tuple = xlang3::value_as_tuple(decoded.functions[0].constants.back());
  require(tuple && tuple->items.size() == 2 && tuple->items[0].as.i64 == 42, "tuple constant lost");
  auto* nested = xlang3::value_as_tuple(tuple->items[1]);
  require(nested && nested->items.size() == 2 && nested->items[1].tag == xlang3::ValueTag::Invalid, "nested invalid constant lost");
  bool found_expression = false;
  for (const auto& function : decoded.functions) {
    for (const auto& value : function.constants) {
      if (value.tag != xlang3::ValueTag::Object || !value.as.obj || value.as.obj->kind != xlang3::ObjectKind::Expression) continue;
      found_expression = true;
      std::string bytes;
      require(xlang3::encode_expression(value, bytes, error), "expression encoding failed");
      for (size_t size = 0; size < bytes.size(); ++size) {
        xlang3::Value invalid;
        require(!xlang3::decode_expression(bytes.substr(0, size), invalid, error), "truncated expression accepted");
      }
      xlang3::Value result, reservations;
      const auto bindings = xlang3::Value::dict({
        {xlang3::Value::string("NPU"), xlang3::Value::int64(2)},
        {xlang3::Value::string("OS"), xlang3::Value::string("Windows")}});
      require(xlang3::evaluate_expression(value, bindings, result, reservations, error), "decoded expression evaluation failed");
      require(xlang3::value_truthy(result), "decoded expression should match");
    }
  }
  require(found_expression, "IR did not preserve expression constants");

  xlang3::ir::Module stale;
  require(
      !xlang3::ir::decode_module(encoded.bytes.data(), encoded.bytes.size(), hash + 1, stale, error),
      "stale IR cache should be rejected");
  return 0;
}
