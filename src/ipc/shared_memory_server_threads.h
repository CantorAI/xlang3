#pragma once
#include <atomic>
#include "shared_memory_transport_internal.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <deque>
#include <thread>
#include <utility>
#include <vector>

namespace xlang3::ipc {
class SharedMemoryServerThreads {
  std::mutex mutex_;
  std::condition_variable available_;
  std::condition_variable drained_;
  struct Task {
    std::function<void()> run;
    std::function<bool()> valid;
  };
  std::deque<Task> tasks_;
  std::vector<std::thread> workers_;
  size_t active_ = 0;
  bool pool_stopping_ = false;

  void start_workers_locked() {
    if (!workers_.empty()) return;
    constexpr size_t kWorkerCount = 8;
    workers_.reserve(kWorkerCount);
    for (size_t i = 0; i < kWorkerCount; ++i) {
      workers_.emplace_back([this] {
        for (;;) {
          Task task;
          {
            std::unique_lock<std::mutex> lock(mutex_);
            available_.wait(lock, [this] { return pool_stopping_ || !tasks_.empty(); });
            if (pool_stopping_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
          }
          try { if (!task.valid || task.valid()) task.run(); } catch (...) {}
          finish();
        }
      });
    }
  }
public:
  std::atomic<bool> stopping{false};
  std::thread listener;
  template<class F> bool launch(F&& function, std::function<bool()> valid = {}) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      start_workers_locked();
      // Caller timeout now releases message slots immediately. Also retire its
      // queued closure: otherwise repeated timeouts can grow this queue while
      // all eight workers are inside long, non-cooperative native handlers.
      for (auto it = tasks_.begin(); it != tasks_.end();) {
        if (it->valid && !it->valid()) { it = tasks_.erase(it); --active_; }
        else ++it;
      }
      if (tasks_.size() >= kSharedSlotCount) return false;
      tasks_.push_back({std::forward<F>(function), std::move(valid)});
      ++active_;
    }
    available_.notify_one();
    return true;
  }
  void finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!--active_) drained_.notify_all();
  }
  void drain() {
    if (listener.joinable()) listener.join();
    std::vector<std::thread> workers;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      drained_.wait(lock, [&] { return active_ == 0; });
      pool_stopping_ = true;
      workers.swap(workers_);
    }
    available_.notify_all();
    for (auto& worker : workers) if (worker.joinable()) worker.join();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pool_stopping_ = false;
    }
  }
};
}
