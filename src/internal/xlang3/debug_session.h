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

#include "xlang3/interpreter.h"
#include "xlang3/runtime.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace xlang3 {

struct DebugSessionStatus {
  bool loaded = false;
  bool running = false;
  bool paused = false;
  bool finished = false;
  RuntimePauseReason reason = RuntimePauseReason::None;
  std::string file;
  uint32_t line = 0;
  uint32_t selected_frame = 0;
  Value frame;
};

class DebugSession {
public:
  explicit DebugSession(std::ostream& output);

  Runtime& runtime() { return runtime_; }
  const DebugSessionStatus& status() const { return status_; }

  bool load_source(std::string file_name, const std::string& source, std::string& error);
  bool set_argv(const std::vector<std::string>& argv, std::string& error);

  void add_breakpoint(std::string file_name, uint32_t line);
  void clear_breakpoints();
  void request_pause();

  bool launch(std::string& error);
  bool continue_execution(std::string& error);
  bool step_into(std::string& error);
  bool step_over(std::string& error);
  bool step_out(std::string& error);

private:
  bool consume_result(RuntimeResult result, std::string& error);
  bool resume_with_current_policy(std::string& error);

  Runtime runtime_;
  Interpreter interpreter_;
  std::shared_ptr<ir::Module> module_;
  std::shared_ptr<RuntimeDebugPauseState> paused_state_;
  DebugSessionStatus status_;
};

} // namespace xlang3
