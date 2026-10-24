/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0.
*/
#pragma once

#include "xlang3/builtins.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xlang3::asyncio_native {

inline constexpr const char* kFutureData = "_asyncio.Future";
inline constexpr const char* kIteratorData = "_asyncio.FutureIter";
inline constexpr const char* kRegistryData = "_asyncio.Registry";

enum class Completion : uint8_t { Pending, Cancelled, Finished };

struct Callback {
  Value function = Value::none();
  Value context = Value::none();
};

struct Registry;
struct TaskFields {
  std::shared_ptr<Registry> registry;
  InstanceObject* owner = nullptr;
  TaskFields* previous = nullptr;
  TaskFields* next = nullptr;
  Value coroutine = Value::none();
  Value waiter = Value::none();
  Value context = Value::none();
  Value name = Value::none();
  uint64_t cancellation_requests = 0;
  bool must_cancel = false;
  bool log_destroy_pending = true;
  bool registered = false;
};

struct Registry {
  // Keep a weak intrusive list like CPython's per-thread task list. A hash set
  // would allocate one node per live Task and add work to every task creation
  // and completion in async-tree. Snapshot Values under the VM lock, then
  // release this mutex before any Python method call or Value destruction.
  std::mutex mutex;
  TaskFields* task_head = nullptr;
  uint64_t next_name = 1;
  // asyncio.tasks imports these compatibility sets from _asyncio. Weak sets
  // preserve non-owning tracking for third-party Task implementations.
  Value scheduled_tasks = Value::set({});
  Value eager_tasks = Value::set({});
};


struct FutureState {
  InstanceObject* owner = nullptr;
  Completion completion = Completion::Pending;
  Value loop = Value::none();
  Value result = Value::none();
  Value exception = Value::none();
  Value exception_traceback = Value::none();
  Value source_traceback = Value::none();
  Value cancel_message = Value::none();
  Value cancelled_exception = Value::none();
  Value awaited_by = Value::none();
  bool awaited_by_is_set = false;
  bool log_traceback = false;
  bool blocking = false;

  // CPython keeps the first callback/context in native fields. Most I/O
  // futures have one waiter; do not allocate a Python tuple/list or a vector
  // buffer for that common case. Multiple callbacks preserve insertion order.
  bool has_first_callback = false;
  Callback first_callback;
  std::vector<Callback> additional_callbacks;
  // Task is a native Future subtype: keep its state inline in the native
  // payload so task creation needs one payload allocation, not a second
  // heap allocation for TaskFields. This also makes the hot Task fields
  // adjacent to the Future state used by each step/wakeup.
  std::optional<TaskFields> task;
};

struct AwaitState {
  // A pending Future needs a tiny iterator, not a suspended Python VM frame.
  // This is a strong reference and must also appear in native GC edges.
  Value future = Value::none();
};

FutureState* state(Runtime& runtime, const Value& self, std::string& error,
                   bool require_initialized = true);
bool publish_edges(const Value& self, FutureState& state, std::string& error);
void unregister(FutureState& state);
void clear_future(void* payload);
bool raise(Runtime& runtime, const char* exception_name, std::string message,
           std::string& error);
bool helper(Runtime& runtime, const char* module, const char* name,
            Value& out, std::string& error);
inline bool import_attribute(Runtime& runtime, const std::string& module_name,
                             const std::string& name, Value& out,
                             std::string& error) {
  Value module;
  return runtime.import_module(module_name, module, error) &&
         module_get_attr(module, name, out, error);
}
bool call_method(Runtime& runtime, const Value& receiver, const char* name,
                 const Value* args, uint32_t count,
                 const std::vector<std::pair<std::string, Value>>& keywords,
                 Value& out, std::string& error);
bool exception_is(Runtime& runtime, const Value& exception, const char* name);
bool cancelled_error(Runtime& runtime, const Value& self, FutureState& state,
                     Value& out, std::string& error);
bool future_result(Runtime& runtime, const Value& self, FutureState& state,
                   Value& out, std::string& error);
bool finish_result(Runtime& runtime, const Value& self, FutureState& state,
                   Value result, std::string& error);
bool finish_exception(Runtime& runtime, const Value& self, FutureState& state,
                      Value exception, std::string& error);
bool cancel_future(Runtime& runtime, const Value& self, FutureState& state,
                   Value message, bool& changed, std::string& error);
bool initialize_future(Runtime& runtime, const Value& self, FutureState& state,
                       Value loop, std::string& error);
bool running_loop(Runtime& runtime, Value& out, std::string& error);
bool event_loop(Runtime& runtime, Value& out, std::string& error);
bool copy_context(Runtime& runtime, Value& out, std::string& error);
bool get_attr(Runtime& runtime, const Value& self, const char* name,
              Value& out, std::string& error, bool optional = false);
bool set_attr(Runtime& runtime, const Value& self, const char* name,
              const Value& value, std::string& error);
bool enter_task(Runtime& runtime, const Value& loop, const Value& task,
                std::string& error);
bool leave_task(Runtime& runtime, const Value& loop, const Value& task,
                std::string& error);
bool swap_task(Runtime& runtime, const Value& loop, const Value& task,
               Value& previous, std::string& error);
bool awaited_by(Runtime& runtime, const Value& future, const Value& waiter,
                bool add, std::string& error);
bool add_native_callback(Runtime& runtime, const Value& self, FutureState& state,
                         Callback callback, std::string& error);
Value future_class(Runtime& runtime);
Value iterator_class(Runtime& runtime);
Value task_class(Runtime& runtime, const Value& future_base);
std::shared_ptr<Registry> registry(Runtime& runtime, std::string& error);
void initialize_asyncio_module_compat(Runtime& runtime);

} // namespace xlang3::asyncio_native
