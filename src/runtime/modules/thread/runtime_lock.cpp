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
#include "runtime_lock.h"

namespace xlang3 {

XlangRuntimeExecutionMutex& xlang_runtime_execution_lock() {
  static XlangRuntimeExecutionMutex lock;
  return lock;
}

uint32_t& xlang_runtime_execution_depth() {
  static thread_local uint32_t depth = 0;
  return depth;
}

std::function<void()>& xlang_runtime_suspension_callback() {
  static thread_local std::function<void()> callback;
  return callback;
}

bool xlang_runtime_execution_contended() {
  return xlang_runtime_execution_lock().has_waiters();
}

} // namespace xlang3
