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

#include <condition_variable>
#include <atomic>
#include <functional>
#include <mutex>
#include <cstdint>
#include <thread>

namespace xlang3 {

#ifndef XLANG3_VM_GLOBAL_LOCK
#define XLANG3_VM_GLOBAL_LOCK 1
#endif

class XlangRuntimeExecutionMutex {
public:
  void lock() {
    const auto current = std::this_thread::get_id();
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (owner_ == current) {
      ++depth_;
      return;
    }
    const uint64_t ticket = next_ticket_++;
    const bool waiting = owner_ != std::thread::id() || ticket != serving_ticket_;
    if (waiting) waiters_.fetch_add(1, std::memory_order_relaxed);
    available_.wait(lock, [&]() { return owner_ == std::thread::id() && ticket == serving_ticket_; });
    if (waiting) waiters_.fetch_sub(1, std::memory_order_relaxed);
    owner_ = current;
    depth_ = 1;
  }

  bool has_waiters() const {
    return waiters_.load(std::memory_order_relaxed) != 0;
  }

  void unlock() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (owner_ != std::this_thread::get_id() || depth_ == 0) {
      return;
    }
    if (--depth_ != 0) {
      return;
    }
    owner_ = std::thread::id();
    ++serving_ticket_;
    available_.notify_all();
  }

private:
  std::mutex state_mutex_;
  std::condition_variable available_;
  std::thread::id owner_;
  uint32_t depth_ = 0;
  uint64_t next_ticket_ = 0;
  uint64_t serving_ticket_ = 0;
  std::atomic_uint32_t waiters_{0};
};

XlangRuntimeExecutionMutex& xlang_runtime_execution_lock();
uint32_t& xlang_runtime_execution_depth();
std::function<void()>& xlang_runtime_suspension_callback();
bool xlang_runtime_execution_contended();

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
