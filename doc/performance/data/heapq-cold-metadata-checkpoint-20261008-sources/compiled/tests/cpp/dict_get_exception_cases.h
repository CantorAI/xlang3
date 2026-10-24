#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include <memory>

namespace xlang3::test {
namespace dict_get_exception_cases {
struct State {
  Value exception;
  std::string diagnostic;
  bool publish = true;
  bool hash_succeeds = false;
  uint32_t calls = 0;
  uint32_t equality_calls = 0;
};
inline bool hash_failure(Runtime& runtime, const Value*, uint32_t argc,
    Value& out, std::string& error, void* context) {
  auto& state = **static_cast<std::shared_ptr<State>*>(context);
  if (argc != 1) { error = "unexpected dict hash callback arity"; return false; }
  ++state.calls;
  if (state.hash_succeeds) { error.clear(); out = Value::int64(7); return true; }
  error = state.diagnostic;
  if (state.publish) runtime.set_pending_exception(state.exception);
  return false;
}
inline bool equality_failure(Runtime& runtime, const Value*, uint32_t argc,
    Value&, std::string& error, void* context) {
  auto& state = **static_cast<std::shared_ptr<State>*>(context);
  if (argc != 2) { error = "unexpected dict equality callback arity"; return false; }
  ++state.equality_calls;
  error = "key not found";
  runtime.set_pending_exception(state.exception);
  return false;
}
inline void destroy_state(void* context) { delete static_cast<std::shared_ptr<State>*>(context); }
inline Value make_callback(Runtime& runtime, const char* name, NativeFunctionCallback callback,
    const std::shared_ptr<State>& state) {
  auto context = std::make_unique<std::shared_ptr<State>>(state);
  Value function = runtime.make_native_function(name, callback, context.get(), destroy_state);
  context.release();
  return function;
}
} // namespace dict_get_exception_cases

inline void check_dict_get_exception_cases(CaseResult& result) {
  std::ostringstream output;
  Runtime runtime(output);
  const Value* builtin_dict = runtime.find_builtin("dict");
  auto* klass = builtin_dict == nullptr ? nullptr : value_as_class(*builtin_dict);
  expect_true(result, klass != nullptr, "dict exception checks require registered native dict class");
  if (klass == nullptr) return;
  auto found = klass->attrs.find("get");
  expect_true(result, found != klass->attrs.end(), "registered dict exposes its real get callback");
  if (found == klass->attrs.end()) return;
  Value get_function = found->second;
  auto* native = value_as_native_function(get_function);
  expect_true(result, native != nullptr && native->fast_callback != nullptr,
      "dict.get must expose ordinary and fast native paths");
  if (native == nullptr || native->fast_callback == nullptr) return;
  Value original = runtime.make_exception("LookupError", "dict hash marker");
  auto state = std::make_shared<dict_get_exception_cases::State>();
  state->exception = original;
  Value method = dict_get_exception_cases::make_callback(runtime, "dict_get_exception.__hash__",
      dict_get_exception_cases::hash_failure, state);
  Value key_class = Value::class_object("DictGetExceptionKey", {{"__hash__", method}});
  Value key = Value::instance(key_class);
  Value table = Value::dict({});
  Value args[] = {table, key, Value::int64(903)};
  Value registers[] = {key, Value::int64(903)};
  const uint32_t register_args[] = {0, 1};
  const auto references = original.as.obj->refcnt.load();
  for (const char* diagnostic : {"native hash failure", "key not found"}) {
    state->diagnostic = diagnostic;
    for (bool fast : {false, true}) {
      for (uint32_t repeat = 0; repeat < 12; ++repeat) {
        Value out = Value::int64(887);
        std::string error;
        const bool ok = fast ? native->fast_callback(runtime, &table, 1, registers,
            register_args, 2, out, error, native->user_data)
            : runtime_call_callable(runtime, get_function, args, 3, out, error);
        Value pending;
        expect_true(result, !ok && runtime.take_pending_exception(pending) && value_is(pending, original),
            "dict.get preserves exact callback exception even when diagnostic resembles a key miss");
        expect_true(result, out.tag == ValueTag::Int64 && out.as.i64 == 887 && error == diagnostic,
            "failed dict.get preserves output and diagnostic without returning default");
        value_set_invalid(pending);
        expect_true(result, original.as.obj->refcnt.load() == references,
            "dict pending transfer releases each temporary exception owner");
        Value extra;
        expect_true(result, !runtime.take_pending_exception(extra), "dict failure leaves no second pending owner");
      }
    }
  }
  expect_true(result, state->calls == 48, "dict failure invokes native hash exactly once, without lookup retry");
  state->publish = false;
  state->diagnostic = "native hash text only";
  Value out = Value::int64(887);
  std::string error;
  const bool ok = runtime_call_callable(runtime, get_function, args, 3, out, error);
  Value fallback;
  expect_true(result, !ok && runtime.take_pending_exception(fallback) &&
      value_is(runtime.exception_type(fallback), *runtime.find_builtin("TypeError")),
      "text-only dict hash failure retains TypeError fallback");
  value_set_invalid(fallback);
  args[1] = Value::string("missing");
  error.clear();
  expect_true(result, runtime_call_callable(runtime, get_function, args, 3, out, error) &&
      out.tag == ValueTag::Int64 && out.as.i64 == 903,
      "real missing keys still return the supplied default after failure recovery");
  Value unexpected;
  expect_true(result, !runtime.take_pending_exception(unexpected), "successful dict miss has no pending exception");
  state->hash_succeeds = true;
  state->publish = true;
  Value equality = dict_get_exception_cases::make_callback(runtime, "dict_get_exception.__eq__",
      dict_get_exception_cases::equality_failure, state);
  Value collision_class = Value::class_object("DictGetEqualityKey", {{"__hash__", method}, {"__eq__", equality}});
  Value collision_key = Value::instance(collision_class);
  std::string payload_error;
  expect_true(result, object_set_attr(collision_key, "_value_", Value::int64(7), payload_error),
      "install int-like Instance payload without changing its equality callback");
  Value collision_table = Value::dict({{collision_key, Value::int64(19)}});
  Value collision_args[] = {collision_table, Value::int64(7), Value::int64(903)};
  Value collision_registers[] = {Value::int64(7), Value::int64(903)};
  for (bool fast : {false, true}) {
    const auto before = state->equality_calls;
    out = Value::int64(887);
    error.clear();
    const bool equal_ok = fast ? native->fast_callback(runtime, &collision_table, 1,
        collision_registers, register_args, 2, out, error, native->user_data)
        : runtime_call_callable(runtime, get_function, collision_args, 3, out, error);
    Value pending;
    expect_true(result, !equal_ok && state->equality_calls == before + 1 &&
        runtime.take_pending_exception(pending) && value_is(pending, original),
        "exact-int query invokes stored Python-key equality once and preserves its original failure");
    expect_true(result, error == "key not found" && out.tag == ValueTag::Int64 && out.as.i64 == 887,
        "equality callback text must not become a default result or overwrite output");
    value_set_invalid(pending);
  }
}
} // namespace xlang3::test
