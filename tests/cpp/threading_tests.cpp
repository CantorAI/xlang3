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
        "import threading\n"
        "\n"
        "def work():\n"
        "    print(\"worker\")\n"
        "\n"
        "t = threading.Thread(work)\n"
        "t.start()\n"
        "t.join()\n"
        "print(\"done\")\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "worker\ndone\n", "threading.Thread should start and join");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "import threading\n"
        "\n"
        "def add(a, b):\n"
        "    print(a + b)\n"
        "\n"
        "t = threading.Thread(add, (20, 22))\n"
        "t.start()\n"
        "t.join()\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "42\n", "Thread should pass tuple args");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "import threading\n"
        "\n"
        "def add(a, b):\n"
        "    print(a + b)\n"
        "\n"
        "t = threading.Thread(None, add, None, (30, 12))\n"
        "t.start()\n"
        "t.join()\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "42\n", "Thread should accept CPython positional constructor");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "import _thread\n"
        "\n"
        "lock = _thread.allocate_lock()\n"
        "print(lock.locked())\n"
        "print(lock.acquire())\n"
        "print(lock.locked())\n"
        "lock.release()\n"
        "print(lock.locked())\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "False\nTrue\nTrue\nFalse\n", "_thread lock should acquire and release");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "import _thread\n"
        "\n"
        "lock = _thread.allocate_lock()\n"
        "lock.acquire()\n"
        "\n"
        "def work(value):\n"
        "    print(value)\n"
        "    lock.release()\n"
        "\n"
        "_thread.start_new_thread(work, (7,))\n"
        "lock.acquire()\n"
        "lock.release()\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "7\n", "_thread.start_new_thread should run detached worker");
  }

  return xlang3::test::finish(result);
}
