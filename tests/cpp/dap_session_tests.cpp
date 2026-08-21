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

    Json initialize = single_response(result, session, request(1, "initialize"), "initialize");
    xlang3::test::expect_true(result, initialize.value("success", false), "initialize should succeed");
    xlang3::test::expect_true(
        result,
        initialize["body"].value("supportsConfigurationDoneRequest", false),
        "initialize should advertise configurationDone");

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

    auto configuration_done = session.handle_payload(request(4, "configurationDone").dump());
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

    Json stack_trace = single_response(result, session, request(5, "stackTrace"), "stackTrace");
    xlang3::test::expect_true(
        result,
        stack_trace["body"]["stackFrames"][0].value("line", 0) == 3,
        "stackTrace should report breakpoint line");

    Json scopes = single_response(result, session, request(6, "scopes"), "scopes");
    const int64_t globals_ref = scopes["body"]["scopes"][0].value("variablesReference", 0);
    xlang3::test::expect_true(result, globals_ref != 0, "scopes should expose globals reference");

    Json variables =
        single_response(result, session, request(7, "variables", Json{{"variablesReference", globals_ref}}), "variables");
    bool found_y = false;
    for (const auto& item : variables["body"]["variables"]) {
      if (item.value("name", "") == "y" && item.value("value", "") == "3" && item.value("type", "") == "int") {
        found_y = true;
      }
    }
    xlang3::test::expect_true(result, found_y, "variables should expose computed global y");

    auto continue_messages = session.handle_payload(request(8, "continue").dump());
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

  return xlang3::test::finish(result);
}
