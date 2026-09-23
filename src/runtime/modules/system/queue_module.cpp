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

#include "../thread/runtime_lock.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kSimpleQueueNativeType = "_queue.SimpleQueue";

struct SimpleQueueState {
  std::mutex mutex;
  std::condition_variable available;
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

void raise_queue_exception(Runtime& runtime, const char* class_name, std::string message) {
  runtime.set_pending_exception(runtime.make_exception(class_name, std::move(message)));
}

bool simple_queue_raise_empty(Runtime& runtime, std::string message) {
  raise_queue_exception(runtime, "Empty", std::move(message));
  return false;
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
  state->available.notify_one();
  value_set_none(out);
  return true;
}

bool simple_queue_get_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    bool block,
    const Value& timeout,
    Value& out,
    std::string& error) {
  if (argc > 3) {
    error = "SimpleQueue.get() expected optional block/timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  bool has_timeout = timeout.tag != ValueTag::None;
  double timeout_seconds = 0.0;
  if (has_timeout) {
    if (timeout.tag == ValueTag::Int64) {
      timeout_seconds = static_cast<double>(timeout.as.i64);
    } else if (timeout.tag == ValueTag::Double) {
      timeout_seconds = timeout.as.f64;
    } else if (timeout.tag == ValueTag::Bool) {
      timeout_seconds = timeout.as.b ? 1.0 : 0.0;
    } else {
      error = "'" + std::string(value_binary_type_name(timeout)) +
              "' object cannot be interpreted as a number";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (timeout_seconds < 0.0) {
      error = "'timeout' must be a non-negative number";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  }
  std::unique_lock<std::mutex> lock(state->mutex);
  if (state->items.empty() && block) {
    XlangRuntimeExecutionSuspension execution_suspension;
    if (has_timeout) {
      state->available.wait_for(
          lock,
          std::chrono::duration<double>(timeout_seconds),
          [&state]() { return !state->items.empty(); });
    } else {
      state->available.wait(lock, [&state]() { return !state->items.empty(); });
    }
  }
  if (state->items.empty()) {
    error = "queue is empty";
    return false;
  }
  value_assign_fast(out, state->items.front());
  state->items.pop_front();
  return true;
}

bool simple_queue_get_exc(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  const bool block = argc < 2 || value_truthy(args[1]);
  const Value timeout = argc < 3 ? Value::none() : args[2];
  if (!simple_queue_get_impl(runtime, args, argc, block, timeout, out, error)) {
    if (error == "queue is empty") {
      return simple_queue_raise_empty(runtime, error);
    }
    return false;
  }
  return true;
}

bool simple_queue_get_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3) {
    error = "SimpleQueue.get() expected optional block/timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value block_value = argc >= 2 ? args[1] : Value::boolean(true);
  Value timeout = argc >= 3 ? args[2] : Value::none();
  bool block_assigned = argc >= 2;
  bool timeout_assigned = argc >= 3;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name == "block") {
      if (block_assigned) {
        error = "SimpleQueue.get() got multiple values for argument 'block'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(block_value, *kwargs[i].value);
      block_assigned = true;
    } else if (name == "timeout") {
      if (timeout_assigned) {
        error = "SimpleQueue.get() got multiple values for argument 'timeout'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(timeout, *kwargs[i].value);
      timeout_assigned = true;
    } else {
      error = "SimpleQueue.get() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (!simple_queue_get_impl(runtime, args, 1, value_truthy(block_value), timeout, out, error)) {
    if (error == "queue is empty") {
      return simple_queue_raise_empty(runtime, error);
    }
    return false;
  }
  return true;
}

bool simple_queue_put_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 2 || argc > 4) {
    error = "SimpleQueue.put() expected item and optional block/timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool block_assigned = argc >= 3;
  bool timeout_assigned = argc >= 4;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name == "block") {
      if (block_assigned) {
        error = "SimpleQueue.put() got multiple values for argument 'block'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      block_assigned = true;
    } else if (name == "timeout") {
      if (timeout_assigned) {
        error = "SimpleQueue.put() got multiple values for argument 'timeout'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      timeout_assigned = true;
    } else {
      error = "SimpleQueue.put() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return simple_queue_put(runtime, args, argc, out, error, user_data);
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
  attrs.push_back({"put", runtime.make_native_function("_queue.SimpleQueue.put", simple_queue_put, nullptr, nullptr, nullptr, false, simple_queue_put_kw)});
  attrs.push_back({"put_nowait", runtime.make_native_function("_queue.SimpleQueue.put_nowait", simple_queue_put)});
  attrs.push_back({"get", runtime.make_native_function("_queue.SimpleQueue.get", simple_queue_get_exc, nullptr, nullptr, nullptr, false, simple_queue_get_kw)});
  attrs.push_back({"get_nowait", runtime.make_native_function("_queue.SimpleQueue.get_nowait", simple_queue_get_exc)});
  attrs.push_back({"empty", runtime.make_native_function("_queue.SimpleQueue.empty", simple_queue_empty)});
  attrs.push_back({"qsize", runtime.make_native_function("_queue.SimpleQueue.qsize", simple_queue_qsize)});
  return Value::class_object("SimpleQueue", std::move(attrs));
}

} // namespace

void register_queue_module(Runtime& runtime) {
  Value simple_queue_class = make_simple_queue_class(runtime);
  Value exception_base = runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::invalid();
  Value empty_class = Value::class_object("Empty", {}, exception_base);
  Value full_class = Value::class_object("Full", {}, exception_base);
  Value shutdown_class = Value::class_object("ShutDown", {}, exception_base);
  runtime.register_builtin("Empty", empty_class);
  runtime.register_builtin("Full", full_class);
  runtime.register_builtin("ShutDown", shutdown_class);

  NativeModuleBuilder builder(runtime, "_queue");
  builder.value("SimpleQueue", simple_queue_class)
      .value("Empty", empty_class)
      .value("Full", full_class)
      .value("ShutDown", shutdown_class);
  runtime.register_module("_queue", builder.finish());
}

} // namespace xlang3
