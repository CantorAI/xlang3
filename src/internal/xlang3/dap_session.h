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
#pragma once

#include "xlang3/debug_session.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace xlang3::dap {

struct FramedMessage {
  std::string payload;
};

bool try_read_framed_message(std::string& buffer, FramedMessage& out, std::string& error);
std::string make_framed_message(const std::string& payload);

class DapSession {
public:
  explicit DapSession(std::ostream& program_output);

  std::vector<std::string> handle_framed_input(std::string& input_buffer, std::string& error);
  std::vector<std::string> handle_payload(const std::string& payload);
  std::string make_output_event(const std::string& output);

private:
  std::string make_response(int64_t request_seq, const std::string& command, bool success, const std::string& message);
  std::string make_response_body(
      int64_t request_seq,
      const std::string& command,
      bool success,
      const std::string& message,
      const std::string& body_json);
  std::string make_event(const std::string& event, const std::string& body_json);
  std::string stopped_event();
  std::string terminated_event();
  std::string status_body() const;
  std::string stack_trace_body() const;
  std::string scopes_body() const;
  std::string variables_body(int64_t variables_reference) const;

  DebugSession debug_;
  int64_t next_seq_ = 1;
  int64_t globals_variables_reference_ = 1;
};

} // namespace xlang3::dap
