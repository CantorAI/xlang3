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
#include <mutex>
#include <thread>

namespace xlang3 {

struct XlangThreadState {
  Runtime* runtime = nullptr;
  Value target;
  std::vector<Value> args;
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

int64_t xlang_thread_current_ident();
bool xlang_thread_tuple_to_args(const Value& value, std::vector<Value>& out, std::string& error);
bool xlang_thread_start_state(XlangThreadState& state, std::string& error);
bool xlang_thread_start_detached(Runtime& runtime, Value target, std::vector<Value> args, int64_t& ident, std::string& error);
void xlang_thread_join_state(XlangThreadState& state);
bool xlang_thread_is_alive_state(XlangThreadState& state);
void xlang_thread_state_cleanup(void* data);
void xlang_lock_state_cleanup(void* data);

Value xlang_thread_make_thread_class(Runtime& runtime);
Value xlang_thread_make_lock_class(Runtime& runtime);
Value xlang_thread_make_lock_instance(Runtime& runtime);

} // namespace xlang3
