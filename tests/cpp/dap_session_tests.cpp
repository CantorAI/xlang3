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

#include "xlang3/dap_session.h"

#include "json.hpp"

#include <sstream>

namespace {

using Json = nlohmann::json;

Json request(int seq, const char* command, Json args = Json::object()) {
  return Json{
      {"seq", seq},
      {"type", "request"},
      {"command", command},
      {"arguments", std::move(args)},
  };
}

Json single_response(
    xlang3::test::CaseResult& result,
    xlang3::dap::DapSession& session,
    const Json& message,
    const std::string& label) {
  auto responses = session.handle_payload(message.dump());
  xlang3::test::expect_true(result, responses.size() == 1, label + " should produce one response");
  if (responses.empty()) {
    return Json::object();
  }
  return Json::parse(responses[0]);
}

Json response_at(
    xlang3::test::CaseResult& result,
    const std::vector<std::string>& responses,
    size_t index,
    const std::string& label) {
  xlang3::test::expect_true(result, responses.size() > index, label + " should include response");
  if (responses.size() <= index) {
    return Json::object();
  }
  return Json::parse(responses[index]);
}

} // namespace

int main() {
  xlang3::test::CaseResult result;

  {
    const std::string payload = request(1, "initialize").dump();
    std::string input = xlang3::dap::make_framed_message(payload);
    std::string partial = input.substr(0, input.size() - 2);
    xlang3::dap::FramedMessage framed;
    std::string error;
    xlang3::test::expect_true(
        result,
        !xlang3::dap::try_read_framed_message(partial, framed, error),
        "DAP frame parser should wait for complete payload");
    partial += input.substr(input.size() - 2);
    xlang3::test::expect_true(
        result,
        xlang3::dap::try_read_framed_message(partial, framed, error),
        "DAP frame parser should read complete payload");
    xlang3::test::expect_true(result, framed.payload == payload, "DAP frame payload should match original request");
  }

  {
    std::ostringstream output;
    xlang3::dap::DapSession session(output);

    auto initialize_messages = session.handle_payload(request(1, "initialize").dump());
    xlang3::test::expect_true(
        result,
        initialize_messages.size() == 2,
        "initialize should produce response and initialized event");
    Json initialize = response_at(result, initialize_messages, 0, "initialize");
    xlang3::test::expect_true(result, initialize.value("success", false), "initialize should succeed");
    xlang3::test::expect_true(
        result,
        initialize["body"].value("supportsConfigurationDoneRequest", false),
        "initialize should advertise configurationDone");
    if (initialize_messages.size() == 2) {
      Json initialized = Json::parse(initialize_messages[1]);
      xlang3::test::expect_true(result, initialized.value("event", "") == "initialized", "initialize should signal ready");
    }

    const std::string source =
        "x = 1\n"
        "y = x + 2\n"
        "print(y)\n";
    Json launch_args = {{"program", "dap_breakpoint.py"}, {"source", source}};
    Json launch = single_response(result, session, request(2, "launch", launch_args), "launch");
    xlang3::test::expect_true(result, launch.value("success", false), "launch should load source");

    Json set_breakpoints_args = {
        {"source", {{"path", "dap_breakpoint.py"}}},
        {"breakpoints", Json::array({Json{{"line", 3}}})},
    };
    Json set_breakpoints =
        single_response(result, session, request(3, "setBreakpoints", set_breakpoints_args), "setBreakpoints");
    xlang3::test::expect_true(result, set_breakpoints.value("success", false), "setBreakpoints should succeed");
    xlang3::test::expect_true(
        result,
        set_breakpoints["body"]["breakpoints"][0].value("verified", false),
        "setBreakpoints should verify a line breakpoint");

    Json exception_breakpoints =
        single_response(result, session, request(4, "setExceptionBreakpoints"), "setExceptionBreakpoints");
    xlang3::test::expect_true(
        result,
        exception_breakpoints.value("success", false),
        "setExceptionBreakpoints should be accepted for IDE clients");

    auto configuration_done = session.handle_payload(request(5, "configurationDone").dump());
    xlang3::test::expect_true(
        result,
        configuration_done.size() == 2,
        "configurationDone should return response and stopped event at breakpoint");
    if (configuration_done.size() == 2) {
      Json response = Json::parse(configuration_done[0]);
      Json event = Json::parse(configuration_done[1]);
      xlang3::test::expect_true(result, response.value("success", false), "configurationDone response should succeed");
      xlang3::test::expect_true(result, event.value("event", "") == "stopped", "configurationDone should stop");
      xlang3::test::expect_true(
          result,
          event["body"].value("reason", "") == "breakpoint",
          "configurationDone stop reason should be breakpoint");
    }

    Json threads = single_response(result, session, request(6, "threads"), "threads");
    xlang3::test::expect_true(
        result,
        threads["body"]["threads"][0].value("id", 0) == 1,
        "threads should expose the main thread");

    Json stack_trace = single_response(result, session, request(7, "stackTrace"), "stackTrace");
    xlang3::test::expect_true(
        result,
        stack_trace["body"]["stackFrames"][0].value("line", 0) == 3,
        "stackTrace should report breakpoint line");

    Json scopes = single_response(result, session, request(8, "scopes"), "scopes");
    int64_t globals_ref = 0;
    for (const auto& item : scopes["body"]["scopes"]) {
      if (item.value("name", "") == "Globals") {
        globals_ref = item.value("variablesReference", 0);
      }
    }
    xlang3::test::expect_true(result, globals_ref != 0, "scopes should expose globals reference");

    Json variables =
        single_response(result, session, request(9, "variables", Json{{"variablesReference", globals_ref}}), "variables");
    bool found_y = false;
    for (const auto& item : variables["body"]["variables"]) {
      if (item.value("name", "") == "y" && item.value("value", "") == "3" && item.value("type", "") == "int") {
        found_y = true;
      }
    }
    xlang3::test::expect_true(result, found_y, "variables should expose computed global y");

    auto continue_messages = session.handle_payload(request(10, "continue").dump());
    xlang3::test::expect_true(
        result,
        continue_messages.size() == 2,
        "continue should produce response and terminated event");
    if (continue_messages.size() == 2) {
      Json continue_response = Json::parse(continue_messages[0]);
      Json terminated = Json::parse(continue_messages[1]);
      xlang3::test::expect_true(result, continue_response.value("success", false), "continue should finish execution");
      xlang3::test::expect_true(result, terminated.value("event", "") == "terminated", "continue should terminate session");
    }
    xlang3::test::expect_true(result, output.str() == "3\n", "continue should produce program output");

    Json output_event = Json::parse(session.make_output_event(output.str()));
    xlang3::test::expect_true(result, output_event.value("event", "") == "output", "output event should be DAP output");
    xlang3::test::expect_true(
        result,
        output_event["body"].value("output", "") == "3\n",
        "output event should contain captured stdout");
  }

  {
    std::ostringstream output;
    xlang3::dap::DapSession session(output);
    session.handle_payload(request(11, "initialize").dump());

    const std::string source =
        "x = 41\n"
        "print(x + 1)\n";
    Json launch = single_response(
        result,
        session,
        request(12, "launch", Json{{"program", "dap_stop_at_entry.py"}, {"source", source}, {"stopAtEntry", true}}),
        "launch stopAtEntry");
    xlang3::test::expect_true(result, launch.value("success", false), "launch with stopAtEntry should succeed");

    auto configured = session.handle_payload(request(13, "configurationDone").dump());
    xlang3::test::expect_true(result, configured.size() == 2, "stopAtEntry should produce stopped event");
    if (configured.size() == 2) {
      Json stopped = Json::parse(configured[1]);
      xlang3::test::expect_true(
          result,
          stopped.value("event", "") == "stopped" && stopped["body"].value("reason", "") == "pause",
          "stopAtEntry should pause at entry");
    }
  }

  {
    std::ostringstream output;
    xlang3::dap::DapSession session(output);
    session.handle_payload(request(20, "initialize").dump());

    const std::string source =
        "def f(a):\n"
        "    b = a + 1\n"
        "    print(b)\n"
        "\n"
        "f(4)\n";
    xlang3::test::expect_true(
        result,
        single_response(
            result,
            session,
            request(21, "launch", Json{{"program", "dap_locals.py"}, {"source", source}}),
            "launch locals")
            .value("success", false),
        "function local launch should succeed");
    single_response(
        result,
        session,
        request(
            22,
            "setBreakpoints",
            Json{
                {"source", {{"path", "dap_locals.py"}}},
                {"breakpoints", Json::array({Json{{"line", 3}}})},
            }),
        "setBreakpoints locals");

    auto stopped = session.handle_payload(request(23, "configurationDone").dump());
    xlang3::test::expect_true(result, stopped.size() == 2, "function breakpoint should stop");

    Json stack = single_response(result, session, request(24, "stackTrace"), "stackTrace locals");
    xlang3::test::expect_true(
        result,
        stack["body"]["stackFrames"].size() >= 2,
        "stackTrace should expose current frame and caller frame");
    xlang3::test::expect_true(
        result,
        stack["body"]["stackFrames"][0].value("name", "") == "f",
        "top frame should be function f");

    Json scopes_for_f = single_response(result, session, request(25, "scopes", Json{{"frameId", 1}}), "scopes locals");
    int64_t locals_ref = 0;
    for (const auto& item : scopes_for_f["body"]["scopes"]) {
      if (item.value("name", "") == "Locals") {
        locals_ref = item.value("variablesReference", 0);
      }
    }
    xlang3::test::expect_true(result, locals_ref != 0, "scopes should expose locals reference");

    Json locals =
        single_response(result, session, request(26, "variables", Json{{"variablesReference", locals_ref}}), "variables locals");
    bool found_b = false;
    for (const auto& item : locals["body"]["variables"]) {
      if (item.value("name", "") == "b" && item.value("value", "") == "5") {
        found_b = true;
      }
    }
    xlang3::test::expect_true(result, found_b, "locals should expose function local b");
  }

  return xlang3::test::finish(result);
}
