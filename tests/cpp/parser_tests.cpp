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
#include "test_harness.h"

#include "xlang3/source_cursor.h"

int main() {
  xlang3::test::CaseResult result;
  xlang3::SourceLines lines("alpha\r\n beta\n\nomega");
  xlang3::SourceLine line;
  xlang3::test::expect_true(result, lines.next(line) && line.text == "alpha" && line.line == 1, "SourceLines should read first CRLF line");
  xlang3::test::expect_true(result, lines.next(line) && line.text == " beta" && line.line == 2, "SourceLines should read second LF line");
  xlang3::test::expect_true(result, lines.next(line) && line.text.empty() && line.line == 3, "SourceLines should preserve empty lines");
  xlang3::test::expect_true(result, lines.next(line) && line.text == "omega" && line.line == 4, "SourceLines should read final line");
  xlang3::test::expect_true(result, !lines.next(line), "SourceLines should end after final line");

  std::string_view raw_header = xlang3::trim_ascii_space("  sql sqlite  ");
  std::string_view word;
  size_t word_offset = 0;
  xlang3::test::expect_true(result, xlang3::next_ascii_word(raw_header, word_offset, word) && word == "sql", "scanner should read raw block language");
  xlang3::test::expect_true(result, xlang3::next_ascii_word(raw_header, word_offset, word) && word == "sqlite", "scanner should read raw block provider");

  const char* source =
      "def main():\n"
      "    x = 1 + 2 * 3\n"
      "    if x > 5:\n"
      "        print(x)\n"
      "    else:\n"
      "        print(0)\n"
      "    for item in range(3):\n"
      "        print(item)\n"
      "    print([item + 1 for item in range(2)])\n"
      "    print([item for item in [1, 2, 3] if item > 1][0])\n"
      "    d = {\"a\": 1}\n"
      "    d[\"b\"] = 2\n"
      "    print({1, 2, 2})\n"
      "    import _builtins\n"
      "    _builtins.print(_builtins.len([1, 2]))\n"
      "\n"
      "main()\n";

  auto parsed = xlang3::parse_source(source);
  xlang3::test::expect_true(result, parsed.errors.empty(), "parser should accept core function/if syntax");
  xlang3::test::expect_true(result, parsed.module.body.size() == 2, "module should contain def and call");

  auto raw = xlang3::parse_source(
      "'''text print\n"
      "hello from raw block\n"
      "'''\n"
      "s = '''text print\n"
      "not a raw block\n"
      "'''\n");
  xlang3::test::expect_true(result, raw.errors.empty(), "parser should accept raw block triple string syntax");
  xlang3::test::expect_true(result, raw.module.body.size() == 2, "raw block parse should preserve assigned string");
  xlang3::test::expect_true(
      result,
      dynamic_cast<xlang3::ast::RawBlockStmt*>(raw.module.body[0].get()) != nullptr,
      "standalone DSL triple string should become RawBlockStmt");
  xlang3::test::expect_true(
      result,
      dynamic_cast<xlang3::ast::AssignStmt*>(raw.module.body[1].get()) != nullptr,
      "assigned DSL-looking triple string should stay a normal assignment");

  auto async_parsed = xlang3::parse_source(
      "async def add(a, b):\n"
      "    return a + b\n"
      "\n"
      "async def main():\n"
      "    return await add(20, 22)\n");
  xlang3::test::expect_true(result, async_parsed.errors.empty(), "parser should accept async def and await");
  xlang3::test::expect_true(result, async_parsed.module.body.size() == 2, "async parse should contain two function defs");
  auto* async_fn = dynamic_cast<xlang3::ast::FunctionDef*>(async_parsed.module.body[0].get());
  xlang3::test::expect_true(result, async_fn != nullptr && async_fn->is_async, "async def should mark FunctionDef");

  return xlang3::test::finish(result);
}
