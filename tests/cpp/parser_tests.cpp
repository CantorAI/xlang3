#include "test_harness.h"

int main() {
  xlang3::test::CaseResult result;
  const char* source =
      "def main():\n"
      "    x = 1 + 2 * 3\n"
      "    if x > 5:\n"
      "        print(x)\n"
      "    else:\n"
      "        print(0)\n"
      "\n"
      "main()\n";

  auto parsed = xlang3::parse_source(source);
  xlang3::test::expect_true(result, parsed.errors.empty(), "parser should accept core function/if syntax");
  xlang3::test::expect_true(result, parsed.module.body.size() == 2, "module should contain def and call");
  return xlang3::test::finish(result);
}
