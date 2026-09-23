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

#include <atomic>
#include <functional>
#include <mutex>
#include <cstdint>
#include <thread>

namespace xlang3 {

void xlang_runtime_execution_acquired();

#ifndef XLANG3_VM_GLOBAL_LOCK
#define XLANG3_VM_GLOBAL_LOCK 1
#endif

class XlangRuntimeExecutionMutex {
public:
  void lock() {
    const uintptr_t current = current_thread_token();
    if (owner_.load(std::memory_order_acquire) == current) {
      ++depth_;
      return;
    }
    // Do not use a userspace ticket queue here. Native/LRPC worker threads can
    // be cancelled while waiting during peer teardown. An abandoned ticket
    // leaves owner_ clear but permanently prevents every later ticket from
    // being served. The host mutex has no external queue state to orphan.
    waiters_.fetch_add(1, std::memory_order_relaxed);
    mutex_.lock();
    waiters_.fetch_sub(1, std::memory_order_relaxed);
    owner_.store(current, std::memory_order_release);
    depth_ = 1;
    xlang_runtime_execution_acquired();
  }

  bool has_waiters() const {
    return waiters_.load(std::memory_order_relaxed) != 0;
  }

  void unlock() {
    if (owner_.load(std::memory_order_acquire) != current_thread_token() || depth_ == 0) {
      return;
    }
    if (--depth_ != 0) {
      return;
    }
    owner_.store(0, std::memory_order_release);
    mutex_.unlock();
  }

private:
  static uintptr_t current_thread_token() {
    static thread_local const char token = 0;
    return reinterpret_cast<uintptr_t>(&token);
  }

  std::mutex mutex_;
  std::atomic_uintptr_t owner_{0};
  uint32_t depth_ = 0;
  std::atomic_uint32_t waiters_{0};
};

XlangRuntimeExecutionMutex& xlang_runtime_execution_lock();
uint32_t& xlang_runtime_execution_depth();
std::function<void()>& xlang_runtime_suspension_callback();
bool xlang_runtime_execution_contended();
bool xlang_runtime_execution_should_yield();
double xlang_runtime_switch_interval();
void xlang_runtime_set_switch_interval(double seconds);

class XlangRuntimeSuspensionCallbackGuard {
public:
  explicit XlangRuntimeSuspensionCallbackGuard(std::function<void()> callback)
      : previous_(std::move(xlang_runtime_suspension_callback())) {
    xlang_runtime_suspension_callback() = std::move(callback);
  }
  ~XlangRuntimeSuspensionCallbackGuard() {
    xlang_runtime_suspension_callback() = std::move(previous_);
  }
  XlangRuntimeSuspensionCallbackGuard(const XlangRuntimeSuspensionCallbackGuard&) = delete;
  XlangRuntimeSuspensionCallbackGuard& operator=(const XlangRuntimeSuspensionCallbackGuard&) = delete;
private:
  std::function<void()> previous_;
};

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
    if (auto& callback = xlang_runtime_suspension_callback(); callback) callback();
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

// Native extension callbacks may block or re-enter the runtime from another
// thread. VM calls already release the lock; direct embedding calls may not.
class XlangRuntimeExecutionSuspension {
public:
  XlangRuntimeExecutionSuspension() {
#if XLANG3_VM_GLOBAL_LOCK
    auto& depth = xlang_runtime_execution_depth();
    suspended_depth_ = depth;
    if (suspended_depth_ != 0) {
      if (auto& callback = xlang_runtime_suspension_callback(); callback) callback();
      depth = 0;
      xlang_runtime_execution_lock().unlock();
    }
#endif
  }
  ~XlangRuntimeExecutionSuspension() {
#if XLANG3_VM_GLOBAL_LOCK
    if (suspended_depth_ != 0) {
      xlang_runtime_execution_lock().lock();
      xlang_runtime_execution_depth() = suspended_depth_;
    }
#endif
  }
  XlangRuntimeExecutionSuspension(const XlangRuntimeExecutionSuspension&) = delete;
  XlangRuntimeExecutionSuspension& operator=(const XlangRuntimeExecutionSuspension&) = delete;
private:
  uint32_t suspended_depth_ = 0;
};

} // namespace xlang3
