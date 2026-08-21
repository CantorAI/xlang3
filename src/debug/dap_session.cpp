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
#include "xlang3/dap_session.h"

#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/value.h"

#include "json.hpp"

#include <charconv>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace xlang3::dap {

namespace {

using Json = nlohmann::json;

std::string reason_name(RuntimePauseReason reason) {
  switch (reason) {
    case RuntimePauseReason::Breakpoint:
      return "breakpoint";
    case RuntimePauseReason::Step:
    case RuntimePauseReason::StepOver:
    case RuntimePauseReason::StepOut:
      return "step";
    case RuntimePauseReason::PauseRequest:
      return "pause";
    default:
      return "entry";
  }
}

int64_t request_seq(const Json& request) {
  return request.value("seq", 0);
}

std::string request_command(const Json& request) {
  return request.value("command", "");
}

std::string value_dap_type_name(const Value& value) {
  switch (value.tag) {
    case ValueTag::None:
      return "NoneType";
    case ValueTag::Bool:
      return "bool";
    case ValueTag::Int64:
      return "int";
    case ValueTag::Double:
      return "float";
    case ValueTag::Object:
      if (value.as.obj == nullptr) {
        return "object";
      }
      switch (value.as.obj->kind) {
        case ObjectKind::String:
          return "str";
        case ObjectKind::Bytes:
          return "bytes";
        case ObjectKind::ByteArray:
          return "bytearray";
        case ObjectKind::MemoryView:
          return "memoryview";
        case ObjectKind::Slice:
          return "slice";
        case ObjectKind::Tuple:
          return "tuple";
        case ObjectKind::List:
          return "list";
        case ObjectKind::Dict:
          return "dict";
        case ObjectKind::Set:
          return "set";
        case ObjectKind::Range:
          return "range";
        case ObjectKind::Module:
          return "module";
        case ObjectKind::Function:
          return "function";
        case ObjectKind::NativeFunction:
          return "native_function";
        case ObjectKind::Class:
          return "type";
        case ObjectKind::Instance:
          return "object";
        case ObjectKind::BoundMethod:
          return "method";
        case ObjectKind::Code:
          return "code";
        case ObjectKind::Frame:
          return "frame";
        case ObjectKind::Traceback:
          return "traceback";
        case ObjectKind::File:
          return "file";
        default:
          return "object";
      }
    default:
      return "invalid";
  }
}

bool read_file_text(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return true;
}

Json value_to_dap_variable(const std::string& name, const Value& value) {
  Json item;
  item["name"] = name;
  item["value"] = value_to_string(value);
  item["type"] = value_dap_type_name(value);
  item["variablesReference"] = 0;
  return item;
}

int64_t value_int_or_zero(const Value& value) {
  if (value.tag == ValueTag::Int64) {
    return value.as.i64;
  }
  return 0;
}

bool frame_attr(const Value& frame, const char* name, Value& out) {
  std::string ignored;
  return object_get_attr(frame, name, out, ignored);
}

Value frame_by_id(Value frame, int64_t frame_id) {
  if (frame_id <= 0) {
    return Value::invalid();
  }

  for (int64_t current = 1; current < frame_id; ++current) {
    Value back;
    if (!frame_attr(frame, "f_back", back) || back.tag == ValueTag::None || value_as_frame(back) == nullptr) {
      return Value::invalid();
    }
    frame = std::move(back);
  }
  return frame;
}

std::string frame_code_string_attr(const Value& frame, const char* attr, const std::string& fallback) {
  Value code;
  if (!frame_attr(frame, "f_code", code)) {
    return fallback;
  }
  Value value;
  if (!frame_attr(code, attr, value)) {
    return fallback;
  }
  return value_to_string(value);
}

int64_t frame_line(const Value& frame, uint32_t fallback) {
  Value line;
  if (!frame_attr(frame, "f_lineno", line)) {
    return fallback;
  }
  const int64_t value = value_int_or_zero(line);
  return value == 0 ? fallback : value;
}

Json variables_from_frame_attr(const Value& frame, const char* attr) {
  Json variables = Json::array();
  Value mapping;
  if (!frame_attr(frame, attr, mapping)) {
    return variables;
  }
  if (auto* dict = value_as_dict(mapping)) {
    for (const auto& entry : dict->entries) {
      variables.push_back(value_to_dap_variable(value_to_string(entry.first), entry.second));
    }
  }
  return variables;
}

int64_t make_scope_ref(int64_t frame_id, int64_t scope_kind) {
  return frame_id * 10 + scope_kind;
}

int64_t scope_frame_id(int64_t variables_reference) {
  return variables_reference / 10;
}

int64_t scope_kind(int64_t variables_reference) {
  return variables_reference % 10;
}

} // namespace

bool try_read_framed_message(std::string& buffer, FramedMessage& out, std::string& error) {
  const size_t header_end = buffer.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return false;
  }

  size_t content_length = 0;
  bool found_length = false;
  size_t line_start = 0;
  while (line_start < header_end) {
    const size_t line_end = buffer.find("\r\n", line_start);
    const size_t actual_end = line_end == std::string::npos || line_end > header_end ? header_end : line_end;
    std::string_view line(buffer.data() + line_start, actual_end - line_start);
    static constexpr std::string_view prefix = "Content-Length:";
    if (line.substr(0, prefix.size()) == prefix) {
      line.remove_prefix(prefix.size());
      while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
        line.remove_prefix(1);
      }
      const char* begin = line.data();
      const char* end = line.data() + line.size();
      auto parsed = std::from_chars(begin, end, content_length);
      if (parsed.ec != std::errc()) {
        error = "invalid DAP Content-Length";
        return false;
      }
      found_length = true;
    }
    line_start = actual_end + 2;
  }

  if (!found_length) {
    error = "missing DAP Content-Length";
    return false;
  }

  const size_t payload_start = header_end + 4;
  if (buffer.size() < payload_start + content_length) {
    return false;
  }
  out.payload = buffer.substr(payload_start, content_length);
  buffer.erase(0, payload_start + content_length);
  return true;
}

std::string make_framed_message(const std::string& payload) {
  return "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
}

DapSession::DapSession(std::ostream& program_output) : debug_(program_output) {}

std::vector<std::string> DapSession::handle_framed_input(std::string& input_buffer, std::string& error) {
  std::vector<std::string> responses;
  FramedMessage message;
  while (try_read_framed_message(input_buffer, message, error)) {
    for (const auto& payload : handle_payload(message.payload)) {
      responses.push_back(make_framed_message(payload));
    }
  }
  return responses;
}

std::vector<std::string> DapSession::handle_payload(const std::string& payload) {
  Json request;
  try {
    request = Json::parse(payload);
  } catch (const std::exception& ex) {
    return {make_response(0, "", false, std::string("invalid DAP JSON: ") + ex.what())};
  }

  const int64_t seq = request_seq(request);
  const std::string command = request_command(request);
  const Json args = request.value("arguments", Json::object());

  if (command == "initialize") {
    Json body = {
        {"supportsConfigurationDoneRequest", true},
        {"supportsTerminateRequest", true},
        {"supportsTerminateDebuggee", true},
        {"supportsStepBack", false},
        {"supportsSetVariable", false},
        {"supportsEvaluateForHovers", false},
        {"supportsExceptionOptions", false},
        {"supportsExceptionInfoRequest", false},
        {"supportsDelayedStackTraceLoading", false},
    };
    return {make_response_body(seq, command, true, "", body.dump()), initialized_event()};
  }
  if (command == "launch") {
    std::string program = args.value("program", "");
    std::string source = args.value("source", "");
    if (source.empty() && !program.empty() && !read_file_text(program, source)) {
      return {make_response(seq, command, false, "cannot open launch program")};
    }
    if (program.empty()) {
      program = "<dap>";
    }
    std::string error;
    if (!debug_.load_source(program, source, error)) {
      return {make_response(seq, command, false, error)};
    }
    if (args.contains("args") && args["args"].is_array()) {
      std::vector<std::string> argv;
      argv.push_back(program);
      for (const auto& arg : args["args"]) {
        argv.push_back(arg.get<std::string>());
      }
      if (!debug_.set_argv(argv, error)) {
        return {make_response(seq, command, false, error)};
      }
    }
    return {make_response(seq, command, true, "")};
  }
  if (command == "setBreakpoints") {
    const Json source_obj = args.value("source", Json::object());
    const std::string path = source_obj.value("path", "");
    debug_.clear_breakpoints();
    Json breakpoints = Json::array();
    for (const auto& bp : args.value("breakpoints", Json::array())) {
      const uint32_t line = bp.value("line", 0);
      if (line != 0) {
        debug_.add_breakpoint(path, line);
      }
      breakpoints.push_back({{"verified", line != 0}, {"line", line}});
    }
    Json body = {{"breakpoints", breakpoints}};
    return {make_response_body(seq, command, true, "", body.dump())};
  }
  if (command == "setExceptionBreakpoints") {
    Json body = {{"breakpoints", Json::array()}};
    return {make_response_body(seq, command, true, "", body.dump())};
  }
  if (command == "configurationDone") {
    std::string error;
    if (!debug_.launch(error)) {
      return {make_response(seq, command, false, error)};
    }
    const std::string response = make_response(seq, command, true, "");
    if (debug_.status().paused) {
      return {response, stopped_event()};
    }
    if (debug_.status().finished) {
      return {response, terminated_event()};
    }
    return {response};
  }
  if (command == "continue") {
    std::string error;
    if (!debug_.continue_execution(error)) {
      return {make_response(seq, command, false, error)};
    }
    Json body = {{"allThreadsContinued", true}};
    const std::string response = make_response_body(seq, command, true, "", body.dump());
    if (debug_.status().paused) {
      return {response, stopped_event()};
    }
    if (debug_.status().finished) {
      return {response, terminated_event()};
    }
    return {response};
  }
  if (command == "next" || command == "stepIn" || command == "stepOut") {
    std::string error;
    bool ok = false;
    if (command == "next") {
      ok = debug_.step_over(error);
    } else if (command == "stepIn") {
      ok = debug_.step_into(error);
    } else {
      ok = debug_.step_out(error);
    }
    if (!ok) {
      return {make_response(seq, command, false, error)};
    }
    const std::string response = make_response(seq, command, true, "");
    if (debug_.status().paused) {
      return {response, stopped_event()};
    }
    if (debug_.status().finished) {
      return {response, terminated_event()};
    }
    return {response};
  }
  if (command == "stackTrace") {
    return {make_response_body(seq, command, true, "", stack_trace_body())};
  }
  if (command == "threads") {
    return {make_response_body(seq, command, true, "", threads_body())};
  }
  if (command == "scopes") {
    const int64_t frame_id = args.value("frameId", 1);
    return {make_response_body(seq, command, true, "", scopes_body(frame_id))};
  }
  if (command == "variables") {
    const int64_t ref = args.value("variablesReference", 0);
    return {make_response_body(seq, command, true, "", variables_body(ref))};
  }
  if (command == "disconnect" || command == "terminate") {
    return {make_response(seq, command, true, "")};
  }
  return {make_response(seq, command, false, "unsupported DAP command: " + command)};
}

std::string DapSession::make_response(int64_t request_seq, const std::string& command, bool success, const std::string& message) {
  return make_response_body(request_seq, command, success, message, "{}");
}

std::string DapSession::make_response_body(
    int64_t request_seq,
    const std::string& command,
    bool success,
    const std::string& message,
    const std::string& body_json) {
  Json response = {
      {"seq", next_seq_++},
      {"type", "response"},
      {"request_seq", request_seq},
      {"success", success},
      {"command", command},
  };
  if (!message.empty()) {
    response["message"] = message;
  }
  response["body"] = Json::parse(body_json);
  return response.dump();
}

std::string DapSession::make_event(const std::string& event, const std::string& body_json) {
  Json message = {
      {"seq", next_seq_++},
      {"type", "event"},
      {"event", event},
      {"body", Json::parse(body_json)},
  };
  return message.dump();
}

std::string DapSession::make_output_event(const std::string& output) {
  Json body = {
      {"category", "stdout"},
      {"output", output},
  };
  return make_event("output", body.dump());
}

std::string DapSession::initialized_event() {
  return make_event("initialized", "{}");
}

std::string DapSession::stopped_event() {
  Json body = {
      {"reason", reason_name(debug_.status().reason)},
      {"threadId", 1},
      {"allThreadsStopped", true},
  };
  return make_event("stopped", body.dump());
}

std::string DapSession::terminated_event() {
  return make_event("terminated", "{}");
}

std::string DapSession::status_body() const {
  Json body = {
      {"loaded", debug_.status().loaded},
      {"running", debug_.status().running},
      {"paused", debug_.status().paused},
      {"finished", debug_.status().finished},
      {"file", debug_.status().file},
      {"line", debug_.status().line},
  };
  return body.dump();
}

std::string DapSession::threads_body() const {
  Json threads = Json::array();
  threads.push_back({
      {"id", 1},
      {"name", "MainThread"},
  });
  return Json({{"threads", threads}}).dump();
}

std::string DapSession::stack_trace_body() const {
  Json frames = Json::array();
  if (debug_.status().paused) {
    Value current = debug_.status().frame;
    int64_t frame_id = 1;
    while (value_as_frame(current) != nullptr && frame_id < 128) {
      const std::string file = frame_code_string_attr(current, "co_filename", debug_.status().file);
      Json frame = {
          {"id", frame_id},
          {"name", frame_code_string_attr(current, "co_name", "<module>")},
          {"line", frame_line(current, frame_id == 1 ? debug_.status().line : 0)},
          {"column", 1},
          {"source", {{"path", file}, {"name", file}}},
      };
      frames.push_back(std::move(frame));

      Value back;
      if (!frame_attr(current, "f_back", back) || back.tag == ValueTag::None) {
        break;
      }
      current = std::move(back);
      ++frame_id;
    }
  }
  Json body = {
      {"stackFrames", frames},
      {"totalFrames", frames.size()},
  };
  return body.dump();
}

std::string DapSession::scopes_body(int64_t frame_id) const {
  Json scopes = Json::array();
  if (debug_.status().paused && value_as_frame(frame_by_id(debug_.status().frame, frame_id)) != nullptr) {
    scopes.push_back({
        {"name", "Locals"},
        {"variablesReference", make_scope_ref(frame_id, 1)},
        {"expensive", false},
    });
    scopes.push_back({
        {"name", "Globals"},
        {"variablesReference", make_scope_ref(frame_id, 2)},
        {"expensive", false},
    });
  }
  return Json({{"scopes", scopes}}).dump();
}

std::string DapSession::variables_body(int64_t variables_reference) const {
  Json variables = Json::array();
  if (debug_.status().paused) {
    const Value frame = frame_by_id(debug_.status().frame, scope_frame_id(variables_reference));
    if (value_as_frame(frame) != nullptr) {
      if (scope_kind(variables_reference) == 1) {
        variables = variables_from_frame_attr(frame, "f_locals");
      } else if (scope_kind(variables_reference) == 2) {
        variables = variables_from_frame_attr(frame, "f_globals");
      }
    }
  }
  return Json({{"variables", variables}}).dump();
}

} // namespace xlang3::dap
