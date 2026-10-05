/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0.
*/
#include "asyncio_native.h"

#include "xlang3/builtin_methods.h"


#include "xlang3/mapping.h"
#include "xlang3/set_object.h"
#include "xlang3/sequence.h"

#include <array>
#include <string_view>
#include <unordered_map>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace xlang3::asyncio_native {
namespace {

struct ThreadTaskState {
  Value loop = Value::none();
  Value task = Value::none();
  // Borrowed from asyncio.tasks' module slot; the runtime keeps that mapping
  // alive. Cache its address to avoid module lookup on every Task transition.
  DictObject* python_current_tasks = nullptr;
  int64_t loop_pid = 0;
};

// CPython stores the running loop and Task in per-thread interpreter state.
// Keep native Task transitions here too: consulting threading.local and the
// Python _current_tasks dict on every step adds calls to async-tree's inner
// loop. Pure-Python Tasks continue to use that dict as the fallback.
thread_local std::unordered_map<Runtime*, ThreadTaskState> thread_task_states;

ThreadTaskState* find_thread_task_state(Runtime& runtime) {
  auto it = thread_task_states.find(&runtime);
  return it == thread_task_states.end() ? nullptr : &it->second;
}

ThreadTaskState& thread_task_state(Runtime& runtime) {
  return thread_task_states[&runtime];
}

int64_t process_id() {
#ifdef _WIN32
  return _getpid();
#else
  return getpid();
#endif
}

bool current_tasks(Runtime& runtime, Value& out, std::string& error) {
  if (auto* state = find_thread_task_state(runtime);
      state != nullptr && state->python_current_tasks != nullptr) {
    out.tag = ValueTag::Object;
    out.flags = kXlangValueBorrowedRefFlag;
    out.as.obj = &state->python_current_tasks->header;
    return true;
  }
  Value tasks_module;
  std::string lookup_error;
  if (mapping_get_item(runtime.module_registry_dict(),
                       Value::string("asyncio.tasks"), tasks_module,
                       lookup_error)) {
    if (module_get_attr(tasks_module, "_current_tasks", out, error)) {
      if (auto* state = find_thread_task_state(runtime))
        state->python_current_tasks = value_as_dict(out);
      return true;
    }
    return false;
  }
  if (!import_attribute(runtime, "_asyncio", "_current_tasks", out, error))
    return false;
  if (auto* state = find_thread_task_state(runtime))
    state->python_current_tasks = value_as_dict(out);
  return true;
}

bool sync_python_current_task(Runtime& runtime, const Value& loop,
                              const Value& task, bool clear,
                              std::string& error) {
  Value tasks;
  if (!current_tasks(runtime, tasks, error)) return false;
  if (clear) {
    if (mapping_delete_item_identity_key(tasks, loop, error)) return true;
    if (error == "key not found") {
      error.clear();
      return true;
    }
    return false;
  }
  return mapping_set_item_identity_key(tasks, loop, task, error);
}

bool check_loop(Runtime& runtime, const Value& loop, std::string& error) {
  Value active;
  if (!running_loop(runtime, active, error)) return false;
  if (active.tag != loop.tag || active.as.obj != loop.as.obj)
    return raise(runtime, "RuntimeError", "loop is not the running loop", error);
  return true;
}

bool current_for_loop(Runtime& runtime, const Value& loop, Value& out,
                      std::string& error) {
  if (auto* state = find_thread_task_state(runtime); state != nullptr) {
    if (state->task.tag != ValueTag::None && value_is(state->loop, loop)) {
      value_assign_fast(out, state->task);
      return true;
    }
  }
  Value tasks;
  if (!current_tasks(runtime, tasks, error)) return false;
  Value args[] = {loop, Value::none()};
  return call_method(runtime, tasks, "get", args, 2, {}, out, error);
}

bool noarg_running(Runtime& runtime, const Value*, uint32_t argc, Value& out,
                    std::string& error, void* data) {
  if (argc != 0) return raise(runtime, "TypeError", "expected no arguments", error);
  if (!running_loop(runtime, out, error)) return false;
  if (data != nullptr && out.tag == ValueTag::None)
    return raise(runtime, "RuntimeError", "no running event loop", error);
  return true;
}

bool noarg_event_loop(Runtime& runtime, const Value*, uint32_t argc, Value& out,
                       std::string& error, void*) {
  if (argc != 0) return raise(runtime, "TypeError", "expected no arguments", error);
  return event_loop(runtime, out, error);
}

bool set_running(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                   std::string& error, void*) {
  if (argc != 1) return raise(runtime, "TypeError", "expected one loop", error);
  Value local;
  if (!helper(runtime, "asyncio.events", "_running_loop", local, error)) return false;
  if (!set_attr(runtime, local, "loop_pid",
                Value::tuple({args[0], Value::int64(process_id())}), error)) return false;
  if (args[0].tag == ValueTag::None) {
    auto found = thread_task_states.find(&runtime);
    if (found != thread_task_states.end()) {
      value_set_none(found->second.loop);
      found->second.loop_pid = 0;
      if (found->second.task.tag == ValueTag::None) {
        thread_task_states.erase(found);
      }
      else found->second.python_current_tasks = nullptr;
    }
  } else {
    auto& state = thread_task_state(runtime);
    value_assign_fast(state.loop, args[0]);
    state.loop_pid = process_id();
  }
  value_set_none(out);
  return true;
}

bool introspect_kw(Runtime& runtime, const Value* args, uint32_t argc,
                    const NativeKeywordArg* kwargs, uint32_t kwargc,
                    Value& out, std::string& error, void* all) {
  if (argc > 1 || kwargc > 1 || (argc != 0 && kwargc != 0))
    return raise(runtime, "TypeError", "expected at most one loop", error);
  Value loop = argc != 0 ? args[0] : Value::none();
  if (kwargc != 0) {
    if (kwargs[0].name == nullptr || std::string_view(kwargs[0].name) != "loop")
      return raise(runtime, "TypeError", "unexpected keyword", error);
    loop = *kwargs[0].value;
  }
  if (loop.tag == ValueTag::None) {
    if (!running_loop(runtime, loop, error)) return false;
    if (loop.tag == ValueTag::None)
      return raise(runtime, "RuntimeError", "no running event loop", error);
  }
  if (all == nullptr) return current_for_loop(runtime, loop, out, error);

  auto own = registry(runtime, error);
  if (own == nullptr) return false;
  std::vector<Value> snapshot;
  {
    // The interpreter lock excludes destruction while weak addresses become
    // owning Values. Never hold this registry mutex during Python iteration,
    // user done()/get_loop(), or destruction of the owning snapshot.
    std::lock_guard<std::mutex> guard(own->mutex);
    for (auto* task = own->task_head; task != nullptr; task = task->next) {
      Value borrowed;
      borrowed.tag = ValueTag::Object;
      borrowed.flags = kXlangValueBorrowedRefFlag;
      borrowed.as.obj = &task->owner->header;
      snapshot.push_back(borrowed);
    }
  }
  out = Value::set({});
  Value native_task;
  if (!import_attribute(runtime, "_asyncio", "Task", native_task, error)) return false;
  for (const auto& task : snapshot) {
    auto* fs = state(runtime, task, error, false);
    if (fs == nullptr) return false;
    bool pending;
    Value owner_loop;
    if (value_is(value_as_instance(task)->klass, native_task)) {
      pending = fs->completion == Completion::Pending;
      owner_loop = fs->loop;
    } else {
      Value done, get_loop;
      bool finished;
      if (!call_method(runtime, task, "done", nullptr, 0, {}, done, error) ||
          !runtime_truthy(runtime, done, finished, error)) return false;
      pending = !finished;
      if (!pending) continue;
      if (!helper(runtime, "asyncio.futures", "_get_loop", get_loop, error) ||
          !runtime_call_callable(runtime, get_loop, &task, 1, owner_loop, error)) return false;
    }
    if (pending && value_is(owner_loop, loop) && !set_add_runtime(runtime, out, task, error)) return false;
  }
  // Alternative Task implementations retain the standard library's Python
  // weak sets. Keep them visible without routing native Tasks through WeakSet.
  Value python_all, other;
  if (!helper(runtime, "asyncio.tasks", "_py_all_tasks", python_all, error) ||
      !runtime_call_callable(runtime, python_all, &loop, 1, other, error)) return false;
  std::vector<Value> external;
  if (!runtime_collect_iterable(runtime, other, external, error)) return false;
  for (const auto& task : external)
    if (!set_add_runtime(runtime, out, task, error)) return false;
  return true;
}

bool introspect(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                 std::string& error, void* all) {
  return introspect_kw(runtime, args, argc, nullptr, 0, out, error, all);
}

bool change_current(Runtime& runtime, const Value* args, uint32_t argc,
                      Value& out, std::string& error, void* action) {
  if (argc != 2) return raise(runtime, "TypeError", "expected loop and task", error);
  const auto operation = reinterpret_cast<intptr_t>(action);
  if (operation == 2) return swap_task(runtime, args[0], args[1], out, error);
  if (!(operation == 0 ? enter_task(runtime, args[0], args[1], error) :
                        leave_task(runtime, args[0], args[1], error))) return false;
  value_set_none(out);
  return true;
}

bool registration(Runtime& runtime, const Value* args, uint32_t argc,
                     Value& out, std::string& error, void* operation) {
  if (argc != 1) return raise(runtime, "TypeError", "expected one task", error);
  const intptr_t action = reinterpret_cast<intptr_t>(operation);
  auto* fs = static_cast<FutureState*>(instance_get_native_data(args[0], kFutureData));
  if (fs != nullptr && fs->task.has_value()) {
    if ((action & 1) != 0) unregister(*fs);
    else if (!fs->task->registered) {
      auto own = fs->task->registry;
      if (own == nullptr) return raise(runtime, "RuntimeError", "uninitialized Task", error);
      std::lock_guard<std::mutex> guard(own->mutex);
      fs->task->owner = fs->owner;
      fs->task->next = own->task_head;
      fs->task->previous = nullptr;
      if (own->task_head != nullptr) own->task_head->previous = &*fs->task;
      own->task_head = &*fs->task;
      fs->task->registered = true;
    }
    value_set_none(out);
    return true;
  }
  const char* names[] = {"_py_register_task", "_py_unregister_task",
                         "_py_register_eager_task", "_py_unregister_eager_task"};
  Value function;
  return helper(runtime, "asyncio.tasks", names[action], function, error) &&
      runtime_call_callable(runtime, function, args, argc, out, error);
}

bool track_await(Runtime& runtime, const Value* args, uint32_t argc,
                  Value& out, std::string& error, void* add) {
  if (argc != 2) return raise(runtime, "TypeError", "expected future and waiter", error);
  if (!awaited_by(runtime, args[0], args[1], add != nullptr, error)) return false;
  value_set_none(out);
  return true;
}

void registry_cleanup(void* data) {
  delete static_cast<std::shared_ptr<Registry>*>(data);
}

} // namespace

bool raise(Runtime& runtime, const char* name, std::string message,
            std::string& error) {
  error = std::move(message);
  if (std::string_view(name) == "InvalidStateError") {
    Value klass;
    if (!helper(runtime, "asyncio.exceptions", name, klass, error)) return false;
    runtime.set_pending_exception(runtime.make_exception_from_class(klass, error));
  } else runtime.raise_class_error(name, error);
  return false;
}

bool helper(Runtime& runtime, const char* module, const char* name,
             Value& out, std::string& error) {
  // Cache Python helper objects in the module's ordinary, GC-visible slots.
  // Native payloads/statics must not hide owning interpreter references.
  Value owner;
  if (!runtime.import_module("_asyncio", owner, error)) return false;
  const std::string key = std::string("_helper:") + module + ":" + name;
  std::string missing;
  if (module_get_attr(owner, key, out, missing)) return true;
  if (!import_attribute(runtime, module, name, out, error)) return false;
  return module_set_attr(owner, key, out, error);
}

bool get_attr(Runtime& runtime, const Value& self, const char* name,
               Value& out, std::string& error, bool optional) {
  Value args[] = {self, Value::string(name), Value::none()};
  return runtime_call_callable(runtime, *runtime.find_builtin("getattr"), args,
                               optional ? 3 : 2, out, error);
}

bool set_attr(Runtime& runtime, const Value& self, const char* name,
               const Value& value, std::string& error) {
  Value args[] = {self, Value::string(name), value};
  Value ignored;
  return runtime_call_callable(runtime, *runtime.find_builtin("setattr"), args, 3,
                               ignored, error);
}

bool call_method(Runtime& runtime, const Value& self, const char* name,
                  const Value* args, uint32_t argc,
                  const std::vector<std::pair<std::string, Value>>& kwargs,
                  Value& out, std::string& error) {
  // Native asyncio transitions frequently call ordinary Python methods on
  // their loop/Future objects. For an unshadowed Python function descriptor,
  // prepend self directly instead of routing through getattr() and allocating
  // a temporary BoundMethod on every scheduled callback. The guards retain
  // dynamic attribute, slot, descriptor, and __getattribute__ behavior.
  if (auto* instance = value_as_instance(self);
      instance != nullptr && instance->native_get_attr == nullptr) {
    auto* klass = value_as_class(instance->klass);
    if (klass != nullptr && !klass->has_getattribute_hook) {
      bool shadowed = false;
      for (const auto& attr : instance->attrs) {
        if (attr.first == name) {
          shadowed = true;
          break;
        }
      }
      if (!shadowed) {
        if (auto* attributes = value_as_dict(instance_attribute_storage(*instance))) {
          for (const auto& entry : attributes->entries) {
            const auto* key = value_as_string(entry.first);
            if (key != nullptr && string_object_view(*key) == name) {
              shadowed = true;
              break;
            }
          }
        }
      }
      if (!shadowed) {
        const auto slot = klass->instance_slot_indices.find(name);
        if (slot != klass->instance_slot_indices.end() &&
            slot->second < instance_slot_count(instance) &&
            instance_slot_at(instance, slot->second).tag != ValueTag::Invalid) {
          shadowed = true;
        }
      }
      Value raw_method;
      std::string lookup_error;
      if (!shadowed && object_lookup_class_attr(
              instance->klass, name, raw_method, lookup_error)) {
        const auto* native = value_as_native_function(raw_method);
        if (value_as_function(raw_method) != nullptr ||
            (native != nullptr && native->bind_as_descriptor)) {
          std::array<Value, 8> small_args{};
          std::vector<Value> large_args;
          Value* call_args = nullptr;
          const size_t call_argc = static_cast<size_t>(argc) + 1;
          if (call_argc <= small_args.size()) {
            call_args = small_args.data();
          } else {
            large_args.reserve(call_argc);
            call_args = large_args.data();
          }
          if (call_argc <= small_args.size()) {
            value_assign_fast(call_args[0], self);
            for (uint32_t index = 0; index < argc; ++index) {
              value_assign_fast(call_args[index + 1], args[index]);
            }
          } else {
            large_args.push_back(self);
            for (uint32_t index = 0; index < argc; ++index) {
              large_args.push_back(args[index]);
            }
            call_args = large_args.data();
          }
          return runtime_call_callable_kw(
              runtime, raw_method, call_args, static_cast<uint32_t>(call_argc),
              kwargs, out, error);
        }
      }
    }
  }
  Value method;
  return get_attr(runtime, self, name, method, error) &&
      runtime_call_callable_kw(runtime, method, args, argc, kwargs, out, error);
}

bool exception_is(Runtime& runtime, const Value& exception, const char* name) {
  // Runtime::exception_type() uses RuntimeError as its fallback for non-
  // exception Values. Check the representation first or values such as int
  // would be accepted by FutureIter.throw() as already-normalized exceptions.
  if (value_as_instance(exception) == nullptr) return false;
  Value type = runtime.exception_type(exception);
  auto* klass = value_as_class(type);
  if (klass == nullptr) return false;
  if (std::string_view(name) == "CancelledError") {
    Value expected;
    std::string error;
    return helper(runtime, "asyncio.exceptions", name, expected, error) &&
        class_is_subclass(klass, value_as_class(expected));
  }
  return class_has_builtin_base_name(klass, name);
}

bool running_loop(Runtime& runtime, Value& out, std::string& error) {
  if (auto* state = find_thread_task_state(runtime); state != nullptr) {
    if (state->loop.tag == ValueTag::None || state->loop_pid != process_id()) {
      value_set_none(out);
      return true;
    }
    value_assign_fast(out, state->loop);
    return true;
  }
  // Reuse the event module's threading.local storage rather than a C++ TLS
  // Value whose destruction/GC lifetime would outlive its interpreter. PID
  // validation retains the Python fallback's fork behavior.
  Value local, pair;
  if (!helper(runtime, "asyncio.events", "_running_loop", local, error) ||
      !get_attr(runtime, local, "loop_pid", pair, error)) return false;
  auto* tuple = value_as_tuple(pair);
  if (tuple == nullptr || tuple->items.size() != 2)
    return raise(runtime, "RuntimeError", "invalid running loop state", error);
  const Value& pid = tuple->items[1];
  out = pid.tag == ValueTag::Int64 && pid.as.i64 == process_id()
      ? tuple->items[0] : Value::none();
  return true;
}

bool event_loop(Runtime& runtime, Value& out, std::string& error) {
  if (!running_loop(runtime, out, error)) return false;
  if (out.tag != ValueTag::None) return true;
  Value get_policy, policy;
  return helper(runtime, "asyncio.events", "_get_event_loop_policy", get_policy, error) &&
      runtime_call_callable(runtime, get_policy, nullptr, 0, policy, error) &&
      call_method(runtime, policy, "get_event_loop", nullptr, 0, {}, out, error);
}

bool copy_context(Runtime& runtime, Value& out, std::string& error) {
  Value copy;
  return helper(runtime, "contextvars", "copy_context", copy, error) &&
      runtime_call_callable(runtime, copy, nullptr, 0, out, error);
}

bool enter_task(Runtime& runtime, const Value& loop, const Value& task,
                 std::string& error) {
  if (!check_loop(runtime, loop, error)) return false;
  Value current;
  if (!current_for_loop(runtime, loop, current, error)) return false;
  if (current.tag != ValueTag::None)
    return raise(runtime, "RuntimeError", "Cannot enter a task while another task is being executed", error);
  auto& state = thread_task_state(runtime);
  // The Python asyncio.tasks.current_task implementation reads this dict.
  // Keep its C-extension ABI view current while native callers take the TLS
  // fast path above, so pure-Python TaskGroup and user code see the same task.
  if (!sync_python_current_task(runtime, loop, task, false, error)) return false;
  value_assign_fast(state.loop, loop);
  state.loop_pid = process_id();
  value_assign_fast(state.task, task);
  return true;
}

bool leave_task(Runtime& runtime, const Value& loop, const Value& task,
                 std::string& error) {
  if (!check_loop(runtime, loop, error)) return false;
  Value current;
  if (!current_for_loop(runtime, loop, current, error)) return false;
  if (current.tag != task.tag || current.as.obj != task.as.obj)
    return raise(runtime, "RuntimeError", "Invalid attempt to leave a task which is not entered", error);
  if (!sync_python_current_task(runtime, loop, Value::none(), true, error)) return false;
  if (auto* state = find_thread_task_state(runtime); state != nullptr) {
    value_set_none(state->task);
    if (state->loop.tag == ValueTag::None) thread_task_states.erase(&runtime);
  }
  return true;
}

bool swap_task(Runtime& runtime, const Value& loop, const Value& task,
                Value& previous, std::string& error) {
  if (!check_loop(runtime, loop, error) ||
      !current_for_loop(runtime, loop, previous, error)) return false;
  if (task.tag == ValueTag::None) {
    if (!sync_python_current_task(runtime, loop, Value::none(), true, error)) return false;
  } else {
    if (!sync_python_current_task(runtime, loop, task, false, error)) return false;
  }
  auto& state = thread_task_state(runtime);
  value_assign_fast(state.loop, loop);
  state.loop_pid = process_id();
  value_assign_fast(state.task, task);
  return true;
}

bool awaited_by(Runtime& runtime, const Value& future, const Value& waiter,
                 bool add, std::string& error) {
  auto* fs = static_cast<FutureState*>(instance_get_native_data(future, kFutureData));
  if (fs == nullptr || instance_get_native_data(waiter, kFutureData) == nullptr) return true;
  if (add) {
    // A single waiter is overwhelmingly common. Match CPython's inline
    // reference and allocate a set only when a second waiter is registered.
    if (fs->awaited_by.tag == ValueTag::None) fs->awaited_by = waiter;
    else {
      if (!fs->awaited_by_is_set) {
        fs->awaited_by = Value::set({fs->awaited_by});
        fs->awaited_by_is_set = true;
      }
      if (!set_add_runtime(runtime, fs->awaited_by, waiter, error)) return false;
    }
  } else if (fs->awaited_by.tag != ValueTag::None) {
    if (!fs->awaited_by_is_set) {
      if (value_is(fs->awaited_by, waiter)) {
        Value retired = std::move(fs->awaited_by);
        fs->awaited_by = Value::none();
        return publish_edges(future, *fs, error);
      }
    } else {
      Value ignored;
      if (!call_method(runtime, fs->awaited_by, "discard", &waiter, 1, {}, ignored, error)) return false;
    }
  }
  return publish_edges(future, *fs, error);
}

std::shared_ptr<Registry> registry(Runtime& runtime, std::string& error) {
  Value owner;
  if (!import_attribute(runtime, "_asyncio", "_registry", owner, error)) return {};
  auto* data = static_cast<std::shared_ptr<Registry>*>(instance_get_native_data(owner, kRegistryData));
  if (data == nullptr) {
    raise(runtime, "RuntimeError", "invalid task registry", error);
    return {};
  }
  return *data;
}

} // namespace xlang3::asyncio_native

namespace xlang3 {

void register_asyncio_module(Runtime& runtime) {
  using namespace asyncio_native;
  Value registry_owner = Value::instance(Value::class_object("_TaskRegistry", {}));
  std::string error;
  auto* own = new std::shared_ptr<Registry>(std::make_shared<Registry>());
  if (!instance_set_native_data(registry_owner, kRegistryData, own, registry_cleanup, error)) {
    delete own;
    return;
  }
  Value future = future_class(runtime);
  Value task = task_class(runtime, future);
  NativeModuleBuilder builder(runtime, "_asyncio");
  builder.value("Future", future)
      .value("Task", task)
      .value("_FutureIter", iterator_class(runtime))
      .value("_registry", registry_owner)
      .value("_current_tasks", Value::dict({}))
      .value("_scheduled_tasks", (*own)->scheduled_tasks)
      .value("_eager_tasks", (*own)->eager_tasks)
      // The eager-tree workload calls these native entry points thousands of
      // times. Keep positional arguments in VM registers, like CPython's
      // borrowed-argument vectorcall path; keyword calls still use their
      // existing compatibility handlers.
      .function("_get_running_loop", noarg_running, builtin_fast_adapter<noarg_running, 1>)
      .function("_set_running_loop", set_running)
      .function("get_event_loop", noarg_event_loop);
  builder.value("get_running_loop", runtime.make_native_function(
      "_asyncio.get_running_loop", noarg_running, reinterpret_cast<void*>(1), nullptr,
      builtin_fast_adapter<noarg_running, 1>));
  builder.value("current_task", runtime.make_native_function(
      "_asyncio.current_task", introspect, nullptr, nullptr,
      builtin_fast_adapter<introspect, 2>, false, introspect_kw));
  builder.value("all_tasks", runtime.make_native_function("_asyncio.all_tasks", introspect, reinterpret_cast<void*>(1), nullptr, nullptr, false, introspect_kw));
  const char* changes[] = {"_enter_task", "_leave_task", "_swap_current_task"};
  for (intptr_t index = 0; index != 3; ++index)
    builder.value(changes[index], runtime.make_native_function(std::string("_asyncio.") + changes[index], change_current, reinterpret_cast<void*>(index)));
  const char* registers[] = {"_register_task", "_unregister_task", "_register_eager_task", "_unregister_eager_task"};
  for (intptr_t index = 0; index != 4; ++index)
    builder.value(registers[index], runtime.make_native_function(std::string("_asyncio.") + registers[index], registration, reinterpret_cast<void*>(index)));
  // Task step/wakeup calls these once for each Future wait transition. Keep
  // the function's two positional Values in VM registers, as CPython's native
  // helper receives borrowed PyObject pointers without a temporary argument list.
  builder.value("future_add_to_awaited_by", runtime.make_native_function(
      "_asyncio.future_add_to_awaited_by", track_await, reinterpret_cast<void*>(1),
      nullptr, builtin_fast_adapter<track_await, 2>));
  builder.value("future_discard_from_awaited_by", runtime.make_native_function(
      "_asyncio.future_discard_from_awaited_by", track_await, nullptr, nullptr,
      builtin_fast_adapter<track_await, 2>));
  runtime.register_module("_asyncio", builder.finish());
}

void initialize_asyncio_module_compat(Runtime& runtime) {
  using namespace asyncio_native;
  Value module, weakref_module, weakset_factory;
  Value scheduled_tasks, eager_tasks;
  std::string error;
  if (!runtime.import_module("_asyncio", module, error) ||
      !runtime.import_module("weakref", weakref_module, error) ||
      !module_get_attr(weakref_module, "WeakSet", weakset_factory, error) ||
      !runtime_call_callable(runtime, weakset_factory, nullptr, 0, scheduled_tasks, error) ||
      !runtime_call_callable(runtime, weakset_factory, nullptr, 0, eager_tasks, error)) {
    Value pending;
    runtime.take_pending_exception(pending);
    return;
  }
  if (!module_set_attr(module, "_scheduled_tasks", scheduled_tasks, error) ||
      !module_set_attr(module, "_eager_tasks", eager_tasks, error)) {
    Value pending;
    runtime.take_pending_exception(pending);
    return;
  }
  Value owner;
  if (!module_get_attr(module, "_registry", owner, error)) return;
  auto* data = static_cast<std::shared_ptr<Registry>*>(
      instance_get_native_data(owner, kRegistryData));
  if (data == nullptr) return;
  (*data)->scheduled_tasks = std::move(scheduled_tasks);
  (*data)->eager_tasks = std::move(eager_tasks);
}

} // namespace xlang3
