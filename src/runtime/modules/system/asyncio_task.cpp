/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0.
*/
#include "asyncio_native.h"

#include "xlang3/generator.h"
#include "xlang3/interpreter.h"
#include "xlang3/mapping.h"

namespace xlang3::asyncio_native {
namespace {

FutureState* task_state(Runtime& runtime, const Value& self, std::string& error) {
  auto* fs = state(runtime, self, error);
  if (fs == nullptr) return nullptr;
  if (!fs->task.has_value()) {
    raise(runtime, "RuntimeError", "uninitialized Task object", error);
    return nullptr;
  }
  return fs;
}

bool exact_future(Runtime& runtime, const Value& value, bool& ok, std::string& error) {
  ok = false;
  auto* instance = value_as_instance(value);
  if (instance == nullptr) return false;
  Value future_type;
  Value task_type;
  if (!import_attribute(runtime, "_asyncio", "Future", future_type, error) ||
      !import_attribute(runtime, "_asyncio", "Task", task_type, error)) return false;
  ok = true;
  auto* concrete_class = value_as_class(instance->klass);
  return concrete_class != nullptr &&
      (concrete_class == value_as_class(future_type) ||
       concrete_class == value_as_class(task_type));
}

Value step_callback(Runtime& runtime, const Value& self, bool eager) {
  const Value* function = runtime.find_native_symbol(
      eager ? "_asyncio.Task._eager_step" : "_asyncio.Task._step");
  return function == nullptr ? Value::none() : Value::bound_method(self, *function);
}

Value wakeup_callback(Runtime& runtime, const Value& self) {
  const Value* function = runtime.find_native_symbol("_asyncio.Task._wakeup");
  return function == nullptr ? Value::none() : Value::bound_method(self, *function);
}

bool schedule_step(Runtime& runtime, const Value& self, FutureState& fs,
                     const Value& exception, std::string& error) {
  Value callback = step_callback(runtime, self, false);
  Value args[] = {std::move(callback), exception};
  Value ignored;
  return call_method(runtime, fs.loop, "call_soon", args,
      exception.tag == ValueTag::None ? 1 : 2,
      {{"context", fs.task->context}}, ignored, error);
}

bool schedule_error(Runtime& runtime, const Value& self, FutureState& fs,
                      const std::string& message, std::string& error) {
  return schedule_step(runtime, self, fs, runtime.make_exception("RuntimeError", message), error);
}

bool handle_yield(Runtime& runtime, const Value& self, FutureState& fs,
                   const Value& yielded, std::string& error) {
  auto& task = *fs.task;
  if (value_is(self, yielded))
    return schedule_error(runtime, self, fs, "Task cannot await on itself", error);
  if (yielded.tag == ValueTag::None)
    return schedule_step(runtime, self, fs, Value::none(), error);

  auto* native = static_cast<FutureState*>(instance_get_native_data(yielded, kFutureData));
  bool exact_ok = false;
  const bool exact = exact_future(runtime, yielded, exact_ok, error);
  if (!exact_ok && value_as_instance(yielded) != nullptr) return false;
  bool blocking = false;
  Value loop;
  if (exact) {
    blocking = native->blocking;
    loop = native->loop;
  } else {
    Value flag;
    if (!get_attr(runtime, yielded, "_asyncio_future_blocking", flag, error, true)) return false;
    if (flag.tag == ValueTag::None) {
      const auto* generator = value_as_generator(yielded);
      return schedule_error(runtime, self, fs,
          generator != nullptr && !generator->is_coroutine
              ? "yield was used instead of yield from for generator in task"
              : "Task got bad yield: " + value_to_string(yielded), error);
    }
    if (!runtime_truthy(runtime, flag, blocking, error)) return false;
    Value get_loop;
    if (!helper(runtime, "asyncio.futures", "_get_loop", get_loop, error) ||
        !runtime_call_callable(runtime, get_loop, &yielded, 1, loop, error)) return false;
  }
  if (!value_is(loop, fs.loop))
    return schedule_error(runtime, self, fs, "Task got Future attached to a different loop", error);
  if (!blocking)
    return schedule_error(runtime, self, fs, "yield was used instead of yield from for Future", error);

  if (!awaited_by(runtime, yielded, self, true, error)) return false;
  Value wakeup = wakeup_callback(runtime, self);
  if (exact) {
    // Exact native types use their payload and inline callback directly.
    // Subclasses and third-party futures retain dynamic descriptor/method
    // dispatch; their blocking and result overrides remain observable.
    native->blocking = false;
    if (!add_native_callback(runtime, yielded, *native, {wakeup, task.context}, error)) return false;
  } else {
    if (!set_attr(runtime, yielded, "_asyncio_future_blocking", Value::boolean(false), error)) return false;
    if (!call_method(runtime, yielded, "add_done_callback", &wakeup, 1,
                     {{"context", task.context}}, loop, error)) return false;
  }
  Value previous_waiter = std::move(task.waiter);
  task.waiter = yielded;
  if (!publish_edges(self, fs, error)) return false;
  if (task.must_cancel) {
    // User cancel/attribute hooks can mutate the Task. Hold the message and
    // waiter across that call instead of borrowing a payload field reference.
    Value message = fs.cancel_message;
    Value waiter = task.waiter;
    Value accepted;
    if (!call_method(runtime, waiter, "cancel", &message, 1, {}, accepted, error)) return false;
    bool cancelled = false;
    if (!runtime_truthy(runtime, accepted, cancelled, error)) return false;
    if (cancelled) task.must_cancel = false;
  }
  return true;
}

bool step_impl(Runtime& runtime, const Value& self, FutureState& fs,
                Value exception, std::string& error) {
  auto& task = *fs.task;
  if (fs.completion != Completion::Pending)
    return raise(runtime, "InvalidStateError", "Task step: already done", error);
  if (task.must_cancel) {
    if (exception.tag == ValueTag::None || !exception_is(runtime, exception, "CancelledError"))
      if (!cancelled_error(runtime, self, fs, exception, error)) return false;
    task.must_cancel = false;
  }
  Value retired_waiter = std::move(task.waiter);
  task.waiter = Value::none();
  if (!publish_edges(self, fs, error)) return false;
  if (task.coroutine.tag == ValueTag::None)
    return raise(runtime, "RuntimeError", "uninitialized Task object", error);
  Value coroutine = task.coroutine;
  Value argument = exception.tag == ValueTag::None ? Value::none() : exception;
  Value yielded;
  if (value_as_generator(coroutine) != nullptr) {
    // CPython's task_step_impl uses PyIter_Send for native coroutine objects;
    // it does not look up and call coro.send for each task transition. Match
    // that boundary: resume the XLang VM continuation directly, preserving
    // StopIteration.value without constructing/catching a Python exception.
    // This is the inner loop exercised tens of thousands of times by
    // pyperformance's async-tree variants.
    bool completed = false;
    const bool resumed = exception.tag == ValueTag::None
        ? generator_send(coroutine, Value::none(), completed, yielded, error)
        : generator_throw(coroutine, &exception, 1, yielded, error);
    if (resumed) {
      if (!completed) {
        return handle_yield(runtime, self, fs, yielded, error);
      }
      if (task.must_cancel) {
        task.must_cancel = false;
        bool changed;
        return cancel_future(runtime, self, fs, fs.cancel_message, changed, error);
      }
      return finish_result(runtime, self, fs, std::move(yielded), error);
    }
    // generator.send()/throw() translates an unhandled coroutine exception
    // into the runtime's active exception slot. A bound Python method would
    // move the same exception into pending_exception; preserve that contract.
    if (value_as_instance(yielded) != nullptr) {
      runtime.set_pending_exception(std::move(yielded));
      error.clear();
    } else if (runtime.active_exception().tag != ValueTag::Invalid) {
      Value active = runtime.active_exception();
      runtime.clear_active_exception();
      runtime.set_pending_exception(std::move(active));
      error.clear();
    } else if (error.empty()) {
      return raise(runtime, "RuntimeError", "coroutine resume failed", error);
    } else {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  } else {
  if (call_method(runtime, coroutine,
                   exception.tag == ValueTag::None ? "send" : "throw",
                   &argument, 1, {}, yielded, error))
    return handle_yield(runtime, self, fs, yielded, error);
  }

  Value raised;
  if (!runtime.take_pending_exception(raised)) return false;
  error.clear();
  if (exception_is(runtime, raised, "StopIteration")) {
    Value result;
    if (!get_attr(runtime, raised, "value", result, error)) return false;
    if (task.must_cancel) {
      task.must_cancel = false;
      bool changed;
      return cancel_future(runtime, self, fs, fs.cancel_message, changed, error);
    }
    return finish_result(runtime, self, fs, std::move(result), error);
  }
  if (exception_is(runtime, raised, "CancelledError")) {
    fs.cancelled_exception = std::move(raised);
    bool changed;
    return cancel_future(runtime, self, fs, Value::none(), changed, error);
  }
  if (!finish_exception(runtime, self, fs, raised, error)) return false;
  if (exception_is(runtime, raised, "KeyboardInterrupt") ||
      exception_is(runtime, raised, "SystemExit")) {
    runtime.set_pending_exception(std::move(raised));
    return false;
  }
  return true;
}

bool step(Runtime& runtime, const Value& self, FutureState& fs,
            Value exception, std::string& error) {
  Value loop = fs.loop;
  if (!enter_task(runtime, loop, self, error)) return false;
  const bool ok = step_impl(runtime, self, fs, std::move(exception), error);
  if (ok) return leave_task(runtime, loop, self, error);
  // Cleanup must run even for KeyboardInterrupt/SystemExit or a failing
  // custom coroutine. Preserve the original exception through task restoration.
  Value raised;
  const bool has_exception = runtime.take_pending_exception(raised);
  std::string original_error = error;
  std::string cleanup_error;
  const bool left = leave_task(runtime, loop, self, cleanup_error);
  if (has_exception) {
    Value cleanup_exception;
    runtime.take_pending_exception(cleanup_exception);
    runtime.set_pending_exception(std::move(raised));
  }
  error = !original_error.empty() ? std::move(original_error) : std::move(cleanup_error);
  (void)left;
  return false;
}

bool step_method(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void* eager) {
  if (argc < 1 || argc > 2) return raise(runtime, "TypeError", "invalid Task step arguments", error);
  auto* fs = task_state(runtime, args[0], error);
  if (fs == nullptr) return false;
  Value exception = argc == 2 ? args[1] : Value::none();
  if (!(eager != nullptr ? step_impl(runtime, args[0], *fs, exception, error) :
                          step(runtime, args[0], *fs, exception, error))) return false;
  value_set_none(out);
  return true;
}

bool eager_step_method(Runtime& runtime, const Value* args, uint32_t argc,
                         Value& out, std::string& error, void*) {
  return step_method(runtime, args, argc, out, error, reinterpret_cast<void*>(1));
}

bool wakeup(Runtime& runtime, const Value* args, uint32_t argc,
              Value& out, std::string& error, void*) {
  if (argc != 2) return raise(runtime, "TypeError", "Task wakeup requires a Future", error);
  auto* fs = task_state(runtime, args[0], error);
  if (fs == nullptr || !awaited_by(runtime, args[1], args[0], false, error)) return false;
  Value result, exception = Value::none();
  bool exact_ok = false;
  const bool exact = exact_future(runtime, args[1], exact_ok, error);
  if (!exact_ok && value_as_instance(args[1]) != nullptr) return false;
  auto* waiter = exact ? state(runtime, args[1], error) : nullptr;
  const bool success = exact
      ? waiter != nullptr && future_result(runtime, args[1], *waiter, result, error)
      : call_method(runtime, args[1], "result", nullptr, 0, {}, result, error);
  if (!success) {
    if (!runtime.take_pending_exception(exception)) return false;
    error.clear();
  }
  if (!step(runtime, args[0], *fs, std::move(exception), error)) return false;
  value_set_none(out);
  return true;
}

bool register_native(Runtime& runtime, FutureState& fs,
                     std::string& error) {
  auto own = fs.task->registry;
  if (own == nullptr || fs.owner == nullptr)
    return raise(runtime, "RuntimeError", "uninitialized Task registry", error);
  auto& task = *fs.task;
  task.owner = fs.owner;
  Value task_value;
  task_value.tag = ValueTag::Object;
  task_value.flags = kXlangValueBorrowedRefFlag;
  task_value.as.obj = &task.owner->header;
  // CPython 3.14 tracks native Tasks in its intrusive task list and reserves
  // the Python WeakSet for third-party Task implementations. Keep native Task
  // registration entirely in the native list: allocating a weakref and
  // calling Python here costs several microseconds per task on async_tree.
  std::lock_guard<std::mutex> guard(own->mutex);
  task.previous = nullptr;
  task.next = own->task_head;
  if (own->task_head != nullptr) own->task_head->previous = &*fs.task;
  own->task_head = &task;
  task.registered = true;
  return true;
}

bool eager_start(Runtime& runtime, const Value& self, FutureState& fs,
                 std::string& error) {
  Value loop = fs.loop;
  Value previous;
  if (!swap_task(runtime, loop, self, previous, error)) return false;
  if (!register_native(runtime, fs, error)) return false;
  Value callback = step_callback(runtime, self, true);
  Value ignored;
  bool ok = call_method(runtime, fs.task->context, "run", &callback, 1, {}, ignored, error);
  Value raised;
  const bool has_exception = !ok && runtime.take_pending_exception(raised);
  std::string original_error = error;
  Value current;
  std::string cleanup_error;
  const bool restored = swap_task(runtime, loop, previous, current, cleanup_error);
  if (has_exception) {
    Value cleanup_exception;
    runtime.take_pending_exception(cleanup_exception);
    runtime.set_pending_exception(std::move(raised));
  }
  if (!restored && ok) {
    ok = false;
    error = cleanup_error;
  } else error = original_error;
  if (fs.completion != Completion::Pending) {
    // CPython releases the completed eager coroutine. Do not retain its VM
    // frame through a finished Task; suspended Tasks still own the coroutine.
    Value retired_coroutine = std::move(fs.task->coroutine);
    fs.task->coroutine = Value::none();
    if (!publish_edges(self, fs, error)) return false;
  }
  return ok;
}

bool init_kw(Runtime& runtime, const Value* args, uint32_t argc,
               const NativeKeywordArg* kwargs, uint32_t kwargc,
               Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) return raise(runtime, "TypeError", "Task() requires one coroutine", error);
  bool supplied_coroutine = argc == 2;
  Value coroutine = supplied_coroutine ? args[1] : Value::none();
  Value loop = Value::none(), name = Value::none(), context = Value::none(), eager = Value::boolean(false);
  bool seen[4] = {};
  for (uint32_t index = 0; index != kwargc; ++index) {
    const std::string_view keyword = kwargs[index].name == nullptr ? "" : kwargs[index].name;
    if (keyword == "coro") {
      if (supplied_coroutine) return raise(runtime, "TypeError", "multiple values for coro", error);
      supplied_coroutine = true;
      coroutine = *kwargs[index].value;
      continue;
    }
    const int slot = keyword == "loop" ? 0 : keyword == "name" ? 1 :
        keyword == "context" ? 2 : keyword == "eager_start" ? 3 : -1;
    if (slot < 0 || seen[slot]) return raise(runtime, "TypeError", "invalid Task keyword", error);
    seen[slot] = true;
    Value* targets[] = {&loop, &name, &context, &eager};
    *targets[slot] = *kwargs[index].value;
  }
  if (!supplied_coroutine) return raise(runtime, "TypeError", "Task() requires one coroutine", error);
  auto* fs = state(runtime, args[0], error, false);
  if (fs == nullptr || !initialize_future(runtime, args[0], *fs, loop, error)) return false;
  unregister(*fs);
  // Keep the payload address stable across reinitialization. User descriptor,
  // loop and coroutine hooks can invoke __init__ reentrantly while a native
  // step/cancel operation holds a reference to these fields.
  if (!fs->task.has_value()) fs->task.emplace();
  auto& task = *fs->task;
  Value retired[] = {std::move(task.waiter), std::move(task.coroutine),
                     std::move(task.context), std::move(task.name)};
  task.must_cancel = false;
  task.cancellation_requests = 0;
  task.waiter = task.coroutine = task.context = task.name = Value::none();
  task.log_destroy_pending = false;
  if (!publish_edges(args[0], *fs, error)) return false;
  const auto* generator = value_as_generator(coroutine);
  bool is_coroutine = generator != nullptr && generator->is_coroutine;
  if (!is_coroutine) {
    Value test, answer;
    if (!helper(runtime, "asyncio.coroutines", "iscoroutine", test, error) ||
        !runtime_call_callable(runtime, test, &coroutine, 1, answer, error) ||
        !runtime_truthy(runtime, answer, is_coroutine, error)) return false;
  }
  if (!is_coroutine)
    return raise(runtime, "TypeError", "a coroutine was expected, got " + value_to_string(coroutine), error);
  task.registry = registry(runtime, error);
  if (task.registry == nullptr) return false;
  if (context.tag == ValueTag::None && !copy_context(runtime, context, error)) return false;
  task.context = std::move(context);
  task.coroutine = std::move(coroutine);
  task.log_destroy_pending = true;
  if (name.tag == ValueTag::None) {
    std::lock_guard<std::mutex> guard(task.registry->mutex);
    // Defer "Task-N" string allocation until get_name/repr is requested.
    task.name = Value::int64(static_cast<int64_t>(task.registry->next_name++));
  } else if (value_as_string(name) != nullptr) task.name = std::move(name);
  else if (!runtime_call_callable(runtime, *runtime.find_builtin("str"), &name, 1, task.name, error)) return false;
  if (!publish_edges(args[0], *fs, error)) return false;
  bool start = false;
  if (!runtime_truthy(runtime, eager, start, error)) return false;
  if (start) {
    Value running;
    if (!call_method(runtime, fs->loop, "is_running", nullptr, 0, {}, running, error) ||
        !runtime_truthy(runtime, running, start, error)) return false;
  }
  if (start) {
    if (!eager_start(runtime, args[0], *fs, error)) return false;
  } else {
    if (!schedule_step(runtime, args[0], *fs, Value::none(), error)) return false;
    if (!register_native(runtime, *fs, error)) return false;
  }
  value_set_none(out);
  return true;
}

bool init(Runtime& runtime, const Value* args, uint32_t argc,
            Value& out, std::string& error, void* data) {
  return init_kw(runtime, args, argc, nullptr, 0, out, error, data);
}


bool cancel_kw(Runtime& runtime, const Value* args, uint32_t argc,
                 const NativeKeywordArg* kwargs, uint32_t kwargc,
                 Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2 || kwargc > 1 || (argc == 2 && kwargc != 0))
    return raise(runtime, "TypeError", "cancel() accepts at most one message", error);
  Value message = argc == 2 ? args[1] : Value::none();
  if (kwargc != 0) {
    if (kwargs[0].name == nullptr || std::string_view(kwargs[0].name) != "msg")
      return raise(runtime, "TypeError", "cancel() received an invalid keyword", error);
    message = *kwargs[0].value;
  }
  auto* fs = task_state(runtime, args[0], error);
  if (fs == nullptr) return false;
  fs->log_traceback = false;
  if (fs->completion != Completion::Pending) { out = Value::boolean(false); return true; }
  auto& task = *fs->task;
  ++task.cancellation_requests;
  Value waiter = task.waiter;
  if (waiter.tag != ValueTag::None) {
    Value accepted;
    if (!call_method(runtime, waiter, "cancel", &message, 1, {}, accepted, error)) return false;
    bool changed;
    if (!runtime_truthy(runtime, accepted, changed, error)) return false;
    if (changed) { out = Value::boolean(true); return true; }
  }
  task.must_cancel = true;
  Value previous_message = std::move(fs->cancel_message);
  fs->cancel_message = std::move(message);
  if (!publish_edges(args[0], *fs, error)) return false;
  out = Value::boolean(true);
  return true;
}

bool cancel(Runtime& runtime, const Value* args, uint32_t argc,
              Value& out, std::string& error, void* data) {
  return cancel_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool forbidden_set(Runtime& runtime, const Value*, uint32_t argc,
                     Value&, std::string& error, void* exception) {
  if (argc != 2) return raise(runtime, "TypeError", "expected one argument", error);
  return raise(runtime, "RuntimeError", exception == nullptr
      ? "Task does not support set_result operation"
      : "Task does not support set_exception operation", error);
}

enum class Field : intptr_t { Waiter, Coroutine, Context, Name, MustCancel, LogDestroy, Cancels };

bool field_get(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* data) {
  if (argc != 1) return raise(runtime, "TypeError", "expected self", error);
  auto* fs = task_state(runtime, args[0], error);
  if (fs == nullptr) return false;
  auto& task = *fs->task;
  switch (static_cast<Field>(reinterpret_cast<intptr_t>(data))) {
    case Field::Waiter: out = task.waiter; break;
    case Field::Coroutine: out = task.coroutine; break;
    case Field::Context: out = task.context; break;
    case Field::Name:
      if (task.name.tag == ValueTag::Int64) {
        task.name = Value::string("Task-" + std::to_string(task.name.as.i64));
        if (!publish_edges(args[0], *fs, error)) return false;
      }
      out = task.name;
      break;
    case Field::MustCancel: out = Value::boolean(task.must_cancel); break;
    case Field::LogDestroy: out = Value::boolean(task.log_destroy_pending); break;
    case Field::Cancels: out = Value::int64(static_cast<int64_t>(task.cancellation_requests)); break;
  }
  return true;
}

bool log_destroy_set(Runtime& runtime, const Value* args, uint32_t argc,
                       Value& out, std::string& error, void*) {
  if (argc != 2) return raise(runtime, "TypeError", "expected self and value", error);
  auto* fs = task_state(runtime, args[0], error);
  if (fs == nullptr || !runtime_truthy(runtime, args[1], fs->task->log_destroy_pending, error)) return false;
  value_set_none(out);
  return true;
}

bool set_name(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void*) {
  if (argc != 2) return raise(runtime, "TypeError", "set_name() requires one name", error);
  auto* fs = task_state(runtime, args[0], error);
  if (fs == nullptr) return false;
  Value text;
  if (!runtime_call_callable(runtime, *runtime.find_builtin("str"), &args[1], 1, text, error)) return false;
  Value previous_name = std::move(fs->task->name);
  fs->task->name = std::move(text);
  if (!publish_edges(args[0], *fs, error)) return false;
  value_set_none(out);
  return true;
}

bool uncancel(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void*) {
  if (argc != 1) return raise(runtime, "TypeError", "uncancel() takes no arguments", error);
  auto* fs = task_state(runtime, args[0], error);
  if (fs == nullptr) return false;
  auto& task = *fs->task;
  if (task.cancellation_requests != 0 && --task.cancellation_requests == 0) task.must_cancel = false;
  out = Value::int64(static_cast<int64_t>(task.cancellation_requests));
  return true;
}

bool repr(Runtime& runtime, const Value* args, uint32_t argc,
             Value& out, std::string& error, void*) {
  if (argc != 1) return raise(runtime, "TypeError", "Task.__repr__ requires self", error);
  Value function;
  return helper(runtime, "asyncio.base_tasks", "_task_repr", function, error) &&
      runtime_call_callable(runtime, function, args, argc, out, error);
}

bool stack_kw(Runtime& runtime, const Value* args, uint32_t argc,
                 const NativeKeywordArg* kwargs, uint32_t kwargc,
                 Value& out, std::string& error, void* printing) {
  if (argc != 1) return raise(runtime, "TypeError", "stack methods accept keyword arguments", error);
  Value limit = Value::none(), file = Value::none();
  bool seen_limit = false, seen_file = false;
  for (uint32_t index = 0; index != kwargc; ++index) {
    const std::string_view name = kwargs[index].name == nullptr ? "" : kwargs[index].name;
    if (name == "limit" && !seen_limit) { limit = *kwargs[index].value; seen_limit = true; }
    else if (name == "file" && printing != nullptr && !seen_file) { file = *kwargs[index].value; seen_file = true; }
    else return raise(runtime, "TypeError", "invalid stack keyword", error);
  }
  Value function;
  if (!helper(runtime, "asyncio.base_tasks", printing != nullptr
      ? "_task_print_stack" : "_task_get_stack", function, error)) return false;
  Value arguments[] = {args[0], limit, file};
  return runtime_call_callable(runtime, function, arguments, printing != nullptr ? 3 : 2, out, error);
}

bool stack(Runtime& runtime, const Value* args, uint32_t argc,
              Value& out, std::string& error, void* printing) {
  return stack_kw(runtime, args, argc, nullptr, 0, out, error, printing);
}

bool finalize(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void*) {
  value_set_none(out);
  if (argc != 1) return true;
  auto* fs = static_cast<FutureState*>(instance_get_native_data(args[0], kFutureData));
  if (fs == nullptr || !fs->task.has_value() || fs->loop.tag == ValueTag::None) return true;
  if (fs->completion == Completion::Pending && fs->task->log_destroy_pending) {
    fs->task->log_destroy_pending = false;
    std::vector<std::pair<Value, Value>> entries{
        {Value::string("message"), Value::string("Task was destroyed but it is pending!")},
        {Value::string("task"), args[0]}};
    if (fs->source_traceback.tag != ValueTag::None)
      entries.emplace_back(Value::string("source_traceback"), fs->source_traceback);
    Value context = Value::dict(std::move(entries));
    if (!call_method(runtime, fs->loop, "call_exception_handler", &context, 1, {}, out, error)) return false;
  }
  // Let the native Future finalizer report an unconsumed Task exception too.
  const Value* destructor = runtime.find_native_symbol("_asyncio.Future.__del__");
  if (destructor == nullptr) return true;
  return runtime_call_callable(runtime, *destructor, args, 1, out, error);
}

} // namespace

Value task_class(Runtime& runtime, const Value& future_base) {
  runtime.make_native_function("_asyncio.Task._step", step_method);
  runtime.make_native_function("_asyncio.Task._eager_step", eager_step_method);
  runtime.make_native_function("_asyncio.Task._wakeup", wakeup);
  std::vector<std::pair<std::string, Value>> attributes{
      {"__module__", Value::string("_asyncio")},
      {"__init__", runtime.make_native_function("_asyncio.Task.__init__", init, nullptr, nullptr, nullptr, false, init_kw)},
      {"__repr__", runtime.make_native_function("_asyncio.Task.__repr__", repr)},
      {"__del__", runtime.make_native_function("_asyncio.Task.__del__", finalize)},
      {"cancel", runtime.make_native_function("_asyncio.Task.cancel", cancel, nullptr, nullptr, nullptr, false, cancel_kw)},
      {"uncancel", runtime.make_native_function("_asyncio.Task.uncancel", uncancel)},
      {"set_result", runtime.make_native_function("_asyncio.Task.set_result", forbidden_set)},
      {"set_exception", runtime.make_native_function("_asyncio.Task.set_exception", forbidden_set, reinterpret_cast<void*>(1))},
      {"set_name", runtime.make_native_function("_asyncio.Task.set_name", set_name)},
      {"get_stack", runtime.make_native_function("_asyncio.Task.get_stack", stack, nullptr, nullptr, nullptr, false, stack_kw)},
      {"print_stack", runtime.make_native_function("_asyncio.Task.print_stack", stack, reinterpret_cast<void*>(1), nullptr, nullptr, false, stack_kw)}};
  const std::pair<const char*, Field> getters[] = {
      {"get_coro", Field::Coroutine}, {"get_context", Field::Context},
      {"get_name", Field::Name}, {"cancelling", Field::Cancels}};
  for (const auto& [name, field] : getters)
    attributes.emplace_back(name, runtime.make_native_function(std::string("_asyncio.Task.") + name,
        field_get, reinterpret_cast<void*>(static_cast<intptr_t>(field))));
  const std::pair<const char*, Field> fields[] = {
      {"_fut_waiter", Field::Waiter}, {"_coro", Field::Coroutine},
      {"_must_cancel", Field::MustCancel}, {"_log_destroy_pending", Field::LogDestroy}};
  for (const auto& [name, field] : fields) {
    Value getter = runtime.make_native_function(std::string("_asyncio.Task.") + name + ".get",
        field_get, reinterpret_cast<void*>(static_cast<intptr_t>(field)));
    Value setter = field == Field::LogDestroy
        ? runtime.make_native_function("_asyncio.Task._log_destroy_pending.set", log_destroy_set)
        : Value::none();
    attributes.emplace_back(name, Value::property(getter, setter, Value::none(), Value::none()));
  }
  Value task = Value::class_object("Task", std::move(attributes), future_base);
  return task;
}

} // namespace xlang3::asyncio_native
