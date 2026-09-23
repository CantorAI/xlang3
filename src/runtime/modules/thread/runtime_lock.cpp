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

#include <chrono>

namespace xlang3 {

namespace {

std::atomic<int64_t> g_switch_interval_nanoseconds{5000000};
thread_local std::chrono::steady_clock::time_point g_execution_acquired_at =
    std::chrono::steady_clock::now();

} // namespace

void xlang_runtime_execution_acquired() {
  g_execution_acquired_at = std::chrono::steady_clock::now();
}

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

bool xlang_runtime_execution_should_yield() {
  if (!xlang_runtime_execution_contended()) return false;
  const auto held_for = std::chrono::steady_clock::now() - g_execution_acquired_at;
  return held_for >= std::chrono::nanoseconds(
      g_switch_interval_nanoseconds.load(std::memory_order_relaxed));
}

double xlang_runtime_switch_interval() {
  return static_cast<double>(
      g_switch_interval_nanoseconds.load(std::memory_order_relaxed)) / 1.0e9;
}

void xlang_runtime_set_switch_interval(double seconds) {
  const auto nanoseconds = static_cast<int64_t>(seconds * 1.0e9);
  g_switch_interval_nanoseconds.store(
      nanoseconds > 0 ? nanoseconds : 1,
      std::memory_order_relaxed);
}

} // namespace xlang3
