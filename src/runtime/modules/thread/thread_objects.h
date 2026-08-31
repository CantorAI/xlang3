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

#include "xlang3/runtime.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace xlang3 {

struct XlangThreadState {
  Runtime* runtime = nullptr;
  Value target;
  std::vector<Value> args;
  std::string name;
  bool daemon = false;
  std::thread worker;
  std::mutex mutex;
  std::condition_variable done_cv;
  bool started = false;
  bool done = false;
  std::string error;
  int64_t ident = 0;
};

struct XlangLockState {
  std::mutex mutex;
  std::condition_variable cv;
  bool locked = false;
};

struct XlangRLockState {
  std::mutex mutex;
  std::condition_variable cv;
  std::thread::id owner;
  uint32_t depth = 0;
};

int64_t xlang_thread_current_ident();
size_t xlang_thread_active_count();
std::vector<int64_t> xlang_thread_active_idents();
bool xlang_thread_tuple_to_args(const Value& value, std::vector<Value>& out, std::string& error);
bool xlang_thread_start_state(std::shared_ptr<XlangThreadState> state, std::string& error);
bool xlang_thread_start_detached(Runtime& runtime, Value target, std::vector<Value> args, int64_t& ident, std::string& error);
void xlang_thread_join_state(XlangThreadState& state);
void xlang_thread_join_runtime_threads(Runtime* runtime);
bool xlang_thread_is_alive_state(XlangThreadState& state);
bool xlang_lock_acquire_value(const Value& lock, bool blocking, std::string& error);
bool xlang_lock_release_value(const Value& lock, std::string& error);
void xlang_lock_state_cleanup(void* data);
void xlang_rlock_state_cleanup(void* data);

Value xlang_thread_make_lock_class(Runtime& runtime);
Value xlang_thread_make_rlock_class(Runtime& runtime);
Value xlang_thread_make_lock_instance(Runtime& runtime);
Value xlang_thread_make_rlock_instance(Runtime& runtime);

} // namespace xlang3
