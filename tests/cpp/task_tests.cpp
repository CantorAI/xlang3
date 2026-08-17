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
        "import task\n"
        "\n"
        "def add(a, b):\n"
        "    return a + b\n"
        "\n"
        "t = task.spawn(add, (20, 22))\n"
        "print(t.done())\n"
        "print(t.join())\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "False\n42\n" || output == "True\n42\n", "task.spawn should return a joinable result");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "import task\n"
        "\n"
        "def square(x):\n"
        "    return x * x\n"
        "\n"
        "items = [task.spawn(square, (2,)), task.spawn(square, (3,)), task.spawn(square, (4,))]\n"
        "print(task.await_all(items))\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "[4, 9, 16]\n", "task.await_all should preserve list order");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "import asyncio\n"
        "\n"
        "def add(a, b):\n"
        "    return a + b\n"
        "\n"
        "t = asyncio.create_task(add, (5, 7))\n"
        "print(asyncio.run(t))\n"
        "print(asyncio.run(add, (30, 12)))\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "12\n42\n", "asyncio facade should run tasks and callables");
  }

  return xlang3::test::finish(result);
}
