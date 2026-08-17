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

#include "xlang_coroutine.h"
#include "xlang_task.h"
#include "xlang_thread.h"
#include "xlang3/runtime.h"

#include <cstdint>
#include <memory>
#include <vector>

/*
XlangVM design note
Author: Shawn Xiong

Purpose:
Execution owner above the interpreter loop.

Ownership rule:
Runtime owns objects, modules, and packages. XlangVM owns logical VMThreads and
will own scheduling, command/RPC integration, and step/resume control. The
current interpreter still runs mostly blocking, but its frame type now belongs
here.
*/

namespace xlang3 {

class XlangVM {
public:
  explicit XlangVM(Runtime& runtime) : runtime_(runtime) {}

  Runtime& runtime() {
    return runtime_;
  }

  XlangVMThread& create_thread() {
    threads_.push_back(std::make_unique<XlangVMThread>(next_thread_id_++));
    return *threads_.back();
  }

  const std::vector<std::unique_ptr<XlangVMThread>>& threads() const {
    return threads_;
  }

private:
  Runtime& runtime_;
  uint64_t next_thread_id_ = 1;
  std::vector<std::unique_ptr<XlangVMThread>> threads_;
};

} // namespace xlang3
