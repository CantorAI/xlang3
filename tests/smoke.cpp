#include "test_harness.h"

int main() {
  const char* source =
      "def add(a, b):\n"
      "    return a + b\n"
      "\n"
      "x = add(20, 22)\n";

  std::string output;
  auto result = xlang3::test::run_source(source, output);
  xlang3::test::expect_true(result, output.empty(), "smoke program should not print");
  return xlang3::test::finish(result);
}
