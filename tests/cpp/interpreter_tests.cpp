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

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "def outer():\n"
        "    x = 10\n"
        "    def inner():\n"
        "        return x\n"
        "    return inner()\n"
        "\n"
        "print(outer())\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "10\n", "inner function should capture outer local");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "def make_reader():\n"
        "    x = 33\n"
        "    def read():\n"
        "        return x\n"
        "    return read\n"
        "\n"
        "reader = make_reader()\n"
        "print(reader())\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "33\n", "returned closure should keep captured cell alive");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "def make_counter():\n"
        "    x = 0\n"
        "    def inc():\n"
        "        nonlocal x\n"
        "        x = x + 1\n"
        "        return x\n"
        "    return inc\n"
        "\n"
        "counter = make_counter()\n"
        "print(counter())\n"
        "print(counter())\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "1\n2\n", "nonlocal assignment should update captured cell");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "p = print\n"
        "p(99)\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "99\n", "builtin print should be a callable value");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "def pick(a, b):\n"
        "    print(a)\n"
        "    print(b)\n"
        "\n"
        "print((1, 2))\n"
        "print((3,))\n"
        "print(())\n"
        "pick(4, 5)\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "(1, 2)\n(3,)\n()\n4\n5\n",
                              "tuple literals and call arg separators should work");
  }

  return xlang3::test::finish(result);
}
