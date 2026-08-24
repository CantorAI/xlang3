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

/*
Author: Shawn Xiong

XlangTaskState is the runtime payload behind task.Task. It is intentionally
separate from threading.Thread: Thread mirrors CPython's fire-and-join shape,
while Task owns a result/error slot so async-style APIs can compose work.
*/
struct XlangTaskState {
  Runtime* runtime = nullptr;
  Value target;
  std::vector<Value> args;
  Value result;
  std::thread worker;
  std::mutex mutex;
  std::condition_variable done_cv;
  bool started = false;
  bool done = false;
  std::string error;
};

bool xlang_task_tuple_to_args(const Value& value, std::vector<Value>& out, std::string& error);
bool xlang_task_start_state(XlangTaskState& state, std::string& error);
bool xlang_task_join_state(XlangTaskState& state, Value& out, std::string& error);
bool xlang_task_is_done_state(XlangTaskState& state);
void xlang_task_state_cleanup(void* data);

bool xlang_task_spawn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error);
bool xlang_task_completed(Runtime& runtime, const Value& result, Value& out, std::string& error);
bool xlang_task_await_value(Runtime& runtime, const Value& value, Value& out, std::string& error);
bool xlang_task_join_value(const Value& task, Value& out, std::string& error);
bool xlang_task_done_value(const Value& task, bool& done, std::string& error);
bool xlang_task_await_all(Runtime& runtime, const Value& tasks, Value& out, std::string& error);

Value xlang_task_make_task_class(Runtime& runtime);

} // namespace xlang3
