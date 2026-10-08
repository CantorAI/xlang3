#pragma once

#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"

#include <memory>
#include <sstream>
#include <utility>

namespace xlang3::test {

namespace hash_exception_cases {

struct CallbackState {
  Value exception;
  bool publish_exception = true;
  uint32_t calls = 0;
};

inline bool fail_hash(Runtime& runtime, const Value*, uint32_t argc,
                      Value&, std::string& error, void* user_data) {
  auto& state = *static_cast<CallbackState*>(user_data);
  if (argc != 1) {
    error = "unexpected native __hash__ arity";
    return false;
  }
  ++state.calls;
  error = "native hash diagnostic";
  if (state.publish_exception) runtime.set_pending_exception(state.exception);
  return false;
}

inline void destroy_state(void* user_data) {
  delete static_cast<CallbackState*>(user_data);
}

} // namespace hash_exception_cases

inline void check_hash_exception_cases(CaseResult& result) {
  std::ostringstream output;
  Runtime runtime(output);
  const Value* registered_hash = runtime.find_builtin("hash");
  auto* native_hash = registered_hash == nullptr
      ? nullptr : value_as_native_function(*registered_hash);
  expect_true(result, native_hash != nullptr && native_hash->fast_callback != nullptr,
              "hash exception checks require the ordinary and fast builtin callbacks");
  if (native_hash == nullptr || native_hash->fast_callback == nullptr) return;
  Value builtin_hash = *registered_hash;
  Value original_exception = runtime.make_exception("ValueError", "hash marker");
  auto state_owner = std::make_unique<hash_exception_cases::CallbackState>();
  state_owner->exception = original_exception;
  auto* state = state_owner.get();
  Value method = runtime.make_native_function(
      "hash_exception_test.__hash__", hash_exception_cases::fail_hash,
      state_owner.get(), hash_exception_cases::destroy_state);
  state_owner.release();
  Value klass = Value::class_object("HashExceptionTest", {{"__hash__", method}});
  Value key = Value::instance(klass);
  Value arguments[1] = {key};
  const uint32_t argument_registers[1] = {0};
  const uint32_t original_refs = original_exception.as.obj->refcnt.load();

  for (bool fast : {false, true}) {
    for (uint32_t repeat = 0; repeat != 12; ++repeat) {
      std::string error;
      Value out = Value::int64(887);
      const bool ok = fast
          ? native_hash->fast_callback(runtime, nullptr, 0, arguments,
                argument_registers, 1, out, error, native_hash->user_data)
          : runtime_call_callable(runtime, builtin_hash, arguments, 1, out, error);
      Value pending;
      const bool has_pending = runtime.take_pending_exception(pending);
      expect_true(result, !ok && has_pending && value_is(pending, original_exception),
                  "hash must preserve the exact native callback exception object");
      expect_true(result, out.tag == ValueTag::Int64 && out.as.i64 == 887,
                  "a failed hash must not publish or overwrite a result");
      expect_true(result, error == "native hash diagnostic",
                  "hash must retain diagnostic text without replacing the exception");
      value_set_invalid(pending);
      expect_true(result, original_exception.as.obj->refcnt.load() == original_refs,
                  "repeated pending exception transfer must release its temporary owner");
      Value extra;
      expect_true(result, !runtime.take_pending_exception(extra),
                  "draining the failed hash must leave no second pending owner");
    }
  }
  expect_true(result, state->calls == 24,
              "hash failure must invoke the callback exactly once per call");

  // A callback failing only with text must still receive the native API's
  // TypeError fallback. The state is owned by the native function, including
  // every early assertion-return/unwind path; no callback holds stack counters.
  state->publish_exception = false;
  std::string error;
  Value out = Value::int64(887);
  const bool text_ok = runtime_call_callable(runtime, builtin_hash, arguments, 1, out, error);
  Value fallback;
  const bool has_fallback = runtime.take_pending_exception(fallback);
  expect_true(result, !text_ok && has_fallback &&
                  value_is(runtime.exception_type(fallback), *runtime.find_builtin("TypeError")),
              "a hash callback failure without an exception must retain TypeError fallback");
  value_set_invalid(fallback);

  arguments[0] = Value::string("SELECT");
  error.clear();
  expect_true(result, runtime_call_callable(runtime, builtin_hash, arguments, 1, out, error) &&
                  out.tag == ValueTag::Int64,
              "a successful hash must recover after prior exception transfers");
  Value unexpected;
  expect_true(result, !runtime.take_pending_exception(unexpected),
              "successful hashing must not leave an exception owner");
}

} // namespace xlang3::test
