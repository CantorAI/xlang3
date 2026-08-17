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

#include "xlang_thread.h"
#include "xlang3/runtime.h"

#include <cstdint>

/*
XlangVM design note
Author: Shawn Xiong

Purpose:
Scheduler handle for resumable VM execution.

Runtime rule:
A task tracks completion, cancellation, result, and exception state. It may run
cooperatively by the XlangVM scheduler or by a native task pool. It is not a
physical OS thread.
*/

namespace xlang3 {

enum class XlangVMTaskState : uint8_t {
  Pending,
  Running,
  Waiting,
  Completed,
  Cancelled,
  Failed,
};

struct XlangVMTask {
  uint64_t id = 0;
  XlangVMThread* thread = nullptr;
  XlangVMTaskState state = XlangVMTaskState::Pending;
  Value result;
  Value exception;
};

} // namespace xlang3
