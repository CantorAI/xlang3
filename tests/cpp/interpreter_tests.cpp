#include "test_harness.h"

int main() {
  xlang3::test::CaseResult result;

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "def add(a, b):\n"
        "    return a + b\n"
        "\n"
        "print(add(20, 22))\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "42\n", "function call should print 42");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "def main():\n"
        "    total = 0\n"
        "    i = 0\n"
        "    while i < 5:\n"
        "        total = total + i\n"
        "        i = i + 1\n"
        "    print(total)\n"
        "\n"
        "main()\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "10\n", "while loop should print 10");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "def outer():\n"
        "    def inner():\n"
        "        return 7\n"
        "    return inner()\n"
        "\n"
        "print(outer())\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "7\n", "nested function without closure should print 7");
  }

  return xlang3::test::finish(result);
}
