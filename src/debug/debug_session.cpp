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
#include "xlang3/debug_session.h"

#include "xlang3/parser.h"
#include "xlang3/sema.h"

#include <ostream>
#include <sstream>
#include <utility>

namespace xlang3 {

namespace {

std::string join_errors(const std::vector<std::string>& errors) {
  std::ostringstream out;
  for (size_t i = 0; i < errors.size(); ++i) {
    if (i != 0) {
      out << "\n";
    }
    out << errors[i];
  }
  return out.str();
}

} // namespace

DebugSession::DebugSession(std::ostream& output)
    : runtime_(output),
      interpreter_(runtime_) {
  runtime_.set_debug_enabled(true);
  runtime_.set_debug_pause_on_hit(true);
}

bool DebugSession::load_source(std::string file_name, const std::string& source, std::string& error) {
  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    error = join_errors(parsed.errors);
    return false;
  }
  auto lowered = lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    error = join_errors(lowered.errors);
    return false;
  }

  module_ = std::make_shared<ir::Module>(std::move(lowered.module));
  module_->source_file = std::move(file_name);
  paused_state_.reset();
  status_ = {};
  status_.loaded = true;
  status_.file = module_->source_file;
  return true;
}

bool DebugSession::set_argv(const std::vector<std::string>& argv, std::string& error) {
  return runtime_.set_sys_argv(argv, error);
}

void DebugSession::add_breakpoint(std::string file_name, uint32_t line) {
  runtime_.debug_add_breakpoint(std::move(file_name), line);
}

void DebugSession::clear_breakpoints() {
  runtime_.debug_clear_breakpoints();
}

void DebugSession::request_pause() {
  runtime_.debug_request_pause();
}

bool DebugSession::launch(std::string& error) {
  if (module_ == nullptr) {
    error = "debug session has no loaded module";
    return false;
  }
  if (status_.running && !status_.paused) {
    error = "debug session is already running";
    return false;
  }
  status_.running = true;
  status_.finished = false;
  return consume_result(interpreter_.run(module_), error);
}

bool DebugSession::continue_execution(std::string& error) {
  runtime_.debug_continue();
  return resume_with_current_policy(error);
}

bool DebugSession::step_into(std::string& error) {
  runtime_.debug_step_into();
  return resume_with_current_policy(error);
}

bool DebugSession::step_over(std::string& error) {
  if (!status_.paused) {
    error = "debug session is not paused";
    return false;
  }
  runtime_.debug_step_over(static_cast<size_t>(status_.selected_frame) + 1, status_.line);
  return resume_with_current_policy(error);
}

bool DebugSession::step_out(std::string& error) {
  if (!status_.paused) {
    error = "debug session is not paused";
    return false;
  }
  runtime_.debug_step_out(static_cast<size_t>(status_.selected_frame) + 1);
  return resume_with_current_policy(error);
}

bool DebugSession::resume_with_current_policy(std::string& error) {
  if (!status_.paused || paused_state_ == nullptr) {
    error = "debug session is not paused";
    return false;
  }
  status_.paused = false;
  status_.running = true;
  return consume_result(interpreter_.resume_paused(std::move(paused_state_)), error);
}

bool DebugSession::consume_result(RuntimeResult result, std::string& error) {
  if (!result.errors.empty()) {
    error = join_errors(result.errors);
    status_.running = false;
    return false;
  }
  if (result.paused) {
    paused_state_ = std::move(result.pause_state);
    status_.loaded = module_ != nullptr;
    status_.running = true;
    status_.paused = true;
    status_.finished = false;
    status_.reason = result.pause_reason;
    status_.file = result.pause_file;
    status_.line = result.pause_line;
    status_.selected_frame = result.selected_frame;
    status_.frame = std::move(result.pause_frame);
    return true;
  }
  paused_state_.reset();
  status_.loaded = module_ != nullptr;
  status_.running = false;
  status_.paused = false;
  status_.finished = true;
  status_.reason = RuntimePauseReason::None;
  status_.line = 0;
  status_.selected_frame = 0;
  status_.frame = Value::invalid();
  return true;
}

} // namespace xlang3
