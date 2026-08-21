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

#include "xlang3/debug_session.h"

int main() {
  xlang3::test::CaseResult result;

  {
    std::ostringstream output;
    xlang3::DebugSession session(output);
    std::string error;
    const std::string source =
        "def inner():\n"
        "    return 10\n"
        "\n"
        "def outer():\n"
        "    y = inner()\n"
        "    print(y + 1)\n"
        "\n"
        "outer()\n";
    xlang3::test::expect_true(
        result,
        session.load_source("debug_session_step.py", source, error),
        "debug session should load source");
    session.add_breakpoint("debug_session_step.py", 5);
    xlang3::test::expect_true(result, session.launch(error), "debug session launch should pause");
    xlang3::test::expect_true(result, session.status().paused, "debug session should be paused");
    xlang3::test::expect_true(
        result,
        session.status().reason == xlang3::RuntimePauseReason::Breakpoint,
        "debug session first pause should be breakpoint");
    xlang3::test::expect_true(result, session.status().line == 5, "debug session should pause at call line");
    xlang3::test::expect_true(result, output.str().empty(), "debug session should pause before output");

    session.clear_breakpoints();
    xlang3::test::expect_true(result, session.step_over(error), "debug session step over should pause");
    xlang3::test::expect_true(
        result,
        session.status().reason == xlang3::RuntimePauseReason::StepOver,
        "debug session step over reason should match");
    xlang3::test::expect_true(result, session.status().line == 6, "debug session step over should stop after call");
    xlang3::test::expect_true(result, session.continue_execution(error), "debug session continue should finish");
    xlang3::test::expect_true(result, session.status().finished, "debug session should finish after continue");
    xlang3::test::expect_true(result, output.str() == "11\n", "debug session should produce final output");
  }

  {
    std::ostringstream output;
    xlang3::DebugSession session(output);
    std::string error;
    const std::string source =
        "x = 1\n"
        "y = x + 1\n"
        "print(y)\n";
    xlang3::test::expect_true(
        result,
        session.load_source("debug_session_pause.py", source, error),
        "debug session pause request source should load");
    session.request_pause();
    xlang3::test::expect_true(result, session.launch(error), "debug session pause request should launch");
    xlang3::test::expect_true(result, session.status().paused, "debug session pause request should pause");
    xlang3::test::expect_true(
        result,
        session.status().reason == xlang3::RuntimePauseReason::PauseRequest,
        "debug session pause request reason should match");
    xlang3::test::expect_true(result, session.status().line == 1, "debug session pause request should stop at first line");
    xlang3::test::expect_true(result, session.continue_execution(error), "debug session pause request continue should finish");
    xlang3::test::expect_true(result, output.str() == "2\n", "debug session pause request should resume output");
  }

  return xlang3::test::finish(result);
}
