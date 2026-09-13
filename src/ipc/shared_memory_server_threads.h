#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace xlang3::ipc {
class SharedMemoryServerThreads {
  std::mutex mutex_;
  std::condition_variable available_;
  std::condition_variable drained_;
  std::queue<std::function<void()>> tasks_;
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
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(mutex_);
            available_.wait(lock, [this] { return pool_stopping_ || !tasks_.empty(); });
            if (pool_stopping_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
          }
          try { task(); } catch (...) {}
          finish();
        }
      });
    }
  }
public:
  std::atomic<bool> stopping{false};
  std::thread listener;
  template<class F> void launch(F&& function) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      start_workers_locked();
      tasks_.emplace(std::forward<F>(function));
      ++active_;
    }
    available_.notify_one();
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
