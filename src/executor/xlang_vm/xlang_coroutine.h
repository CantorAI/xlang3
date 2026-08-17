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

#include "xlang3/ir.h"
#include "xlang3/runtime.h"

#include <cstdint>
#include <memory>
#include <vector>

/*
XlangVM design note
Author: Shawn Xiong

Purpose:
Python-style async function execution state.

Runtime rule:
Calling an async function creates a coroutine without immediately running its
body. When scheduled, the coroutine attaches to a task and resumes through
XlangVM frames.
*/

namespace xlang3 {

enum class XlangVMCoroutineState : uint8_t {
  Created,
  Running,
  Suspended,
  Completed,
  Failed,
};

struct XlangVMCoroutine {
  XlangVMCoroutineState state = XlangVMCoroutineState::Created;
  std::shared_ptr<const ir::Module> module;
  uint32_t function_id = 0;
  std::vector<Value> closure;
  std::vector<Value> initial_args;
  Value result;
  Value exception;
};

} // namespace xlang3
