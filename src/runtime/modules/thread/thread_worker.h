/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once

#include <thread>

#if defined(__APPLE__)
#include <cerrno>
#include <functional>
#include <memory>
#include <pthread.h>
#include <system_error>
#include <utility>
#endif

namespace xlang3 {

#if defined(__APPLE__)
// Darwin's 512 KiB default pthread stack is too small for nested VM calls.
// Allocate virtual stack space per Python worker; pages commit on demand.
class XlangWorkerThread {
 public:
  XlangWorkerThread() = default;
  explicit XlangWorkerThread(std::function<void()> function) {
    auto work = std::make_unique<std::function<void()>>(std::move(function));
    pthread_attr_t attributes;
    int status = pthread_attr_init(&attributes);
    if (status) throw std::system_error(status, std::generic_category(), "pthread_attr_init");
    status = pthread_attr_setstacksize(&attributes, 16 * 1024 * 1024);
    if (!status) {
      status = pthread_create(&handle_, &attributes, [](void* pointer) -> void* {
        std::unique_ptr<std::function<void()>> callback(static_cast<std::function<void()>*>(pointer));
        try { (*callback)(); } catch (...) { std::terminate(); }
        return nullptr;
      }, work.get());
    }
    pthread_attr_destroy(&attributes);
    if (status) throw std::system_error(status, std::generic_category(), "pthread_create");
    work.release();
    joinable_ = true;
  }
  ~XlangWorkerThread() { if (joinable_) std::terminate(); }
  XlangWorkerThread(const XlangWorkerThread&) = delete;
  XlangWorkerThread& operator=(const XlangWorkerThread&) = delete;
  XlangWorkerThread(XlangWorkerThread&& other) noexcept
      : handle_(other.handle_), joinable_(std::exchange(other.joinable_, false)) {}
  XlangWorkerThread& operator=(XlangWorkerThread&& other) noexcept {
    if (joinable_) std::terminate();
    handle_ = other.handle_;
    joinable_ = std::exchange(other.joinable_, false);
    return *this;
  }
  bool joinable() const noexcept { return joinable_; }
  void join() {
    if (!joinable_) throw std::system_error(EINVAL, std::generic_category(), "pthread_join");
    const int status = pthread_join(handle_, nullptr);
    if (status) throw std::system_error(status, std::generic_category(), "pthread_join");
    joinable_ = false;
  }
  void detach() {
    if (!joinable_) throw std::system_error(EINVAL, std::generic_category(), "pthread_detach");
    const int status = pthread_detach(handle_);
    if (status) throw std::system_error(status, std::generic_category(), "pthread_detach");
    joinable_ = false;
  }
 private:
  pthread_t handle_{};
  bool joinable_ = false;
};
#else
using XlangWorkerThread = std::thread;
#endif

} // namespace xlang3
