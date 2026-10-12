#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include <memory>

namespace xlang3::test {
namespace observable_builtin_method_cases {
struct State {
  std::vector<std::string> events;
  Object* expected_self = nullptr;
  const ir::Module* expected_module = nullptr;
  uint32_t expected_function = 1;
  Value native_exception;
  Value observer_exception;
  std::string fail_event;
  unsigned hash_calls = 0;
  unsigned receiver_cleanup = 0;
  bool hash_fails = false;
  bool replace_pending = false;
  bool collect_in_observer = false;
  bool identity_ok = true;
  bool receiver_alive = true;
  bool observer_saw_native_pending = false;
};
inline void destroy_context(void* context) {
  delete static_cast<std::shared_ptr<State>*>(context);
}
inline void receiver_cleanup(void* context) {
  ++(**static_cast<std::shared_ptr<State>*>(context)).receiver_cleanup;
  destroy_context(context);
}
inline bool lifetime_token(Runtime&, const Value*, uint32_t, Value& out,
    std::string& error, void*) {
  error.clear(); out = Value::none(); return true;
}
inline bool hash_key(Runtime& runtime, const Value*, uint32_t argc, Value& out,
    std::string& error, void* context) {
  auto& state = **static_cast<std::shared_ptr<State>*>(context);
  if (argc != 1) { error = "unexpected observer hash arity"; return false; }
  ++state.hash_calls;
  if (state.hash_fails) {
    runtime.set_pending_exception(state.native_exception);
    error = "native hash marker";
    return false;
  }
  error.clear(); out = Value::int64(7); return true;
}
inline bool profile(Runtime& runtime, const Value* args, uint32_t argc,
    Value& out, std::string& error, void* context) {
  auto& state = **static_cast<std::shared_ptr<State>*>(context);
  auto* event = argc == 3 ? value_as_string(args[1]) : nullptr;
  const std::string event_name = event == nullptr ? "invalid" : std::string(string_object_view(*event));
  // The public interpreter also emits Python call/return/exception events.
  // This proof checks the native get events inside that real DLL activation.
  if (event_name == "call" || event_name == "return" || event_name == "exception") {
    error.clear(); out = Value::none(); return true;
  }
  Value name;
  std::string name_error;
  if (argc != 3 || !object_get_attr(args[2], "__name__", name, name_error) ||
      value_as_string(name) == nullptr || string_object_view(*value_as_string(name)) != "get") {
    error.clear(); out = Value::none(); return true;
  }
  auto* frame = argc == 3 ? value_as_frame(args[0]) : nullptr;
  auto* bound = argc == 3 ? value_as_bound_method(args[2]) : nullptr;
  state.events.push_back(event_name);
  state.identity_ok &= frame != nullptr && frame->module.get() == state.expected_module &&
      frame->function_id == state.expected_function && bound != nullptr &&
      bound->self.tag == ValueTag::Object && bound->self.as.obj == state.expected_self;
  state.receiver_alive &= state.receiver_cleanup == 0;
  if (state.collect_in_observer) {
    Value gc, collect, ignored;
    if (!runtime.import_module("gc", gc, error) ||
        !module_get_attr(gc, "collect", collect, error) ||
        !runtime_call_callable(runtime, collect, nullptr, 0, ignored, error)) return false;
    state.receiver_alive &= state.receiver_cleanup == 0;
  }
  if (state.replace_pending && event_name == "c_exception") {
    Value pending;
    state.observer_saw_native_pending = runtime.take_pending_exception(pending) &&
        value_is(pending, state.native_exception);
    runtime.set_pending_exception(state.observer_exception);
    Value drained;
    if (!runtime.take_pending_exception(drained) ||
        !value_is(drained, state.observer_exception)) {
      error = "observer could not drain its temporary pending owner"; return false;
    }
    runtime.set_pending_exception(state.observer_exception);
  }
  if (state.fail_event == event_name) {
    runtime.set_pending_exception(state.observer_exception);
    error = "profile marker";
    return false;
  }
  error.clear(); out = Value::none(); return true;
}
inline Value callback(const char* name, NativeFunctionCallback function,
    const std::shared_ptr<State>& state) {
  auto context = std::make_unique<std::shared_ptr<State>>(state);
  Value result = Value::native_function(0, name, function, context.get(), destroy_context);
  context.release();
  return result;
}
struct ResetRuntimeView {
  Runtime& runtime;
  ~ResetRuntimeView() {
    runtime.set_profile_function(Value::none());
    runtime.clear_current_frame();
  }
};
} // namespace observable_builtin_method_cases

inline void check_observable_builtin_method_cases(CaseResult& result) {
  using namespace observable_builtin_method_cases;
  // Execute the DLL's actual opcode dispatcher through an already exported
  // Interpreter entry. Never instantiate private VM/monitor/lock templates in
  // the test executable: that would either fail linking or test another state.
  auto mutable_module = std::make_shared<ir::Module>();
  mutable_module->source_file = "<observable-native-cpp>";
  mutable_module->global_slots = {"receiver", "key", "default"};
  mutable_module->functions.resize(3);
  mutable_module->functions[0].name = "<module>";
  for (uint32_t id : {1u, 2u}) {
    auto& fn = mutable_module->functions[id];
    fn.name = id == 1 ? "observable_invoke" : "observable_alias";
    fn.register_count = 4;
    fn.names = {"get"};
    fn.call_args = {{1, 2}};
    fn.code = {{ir::Op::LoadModuleSlot, 0, 0, 0, 0},
        {ir::Op::LoadModuleSlot, 1, 1, 0, 0},
        {ir::Op::LoadModuleSlot, 2, 2, 0, 0}};
    if (id == 2) fn.code.push_back({ir::Op::DeleteModuleSlot, 0, 0, 0, 0});
    fn.code.push_back({ir::Op::CallMethod, 0, 0, 0, 0});
    fn.code.push_back({ir::Op::Return, 0, 0, 0, 0});
  }
  std::shared_ptr<const ir::Module> module = mutable_module;
  Value globals = Value::module("observable_native_cpp");
  std::ostringstream output;
  Runtime runtime(output);
  ResetRuntimeView reset{runtime};
  Interpreter interpreter(runtime);
  std::string error;
  const bool slots_ready = module_ensure_attr_slots(globals, module->global_slots, error);
  expect_true(result, slots_ready, "observer proof binds its real module slots through the public API");
  if (!slots_ready) return;
  Value functions[2] = {Value::function(1, {}, globals, module), Value::function(2, {}, globals, module)};
  auto invoke = [&](uint32_t id) {
    return interpreter.run_function_value(value_as_function(functions[id - 1]), CallArgsView{});
  };
  auto install = [&](const char* name, const Value& value) {
    error.clear();
    const bool ok = module_set_attr(globals, name, value, error);
    expect_true(result, ok, std::string("observer proof installs ") + name);
    return ok;
  };
  auto state = std::make_shared<State>();
  state->expected_module = module.get();
  state->native_exception = runtime.make_exception("LookupError", "native marker");
  state->observer_exception = runtime.make_exception("LookupError", "observer marker");
  Value hook = callback("observable_profile", profile, state);
  Value table = Value::dict({{Value::string("present"), Value::int64(17)}});
  state->expected_self = table.as.obj;
  if (!install("receiver", table) || !install("default", Value::int64(903))) return;
  runtime.set_profile_function(hook);
  RuntimeResult answer;
  for (const char* key : {"present", "missing"}) {
    if (!install("key", Value::string(key))) return;
    answer = invoke(1);
    expect_true(result, answer.errors.empty() && answer.exception.tag == ValueTag::Invalid &&
        answer.value.tag == ValueTag::Int64 && answer.value.as.i64 == (std::string(key) == "present" ? 17 : 903),
        "profiled DLL get retains hit/default semantics");
  }
  expect_true(result, state->events == std::vector<std::string>{"c_call", "c_return", "c_call", "c_return"} &&
      state->identity_ok && state->receiver_alive,
      "profile sees owning real bound get and its actual DLL caller for success/return");

  // Move the receiver into the namespace, drop its host owner, then let the
  // real IR load-and-delete remove the namespace owner before dst==receiver.
  // Only the VM operand and the observed bound callable can now retain it.
  state->events.clear(); state->collect_in_observer = true; state->expected_function = 2;
  auto token_context = std::make_unique<std::shared_ptr<State>>(state);
  Value token = Value::native_function(0, "observable_receiver_token", lifetime_token,
      token_context.get(), receiver_cleanup);
  token_context.release();
  table = Value::dict({{Value::string("present"), Value::int64(17)},
      {Value::string("lifetime"), std::move(token)}});
  state->expected_self = table.as.obj;
  if (!install("receiver", table) || !install("key", Value::string("present"))) return;
  value_set_invalid(table);
  answer = invoke(2);
  expect_true(result, answer.errors.empty() && answer.value.tag == ValueTag::Int64 && answer.value.as.i64 == 17 &&
      state->events == std::vector<std::string>{"c_call", "c_return"} && state->identity_ok &&
      state->receiver_alive && state->receiver_cleanup == 1,
      "DLL dst/receiver alias owns self through observer GC and releases its last owner after events");
  state->collect_in_observer = false; state->receiver_cleanup = 0; state->expected_function = 1;

  Value hash = callback("ObservableKey.__hash__", hash_key, state);
  Value key_class = Value::class_object("ObservableKey", {{"__hash__", hash}});
  Value key = Value::instance(key_class);
  table = Value::dict({}); state->expected_self = table.as.obj;
  if (!install("receiver", table) || !install("key", key)) return;
  state->events.clear(); state->hash_calls = 0; state->hash_fails = true; state->replace_pending = true;
  answer = invoke(1);
  expect_true(result, !answer.errors.empty() && value_is(answer.exception, state->native_exception) &&
      state->hash_calls == 1 && state->events == std::vector<std::string>{"c_call", "c_exception"} &&
      !state->observer_saw_native_pending && state->identity_ok,
      "DLL failure pins native pending before observer replacement/drain and returns original exception identity");
  Value pending;
  expect_true(result, runtime.take_pending_exception(pending) && value_is(pending, state->native_exception),
      "public DLL entry transports one pending owner equal to its returned native exception");
  value_set_invalid(pending);
  expect_true(result, !runtime.take_pending_exception(pending), "DLL native failure leaves no second pending owner");
  state->hash_fails = false; state->replace_pending = false;
  for (const char* failed_event : {"c_call", "c_return"}) {
    runtime.set_profile_function(hook);
    state->fail_event = failed_event; state->events.clear(); state->hash_calls = 0;
    answer = invoke(1);
    expect_true(result, !answer.errors.empty() && value_is(answer.exception, state->observer_exception) &&
        state->hash_calls == (state->fail_event == "c_call" ? 0u : 1u),
        "DLL observer failure preserves exact pending identity without retry or get after c_call failure");
    expect_true(result, runtime.take_pending_exception(pending) && value_is(pending, state->observer_exception),
        "public DLL entry transports exactly its returned observer exception identity");
    value_set_invalid(pending);
    expect_true(result, !runtime.take_pending_exception(pending), "DLL observer failure transfers its pending owner once");
  }
  state->fail_event.clear(); state->events.clear();
  runtime.set_profile_function(Value::none()); state->hash_calls = 0;
  answer = invoke(1);
  expect_true(result, answer.errors.empty() && answer.value.tag == ValueTag::Int64 && answer.value.as.i64 == 903 &&
      state->hash_calls == 1 && state->events.empty(),
      "disabled profiling retains ordinary DLL native get without C events");
  // Exception tracebacks can retain the module namespace. Remove its callback
  // edges while all heap contexts remain valid, including failed-probe unwind.
  error.clear(); (void)module_delete_attr(globals, "receiver", error);
  error.clear(); (void)module_delete_attr(globals, "key", error);
  error.clear(); (void)module_delete_attr(globals, "default", error);
  value_set_invalid(hook);
}
} // namespace xlang3::test
