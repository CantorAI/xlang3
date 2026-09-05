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

#include <mutex>
#include <cstdint>

namespace xlang3 {

#ifndef XLANG3_VM_GLOBAL_LOCK
#define XLANG3_VM_GLOBAL_LOCK 1
#endif

std::recursive_mutex& xlang_runtime_execution_lock();
uint32_t& xlang_runtime_execution_depth();

class XlangRuntimeExecutionGuard {
public:
  XlangRuntimeExecutionGuard() {
#if XLANG3_VM_GLOBAL_LOCK
    auto& depth = xlang_runtime_execution_depth();
    if (depth == 0) xlang_runtime_execution_lock().lock();
    ++depth;
#endif
  }

  ~XlangRuntimeExecutionGuard() {
#if XLANG3_VM_GLOBAL_LOCK
    if (!held_) lock();
    auto& depth = xlang_runtime_execution_depth();
    if (--depth == 0) xlang_runtime_execution_lock().unlock();
#endif
  }

  void lock() {
#if XLANG3_VM_GLOBAL_LOCK
    if (held_) return;
    xlang_runtime_execution_lock().lock();
    xlang_runtime_execution_depth() = suspended_depth_;
    suspended_depth_ = 0;
    held_ = true;
#endif
  }

  void unlock() {
#if XLANG3_VM_GLOBAL_LOCK
    if (!held_) return;
    // Blocking native calls must release embedding and nested VM guards together.
    suspended_depth_ = xlang_runtime_execution_depth();
    xlang_runtime_execution_depth() = 0;
    held_ = false;
    xlang_runtime_execution_lock().unlock();
#endif
  }
  XlangRuntimeExecutionGuard(const XlangRuntimeExecutionGuard&) = delete;
  XlangRuntimeExecutionGuard& operator=(const XlangRuntimeExecutionGuard&) = delete;
private:
  uint32_t suspended_depth_ = 0;
  bool held_ = true;
};

} // namespace xlang3
