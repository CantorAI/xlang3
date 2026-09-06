#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace xlang3::ipc {
class SharedMemoryServerThreads {
  std::mutex mutex_;
  std::condition_variable drained_;
  size_t active_ = 0;
public:
  std::atomic<bool> stopping{false};
  std::thread listener;
  template<class F> void launch(F&& function) {
    { std::lock_guard<std::mutex> lock(mutex_); ++active_; }
    try {
      std::thread([this, fn = std::forward<F>(function)]() mutable {
        struct Done {
          SharedMemoryServerThreads* owner;
          ~Done() { owner->finish(); }
        } done{this};
        fn();
      }).detach();
    } catch (...) { finish(); throw; }
  }
  void finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!--active_) drained_.notify_all();
  }
  void drain() {
    if (listener.joinable()) listener.join();
    std::unique_lock<std::mutex> lock(mutex_);
    drained_.wait(lock, [&] { return active_ == 0; });
  }
};
}
