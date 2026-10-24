#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include <memory>

namespace xlang3::test {
namespace scoped_sorted_key_cases {
struct State {
  unsigned calls = 0;
  unsigned cleanup = 0;
  bool independent_namespace = true;
  bool previous_owner_retired = true;
  bool active_preserved = true;
  Value outer;
  Value failure;
};
inline void context_cleanup(void* pointer) {
  delete static_cast<std::shared_ptr<State>*>(pointer);
}
inline void token_cleanup(void* pointer) {
  ++(**static_cast<std::shared_ptr<State>*>(pointer)).cleanup;
  context_cleanup(pointer);
}
inline bool token(Runtime&, const Value*, uint32_t, Value& out,
    std::string& error, void*) {
  error.clear(); out = Value::none(); return true;
}
inline bool key_probe(Runtime& runtime, const Value* args, uint32_t argc,
    Value& out, std::string& error, void* pointer) {
  auto& state = **static_cast<std::shared_ptr<State>*>(pointer);
  if (argc != 2 || args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64) {
    error = "invalid scoped sorted key probe arguments"; return false;
  }
  state.independent_namespace &= args[0].as.i64 == 0;
  state.previous_owner_retired &= state.cleanup == state.calls;
  state.active_preserved &= value_is(runtime.active_exception(), state.outer);
  ++state.calls;
  auto context = std::make_unique<std::shared_ptr<State>>(
      *static_cast<std::shared_ptr<State>*>(pointer));
  out = Value::native_function(0, "scoped_sorted_owner", token,
      context.get(), token_cleanup);
  context.release();
  error.clear(); return true;
}
inline bool key_failure(Runtime& runtime, const Value*, uint32_t argc,
    Value&, std::string& error, void* pointer) {
  auto& state = **static_cast<std::shared_ptr<State>*>(pointer);
  if (argc != 0) { error = "invalid sorted key failure arity"; return false; }
  runtime.set_pending_exception(state.failure);
  error = "scoped sorted key original marker";
  return false;
}
inline Value native(const char* name, NativeFunctionCallback callback,
    const std::shared_ptr<State>& state) {
  auto context = std::make_unique<std::shared_ptr<State>>(state);
  Value result = Value::native_function(0, name, callback, context.get(), context_cleanup);
  context.release(); return result;
}
inline std::shared_ptr<const ir::Module> key_module(const std::shared_ptr<State>& state,
    bool store, bool erase, bool fail) {
  auto module = std::make_shared<ir::Module>();
  module->source_file = "scoped_sorted_key_public_api.py";
  module->functions.emplace_back();
  auto& function = module->functions[0];
  function.name = "scoped_key";
  function.params = {"value"}; function.locals = {"value"};
  function.names = {"scoped_key_leak"};
  function.register_count = 7;
  function.constants = {Value::int64(0), Value::int64(1),
      native("scoped_key_probe", key_probe, state),
      native("scoped_key_failure", key_failure, state)};
  function.call_args = {{1, 3}, {}};
  // Real SDK/legacy IR, without a module namespace. The exception handler
  // distinguishes an absent fresh fallback global from a previous key's leak.
  // Native probe reads owning cleanup state before producing the next token.
  function.code = {
      {ir::Op::SetupExcept, 5, 0, 0, 0},
      {ir::Op::LoadGlobal, 0, 0, 0, 0},
      {ir::Op::PopExcept, 0, 0, 0, 0},
      {ir::Op::LoadConst, 1, 1, 0, 0},
      {ir::Op::Jump, 7, 0, 0, 0},
      {ir::Op::ClearException, 0, 0, 0, 0},
      {ir::Op::LoadConst, 1, 0, 0, 0},
      {ir::Op::LoadConst, 2, 2, 0, 0},
      {ir::Op::LoadLocal, 3, 0, 0, 0},
      {ir::Op::Call, 4, 2, 0, 0}};
  if (store) function.code.push_back({ir::Op::StoreGlobal, 0, 4, 0, 0});
  if (erase) function.code.push_back({ir::Op::DeleteGlobal, 0, 0, 0, 0});
  if (fail) {
    function.code.push_back({ir::Op::LoadConst, 5, 3, 0, 0});
    function.code.push_back({ir::Op::Call, 6, 5, 1, 0});
  }
  function.code.push_back({ir::Op::LoadLocal, 5, 0, 0, 0});
  function.code.push_back({ir::Op::Return, 0, 5, 0, 0});
  return module;
}
} // namespace scoped_sorted_key_cases

inline void check_scoped_sorted_key_cases(CaseResult& result) {
  using namespace scoped_sorted_key_cases;
  std::ostringstream output;
  Runtime runtime(output);
  const Value* sorted_builtin = runtime.find_builtin("sorted");
  expect_true(result, sorted_builtin != nullptr, "sorted must be registered in the actual runtime");
  if (sorted_builtin == nullptr) return;
  Value sorted = *sorted_builtin;
  // Exercise the DLL's real sorted collection and public embedding entry.
  // No private VM helper is instantiated in this test executable.
  for (const bool erase : {false, true}) {
    auto state = std::make_shared<State>();
    state->outer = runtime.make_exception("LookupError", "scoped sorted outer");
    runtime.set_active_exception(state->outer);
    auto module = key_module(state, true, erase, false);
    Value key = Value::function(0, {}, Value::none(), module);
    Value values = Value::list({Value::int64(3), Value::int64(1), Value::int64(2)});
    std::vector<std::pair<std::string, Value>> kwargs = {{"key", key}};
    Value sorted_out;
    std::string error;
    const bool ok = runtime_call_callable_kw(runtime, sorted, &values, 1, kwargs, sorted_out, error);
    auto* list = value_as_list(sorted_out);
    expect_true(result, ok && list != nullptr && list->items.size() == 3 &&
        list->items[0].tag == ValueTag::Int64 && list->items[0].as.i64 == 1 &&
        list->items[1].as.i64 == 2 && list->items[2].as.i64 == 3,
        "real sorted must invoke the nontrivial legacy key once per input and preserve ordering");
    const bool final_active = value_is(runtime.active_exception(), state->outer);
    const bool audit_ok = state->calls == 3 && state->cleanup == 3 &&
        state->independent_namespace && state->previous_owner_retired && state->active_preserved &&
        final_active;
    std::string audit_message =
        "each fallback-writing key must release its owner and namespace before the next key entry";
    if (!audit_ok) {
      // Add failure evidence only. Preserve every original ownership/context
      // requirement; do not alter the real key, callbacks or runtime entry.
      std::ostringstream details;
      details << " [erase=" << erase << " calls=" << state->calls
          << " cleanup=" << state->cleanup
          << " independent_namespace=" << state->independent_namespace
          << " previous_owner_retired=" << state->previous_owner_retired
          << " active_preserved=" << state->active_preserved
          << " finalactive=" << final_active << "]";
      audit_message += details.str();
    }
    expect_true(result, audit_ok, audit_message);
    // The public embedding scope must release any fallback owner, including
    // store-then-delete. Keep this lifetime proof independent of wrapper policy.
    auto direct_state = std::make_shared<State>();
    direct_state->outer = state->outer;
    auto direct_module = key_module(direct_state, true, erase, false);
    Value direct_key = Value::function(0, {}, Value::none(), direct_module);
    CallArgsView args; Value argument = Value::int64(7);
    args.leading = &argument; args.leading_count = 1;
    {
      Interpreter interpreter(runtime);
      RuntimeResult call = interpreter.run_function_value(value_as_function(direct_key), args);
      expect_true(result, call.errors.empty(),
          "direct public embedding must execute fallback writes, including store followed by delete");
    }
    expect_true(result, direct_state->cleanup == 1,
        "direct embedding wrapper must retire its fallback owner at the ordinary scope boundary");
  }
  {
    auto state = std::make_shared<State>();
    state->outer = runtime.make_exception("LookupError", "scoped failure outer");
    state->failure = runtime.make_exception("ValueError", "scoped original failure");
    runtime.set_active_exception(state->outer);
    auto module = key_module(state, true, false, true);
    Value key = Value::function(0, {}, Value::none(), module);
    Value values = Value::list({Value::int64(3), Value::int64(1)});
    Value sorted_out;
    std::vector<std::pair<std::string, Value>> kwargs = {{"key", key}};
    std::string error;
    const bool ok = runtime_call_callable_kw(runtime, sorted, &values, 1, kwargs, sorted_out, error);
    Value pending;
    const bool had_pending = runtime.take_pending_exception(pending);
    expect_true(result, !ok && had_pending && value_is(pending, state->failure) &&
        state->calls == 1 && state->cleanup == 1 &&
        value_is(runtime.active_exception(), state->outer),
        "failed scoped key must forward original pending identity and retire fallback owners before returning");
  }
  {
    auto state = std::make_shared<State>();
    state->outer = runtime.make_exception("LookupError", "scoped clean outer");
    runtime.set_active_exception(state->outer);
    auto module = key_module(state, false, false, false);
    Value key = Value::function(0, {}, Value::none(), module);
    Value argument = Value::int64(7);
    CallArgsView args; args.leading = &argument; args.leading_count = 1;
    Interpreter interpreter(runtime);
    RuntimeResult first = interpreter.run_function_value(value_as_function(key), args);
    RuntimeResult second = interpreter.run_function_value(value_as_function(key), args);
    expect_true(result, first.errors.empty() && second.errors.empty() &&
        state->calls == 2 && state->cleanup == 2 &&
        state->independent_namespace && state->previous_owner_retired,
        "repeated nontrivial public entries must retire owners and retain an independent namespace");
  }
  runtime.clear_active_exception();
}
} // namespace xlang3::test
