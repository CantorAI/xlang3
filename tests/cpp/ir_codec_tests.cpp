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
      "print('sum', x)\n";

  auto parsed = xlang3::parse_source(source.c_str());
  require(parsed.errors.empty(), "parse failed");
  auto lowered = xlang3::lower_to_ir(parsed.module);
  require(lowered.errors.empty(), "lower failed");

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

  xlang3::ir::Module stale;
  require(
      !xlang3::ir::decode_module(encoded.bytes.data(), encoded.bytes.size(), hash + 1, stale, error),
      "stale IR cache should be rejected");
  return 0;
}
