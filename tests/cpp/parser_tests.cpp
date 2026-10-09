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
  auto assertions = xlang3::parse_source("assert False, 'failure'\nassert (False, 'tuple')\nassert True\n");
  xlang3::test::expect_true(result, assertions.errors.empty() && assertions.module.body.size() == 3,
      "parser should accept assertion forms");
  if (assertions.module.body.size() == 3) {
    auto* with_message = dynamic_cast<xlang3::ast::AssertStmt*>(assertions.module.body[0].get());
    auto* with_tuple = dynamic_cast<xlang3::ast::AssertStmt*>(assertions.module.body[1].get());
    auto* simple = dynamic_cast<xlang3::ast::AssertStmt*>(assertions.module.body[2].get());
    xlang3::test::expect_true(result, with_message && with_message->message &&
        !dynamic_cast<xlang3::ast::TupleExpr*>(with_message->condition.get()),
        "assert message must not become part of the condition tuple");
    xlang3::test::expect_true(result, with_tuple && !with_tuple->message &&
        dynamic_cast<xlang3::ast::TupleExpr*>(with_tuple->condition.get()),
        "parenthesized assertion tuple must remain a tuple");
    xlang3::test::expect_true(result, simple && !simple->message,
        "simple assertion must have no message");
  }
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

  auto continued = xlang3::parse_source(
      "a = 1; b = 2; print(a + b)\n"
      "c = 1 + \\\n"
      "    2\n"
      "d = [\n"
      "    3,\n"
      "    4,\n"
      "]\n");
  xlang3::test::expect_true(result, continued.errors.empty(), "parser should accept semicolon and continued logical lines");
  xlang3::test::expect_true(result, continued.module.body.size() == 5, "continued parse should produce expected statements");

  auto continued_triple_arg = xlang3::parse_source(
      "def configure(parser):\n"
      "    parser.add_argument(\"input\", nargs=\"*\",\n"
      "                        help=\"\"\"\\\n"
      "if no options given, output depends on the input\n"
      "    string or multiple: same as --choice\n"
      "    integer: same as --integer\n"
      "    float: same as --float\"\"\")\n"
      "    return parser\n");
  xlang3::test::expect_true(
      result,
      continued_triple_arg.errors.empty(),
      "parser should accept triple-quoted call arguments inside joined lines");

  auto continued_triple_regex = xlang3::parse_source(R"PY(
pattern = (
    r"""[^!"#$%&'()*]"""  # noqa: E501
)
)PY");
  xlang3::test::expect_true(
      result,
      continued_triple_regex.errors.empty(),
      "comment trimming should preserve # and quotes inside a joined raw triple string");

  auto check_joined_literals = [&](const std::string& source,
                                   const std::vector<std::string>& expected,
                                   const std::string& label) {
    auto parsed = xlang3::parse_source(source);
    xlang3::test::expect_true(result, parsed.errors.empty(), label + " should parse");
    if (!parsed.errors.empty()) return;
    auto* assignment = parsed.module.body.size() == 1
        ? dynamic_cast<xlang3::ast::AssignStmt*>(parsed.module.body[0].get()) : nullptr;
    auto* call = assignment ? dynamic_cast<xlang3::ast::CallExpr*>(assignment->value.get()) : nullptr;
    xlang3::test::expect_true(result, call && call->args.size() == expected.size(),
        label + " should retain all call arguments");
    if (!call || call->args.size() != expected.size()) return;
    for (size_t index = 0; index < expected.size(); ++index) {
      auto* literal = dynamic_cast<xlang3::ast::LiteralExpr*>(call->args[index].get());
      xlang3::test::expect_true(result, literal && literal->kind == xlang3::ast::LiteralExpr::Kind::String &&
          literal->text == expected[index], label + " should preserve exact literal contents");
    }
  };
  const std::string joined_body = "alpha\n# interior hash, \"double\" and 'single'\nomega";
  const char joined_quotes[] = {'"', '\''};
  const bool joined_choices[] = {false, true};
  for (char quote : joined_quotes) {
    const std::string delimiter(3, quote);
    for (bool raw_prefix : joined_choices) {
      for (bool suffix_comment : joined_choices) {
        const std::string source = "result = keep(\n    " + std::string(raw_prefix ? "r" : "") +
            delimiter + joined_body + delimiter + "," +
            (suffix_comment ? " # real closing-line comment" : "") + "\n)\n";
        check_joined_literals(source, {joined_body},
            std::string(quote == '"' ? "double" : "single") +
            (raw_prefix ? " raw" : " normal") + (suffix_comment ? " closing comment" : " no comment"));
      }
      const std::string escaped_body = "alpha\n\\" + delimiter + " still literal\nomega";
      const std::string escaped_expected = raw_prefix ? escaped_body :
          "alpha\n" + delimiter + " still literal\nomega";
      check_joined_literals("result = keep(\n    " + std::string(raw_prefix ? "r" : "") + delimiter +
          escaped_body + delimiter + ", # real comment after actual close\n)\n",
          {escaped_expected}, std::string(quote == '"' ? "double" : "single") +
          (raw_prefix ? " raw escaped close" : " normal escaped close"));
    }
  }
  check_joined_literals("result = keep(\n    \"\"\"" + joined_body +
      "\"\"\", \"suffix # remains literal\", # real comment after second string\n)\n",
      {joined_body, "suffix # remains literal"}, "closed triple followed by quoted suffix");
  const std::string malformed_joined[] = {
      "result = keep(\n    \"\"\"alpha\n# literal hash\n)\n",
      "result = keep(\n    \"\"\"alpha\nomega\"\"\", # real comment\n",
      "result = keep(\n    \"\"\"alpha\nomega''', # remains inside unclosed double string\n)\n",
  };
  for (const auto& source : malformed_joined) {
    auto parsed = xlang3::parse_source(source);
    xlang3::test::expect_true(result, !parsed.errors.empty(),
        "comment handling must still reject malformed strings or missing call delimiters");
  }

  auto statements = xlang3::parse_source(
      "class Box[T](Base):\n"
      "    pass\n"
      "\n"
      "def choose[T](x):\n"
      "    if x == 1:\n"
      "        return 1\n"
      "    elif x == 2:\n"
      "        return 2\n"
      "    try:\n"
      "        pass\n"
      "    except Exception as e:\n"
      "        raise e from None\n"
      "    else:\n"
      "        pass\n"
      "    finally:\n"
      "        pass\n"
      "    with (a as x, b as y):\n"
      "        pass\n"
      "    match x:\n"
      "        case 1:\n"
      "            pass\n"
      "        case _:\n"
      "            pass\n"
      "    return 0\n"
      "\n"
      "from . import helper\n"
      "from ..pkg import name\n"
      "from import_helper import *\n");
  xlang3::test::expect_true(result, statements.errors.empty(), "parser should accept Python statement syntax coverage");

  // OR-pattern alternatives must bind the same capture names.
  auto invalid_or_pattern = xlang3::parse_source(
      "match value:\n"
      "    case [x] | [y]:\n"
      "        pass\n");
  xlang3::test::expect_true(
      result,
      !invalid_or_pattern.errors.empty(),
      "parser should reject OR patterns with different capture names");

  // A single pattern cannot capture the same name twice.
  auto duplicate_pattern_capture = xlang3::parse_source(
      "match value:\n"
      "    case [x, x]:\n"
      "        pass\n");
  xlang3::test::expect_true(
      result,
      !duplicate_pattern_capture.errors.empty(),
      "parser should reject duplicate capture names in one pattern");

  return xlang3::test::finish(result);
}
