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
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <deque>
#include <mutex>

namespace xlang3 {

namespace {

constexpr const char* kSimpleQueueNativeType = "_queue.SimpleQueue";

struct SimpleQueueState {
  std::mutex mutex;
  std::deque<Value> items;
};

SimpleQueueState* simple_queue_state(const Value& self, std::string& error) {
  auto* state = static_cast<SimpleQueueState*>(instance_get_native_data(self, kSimpleQueueNativeType));
  if (state == nullptr) {
    error = "invalid SimpleQueue object";
  }
  return state;
}

void simple_queue_cleanup(void* data) {
  delete static_cast<SimpleQueueState*>(data);
}

bool simple_queue_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SimpleQueue.__init__ expected no arguments";
    return false;
  }
  auto* state = new SimpleQueueState();
  if (!instance_set_native_data(args[0], kSimpleQueueNativeType, state, simple_queue_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool simple_queue_put(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "SimpleQueue.put() expected item and optional block/timeout";
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->items.push_back(args[1]);
  }
  value_set_none(out);
  return true;
}

bool simple_queue_get(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 3) {
    error = "SimpleQueue.get() expected optional block/timeout";
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->items.empty()) {
    error = "queue is empty";
    return false;
  }
  value_assign_fast(out, state->items.front());
  state->items.pop_front();
  return true;
}

bool simple_queue_empty(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SimpleQueue.empty() expected no arguments";
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  out = Value::boolean(state->items.empty());
  return true;
}

bool simple_queue_qsize(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SimpleQueue.qsize() expected no arguments";
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  value_set_int64(out, static_cast<int64_t>(state->items.size()));
  return true;
}

Value make_simple_queue_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_queue.SimpleQueue.__init__", simple_queue_init)});
  attrs.push_back({"put", runtime.make_native_function("_queue.SimpleQueue.put", simple_queue_put)});
  attrs.push_back({"put_nowait", runtime.make_native_function("_queue.SimpleQueue.put_nowait", simple_queue_put)});
  attrs.push_back({"get", runtime.make_native_function("_queue.SimpleQueue.get", simple_queue_get)});
  attrs.push_back({"get_nowait", runtime.make_native_function("_queue.SimpleQueue.get_nowait", simple_queue_get)});
  attrs.push_back({"empty", runtime.make_native_function("_queue.SimpleQueue.empty", simple_queue_empty)});
  attrs.push_back({"qsize", runtime.make_native_function("_queue.SimpleQueue.qsize", simple_queue_qsize)});
  return Value::class_object("SimpleQueue", std::move(attrs));
}

} // namespace

void register_queue_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_queue");
  builder.value("SimpleQueue", make_simple_queue_class(runtime));
  runtime.register_module("_queue", builder.finish());
}

} // namespace xlang3
