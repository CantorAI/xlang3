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

#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

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

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "total = 0\n"
        "for x in range(5):\n"
        "    total = total + x\n"
        "print(total)\n"
        "print([1, 2, 3])\n"
        "print([x + 1 for x in range(3)])\n"
        "print(len([10, 20, 30]))\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "10\n[1, 2, 3]\n[1, 2, 3]\n3\n",
                              "range, for, lists, list comprehension, and len should work");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "x = 99\n"
        "print([x for x in range(2)])\n"
        "print(x)\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "[0, 1]\n99\n",
                              "list comprehension target should not leak to surrounding scope");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "items = [10, 20, 30]\n"
        "print(items[0])\n"
        "print(items[-1])\n"
        "print((1, 2, 3)[1])\n"
        "print(\"abc\"[2])\n"
        "total = 0\n"
        "for x in items:\n"
        "    total = total + x\n"
        "print(total)\n"
        "print([x for x in items if x > 10])\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "10\n30\n2\nc\n60\n[20, 30]\n",
                              "subscript, sequence iteration, and filtered list comprehension should work");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "d = {\"a\": 1, \"b\": 2, \"a\": 3}\n"
        "print(d[\"a\"])\n"
        "d[\"c\"] = 4\n"
        "print(d[\"c\"])\n"
        "print(len(d))\n"
        "total = 0\n"
        "for k in d:\n"
        "    total = total + d[k]\n"
        "print(total)\n"
        "s = {1, 2, 2, 3}\n"
        "print(len(s))\n"
        "sum = 0\n"
        "for x in s:\n"
        "    sum = sum + x\n"
        "print(sum)\n"
        "items = [1, 2]\n"
        "items[1] = 5\n"
        "print(items)\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "3\n4\n3\n9\n3\n6\n[1, 5]\n",
                              "dict, set, iteration, len, and item assignment should work");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "import _builtins\n"
        "_builtins.print(\"hello\")\n"
        "_builtins.print(_builtins.len([1, 2, 3]))\n"
        "p = _builtins.print\n"
        "_builtins.answer = 42\n"
        "p(_builtins.answer)\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "hello\n3\n42\n",
                              "native module import and module attribute access should work");
  }

  {
    std::string output;
    auto run = xlang3::test::run_source(
        "import math\n"
        "print(math.sqrt(9))\n"
        "print(math.cos(0))\n"
        "print(math.pi > 3)\n",
        output);
    result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
    result.ok = result.ok && run.ok;
    xlang3::test::expect_true(result, output == "3.0\n1.0\nTrue\n",
                              "math native module should import and call native functions");
  }

  {
    const std::string source =
        "x = 1\n"
        "y = x + 2\n"
        "print(y)\n";
    auto parsed = xlang3::parse_source(source);
    result.errors.insert(result.errors.end(), parsed.errors.begin(), parsed.errors.end());
    xlang3::test::expect_true(result, parsed.errors.empty(), "debug pause source should parse");
    if (parsed.errors.empty()) {
      auto lowered = xlang3::lower_to_ir(parsed.module);
      result.errors.insert(result.errors.end(), lowered.errors.begin(), lowered.errors.end());
      xlang3::test::expect_true(result, lowered.errors.empty(), "debug pause source should lower");
      if (lowered.errors.empty()) {
        auto module = std::make_shared<xlang3::ir::Module>(std::move(lowered.module));
        module->source_file = "debug_pause_resume.py";
        std::ostringstream out;
        xlang3::Runtime runtime(out);
        runtime.set_debug_enabled(true);
        runtime.set_debug_pause_on_hit(true);
        runtime.debug_add_breakpoint("debug_pause_resume.py", 3);
        xlang3::Interpreter interpreter(runtime);
        auto paused = interpreter.run(module);
        xlang3::test::expect_true(result, paused.errors.empty(), "debug pause should not produce errors");
        xlang3::test::expect_true(result, paused.paused, "debug breakpoint should pause execution");
        xlang3::test::expect_true(
            result,
            paused.pause_reason == xlang3::RuntimePauseReason::Breakpoint,
            "debug pause reason should be breakpoint");
        xlang3::test::expect_true(result, paused.pause_line == 3, "debug pause should report source line");
        xlang3::test::expect_true(result, out.str().empty(), "debug pause should happen before print executes");
        xlang3::Value paused_globals;
        std::string attr_error;
        xlang3::test::expect_true(
            result,
            xlang3::object_get_attr(paused.pause_frame, "f_globals", paused_globals, attr_error),
            "debug pause frame should expose globals");
        xlang3::Value y_value;
        attr_error.clear();
        xlang3::test::expect_true(
            result,
            xlang3::sequence_get_item(paused_globals, xlang3::Value::string("y"), y_value, attr_error),
            "debug pause globals should include assigned y");
        xlang3::test::expect_true(
            result,
            y_value.tag == xlang3::ValueTag::Int64 && y_value.as.i64 == 3,
            "debug pause globals should preserve y value");
        runtime.debug_continue();
        auto resumed = interpreter.resume_paused(paused.pause_state);
        xlang3::test::expect_true(result, resumed.errors.empty(), "debug resume should not produce errors");
        xlang3::test::expect_true(result, !resumed.paused, "debug continue should run to completion");
        xlang3::test::expect_true(result, out.str() == "3\n", "debug resume should continue paused frame stack");
      }
    }
  }

  {
    const std::string source =
        "def inner():\n"
        "    a = 10\n"
        "    return a\n"
        "\n"
        "def outer():\n"
        "    x = 1\n"
        "    y = inner()\n"
        "    z = y + 1\n"
        "    print(z)\n"
        "\n"
        "outer()\n";
    auto parsed = xlang3::parse_source(source);
    result.errors.insert(result.errors.end(), parsed.errors.begin(), parsed.errors.end());
    xlang3::test::expect_true(result, parsed.errors.empty(), "debug step over source should parse");
    if (parsed.errors.empty()) {
      auto lowered = xlang3::lower_to_ir(parsed.module);
      result.errors.insert(result.errors.end(), lowered.errors.begin(), lowered.errors.end());
      xlang3::test::expect_true(result, lowered.errors.empty(), "debug step over source should lower");
      if (lowered.errors.empty()) {
        auto module = std::make_shared<xlang3::ir::Module>(std::move(lowered.module));
        module->source_file = "debug_step_over.py";
        std::ostringstream out;
        xlang3::Runtime runtime(out);
        runtime.set_debug_enabled(true);
        runtime.set_debug_pause_on_hit(true);
        runtime.debug_add_breakpoint("debug_step_over.py", 7);
        xlang3::Interpreter interpreter(runtime);
        auto paused = interpreter.run(module);
        xlang3::test::expect_true(result, paused.paused && paused.pause_line == 7, "debug should pause before call");
        runtime.debug_clear_breakpoints();
        runtime.debug_step_over(static_cast<size_t>(paused.selected_frame) + 1, paused.pause_line);
        auto stepped = interpreter.resume_paused(paused.pause_state);
        xlang3::test::expect_true(result, stepped.errors.empty(), "debug step over should not produce errors");
        xlang3::test::expect_true(result, stepped.paused, "debug step over should pause again");
        xlang3::test::expect_true(
            result,
            stepped.pause_reason == xlang3::RuntimePauseReason::StepOver,
            "debug step over pause reason should be StepOver");
        xlang3::test::expect_true(result, stepped.pause_line == 8, "debug step over should stop after call line");
        xlang3::test::expect_true(result, out.str().empty(), "debug step over should pause before print");
        runtime.debug_continue();
        auto resumed = interpreter.resume_paused(stepped.pause_state);
        xlang3::test::expect_true(result, resumed.errors.empty() && !resumed.paused, "debug step over continue should finish");
        xlang3::test::expect_true(result, out.str() == "11\n", "debug step over resume should print result");
      }
    }
  }

  {
    const std::string source =
        "def inner():\n"
        "    a = 10\n"
        "    return a\n"
        "\n"
        "def outer():\n"
        "    y = inner()\n"
        "    z = y + 1\n"
        "    print(z)\n"
        "\n"
        "outer()\n";
    auto parsed = xlang3::parse_source(source);
    result.errors.insert(result.errors.end(), parsed.errors.begin(), parsed.errors.end());
    xlang3::test::expect_true(result, parsed.errors.empty(), "debug step out source should parse");
    if (parsed.errors.empty()) {
      auto lowered = xlang3::lower_to_ir(parsed.module);
      result.errors.insert(result.errors.end(), lowered.errors.begin(), lowered.errors.end());
      xlang3::test::expect_true(result, lowered.errors.empty(), "debug step out source should lower");
      if (lowered.errors.empty()) {
        auto module = std::make_shared<xlang3::ir::Module>(std::move(lowered.module));
        module->source_file = "debug_step_out.py";
        std::ostringstream out;
        xlang3::Runtime runtime(out);
        runtime.set_debug_enabled(true);
        runtime.set_debug_pause_on_hit(true);
        runtime.debug_add_breakpoint("debug_step_out.py", 2);
        xlang3::Interpreter interpreter(runtime);
        auto paused = interpreter.run(module);
        xlang3::test::expect_true(result, paused.paused && paused.pause_line == 2, "debug should pause inside callee");
        runtime.debug_clear_breakpoints();
        runtime.debug_step_out(static_cast<size_t>(paused.selected_frame) + 1);
        auto stepped = interpreter.resume_paused(paused.pause_state);
        xlang3::test::expect_true(result, stepped.errors.empty(), "debug step out should not produce errors");
        xlang3::test::expect_true(result, stepped.paused, "debug step out should pause in caller");
        xlang3::test::expect_true(
            result,
            stepped.pause_reason == xlang3::RuntimePauseReason::StepOut,
            "debug step out pause reason should be StepOut");
        xlang3::test::expect_true(result, stepped.pause_line == 7, "debug step out should stop after callee returns");
        runtime.debug_continue();
        auto resumed = interpreter.resume_paused(stepped.pause_state);
        xlang3::test::expect_true(result, resumed.errors.empty() && !resumed.paused, "debug step out continue should finish");
        xlang3::test::expect_true(result, out.str() == "11\n", "debug step out resume should print result");
      }
    }
  }

  {
    const std::string source =
        "x = 1\n"
        "y = x + 1\n"
        "print(y)\n";
    auto parsed = xlang3::parse_source(source);
    result.errors.insert(result.errors.end(), parsed.errors.begin(), parsed.errors.end());
    xlang3::test::expect_true(result, parsed.errors.empty(), "debug pause request source should parse");
    if (parsed.errors.empty()) {
      auto lowered = xlang3::lower_to_ir(parsed.module);
      result.errors.insert(result.errors.end(), lowered.errors.begin(), lowered.errors.end());
      xlang3::test::expect_true(result, lowered.errors.empty(), "debug pause request source should lower");
      if (lowered.errors.empty()) {
        auto module = std::make_shared<xlang3::ir::Module>(std::move(lowered.module));
        module->source_file = "debug_pause_request.py";
        std::ostringstream out;
        xlang3::Runtime runtime(out);
        runtime.set_debug_enabled(true);
        runtime.set_debug_pause_on_hit(true);
        runtime.debug_request_pause();
        xlang3::Interpreter interpreter(runtime);
        auto paused = interpreter.run(module);
        xlang3::test::expect_true(result, paused.errors.empty(), "debug pause request should not produce errors");
        xlang3::test::expect_true(result, paused.paused, "debug pause request should pause");
        xlang3::test::expect_true(
            result,
            paused.pause_reason == xlang3::RuntimePauseReason::PauseRequest,
            "debug pause request reason should be PauseRequest");
        xlang3::test::expect_true(result, paused.pause_line == 1, "debug pause request should stop at next line");
        runtime.debug_continue();
        auto resumed = interpreter.resume_paused(paused.pause_state);
        xlang3::test::expect_true(result, resumed.errors.empty() && !resumed.paused, "debug pause request continue should finish");
        xlang3::test::expect_true(result, out.str() == "2\n", "debug pause request resume should print result");
      }
    }
  }

  return xlang3::test::finish(result);
}
