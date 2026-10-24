#pragma once

#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"

namespace xlang3::test {
namespace interpreter_lazy_globals_cases {

inline std::shared_ptr<ir::Module> legacy_module() {
  auto module = std::make_shared<ir::Module>();
  module->global_slots = {"fallback_value"};
  module->functions.resize(8);
  for (auto& fn : module->functions) {
    fn.name = "legacy_fallback";
    fn.register_count = 4;
    fn.constants = {Value::none()};
    fn.names = {"fallback_value"};
  }
  for (uint32_t index : {0u, 4u}) {
    auto& fn = module->functions[index];
    fn.params = {"value"};
    fn.locals = {"value"};
    fn.code = {{ir::Op::LoadLocal, 0, 0, 0, 0},
        {index == 0 ? ir::Op::StoreGlobal : ir::Op::StoreModuleSlot, 0, 0, 0, 0},
        {ir::Op::ReturnConst, 0, 0, 0, 0}};
  }
  module->functions[1].code = {{ir::Op::LoadGlobal, 0, 0, 0, 0},
      {ir::Op::Return, 0, 0, 0, 0}};
  module->functions[2].code = {{ir::Op::DeleteGlobal, 0, 0, 0, 0},
      {ir::Op::ReturnConst, 0, 0, 0, 0}};
  module->functions[3].names = {"len"};
  module->functions[3].code = module->functions[1].code;
  module->functions[5].code = {{ir::Op::LoadModuleSlot, 0, 0, 0, 0},
      {ir::Op::Return, 0, 0, 0, 0}};
  auto& nested = module->functions[6];
  nested.params = {"callback"};
  nested.locals = {"callback"};
  nested.call_args = {{}};
  nested.code = {{ir::Op::LoadGlobal, 0, 0, 0, 0},
      {ir::Op::LoadLocal, 1, 0, 0, 0}, {ir::Op::Call, 2, 1, 0, 0},
      {ir::Op::LoadGlobal, 3, 0, 0, 0}, {ir::Op::Return, 0, 3, 0, 0}};
  module->functions[7].raw_blocks = {{"lazy_test", "fallback", "increment"}};
  module->functions[7].code = {{ir::Op::RawBlock, 0, 0, 0, 0},
      {ir::Op::LoadGlobal, 0, 0, 0, 0}, {ir::Op::Return, 0, 0, 0, 0}};
  return module;
}

inline RuntimeResult invoke(Interpreter& interpreter, const Value& function,
                            const Value* arguments = nullptr, uint32_t argc = 0) {
  CallArgsView view;
  view.leading = arguments;
  view.leading_count = argc;
  return interpreter.run_function_value(value_as_function(function), view);
}

inline bool increment_raw(Runtime&, RawBlockContext& context,
                          const std::string&, const std::string&,
                          const std::string&, std::string& error) {
  Value value;
  if (!context.get_var("fallback_value", value, error)) return false;
  if (value.tag != ValueTag::Int64) { error = "unexpected fallback value"; return false; }
  return context.set_var("fallback_value", Value::int64(value.as.i64 + 1), error);
}

struct ReentryState {
  Interpreter* target = nullptr;
  Value setter;
  Value getter;
  int64_t next = 0;
  bool observed = false;
};

inline bool reenter(Runtime&, const Value*, uint32_t argc, Value& out,
                    std::string& error, void* data) {
  auto& state = *static_cast<ReentryState*>(data);
  if (argc != 0) { error = "unexpected reentry arity"; return false; }
  const Value next = Value::int64(state.next);
  auto stored = invoke(*state.target, state.setter, &next, 1);
  auto loaded = invoke(*state.target, state.getter);
  state.observed = stored.errors.empty() && loaded.errors.empty() &&
      loaded.value.tag == ValueTag::Int64 && loaded.value.as.i64 == state.next;
  if (!state.observed) { error = "fallback reentry failed"; return false; }
  value_set_none(out);
  return true;
}

struct CleanupAudit {
  uint32_t calls = 0;
  bool absent = false;
  bool skipped_expired = false;
};
struct WrapperLifetime {
  bool live = true;
};
struct WrapperLifetimeGuard {
  std::shared_ptr<WrapperLifetime> lifetime;
  ~WrapperLifetimeGuard() { lifetime->live = false; }
};
struct CleanupState {
  std::shared_ptr<CleanupAudit> audit;
  std::shared_ptr<WrapperLifetime> lifetime;
  Interpreter* interpreter = nullptr;
  Runtime* runtime = nullptr;
  Value getter;
};
inline void cleanup(void* data) {
  std::unique_ptr<CleanupState> state(static_cast<CleanupState*>(data));
  ++state->audit->calls;
  // A failed run/traceback or host owner can retain the callback through C++
  // unwind. Its audit/context remains owned, but wrapper pointers no longer
  // permit reentry once the guard expires before Interpreter destruction.
  if (!state->lifetime || !state->lifetime->live) {
    state->audit->skipped_expired = true;
    return;
  }
  auto missing = invoke(*state->interpreter, state->getter);
  state->audit->absent = !missing.errors.empty();
  Value pending;
  (void)state->runtime->take_pending_exception(pending);
}

struct UnwindMarker {};

} // namespace interpreter_lazy_globals_cases

inline void check_interpreter_lazy_globals_cases(CaseResult& result) {
  using namespace interpreter_lazy_globals_cases;
  // This is an allocation-contract check, not a cross-DLL malloc counter:
  // disengaged optional storage cannot construct MSVC's allocated map.
  {
    InterpreterFallbackGlobals globals;
    for (uint32_t index = 0; index != 64; ++index) {
      expect_true(result, globals.find("missing") == nullptr &&
          globals.take("missing").tag == ValueTag::Invalid && !globals.materialized(),
          "fallback misses/deletes must leave allocated map storage absent");
    }
    Value owner = Value::list({Value::int64(71)});
    const auto references = owner.as.obj->refcnt.load();
    value_assign_fast(globals["kept"], owner);
    expect_true(result, globals.materialized() && globals.find("kept") != nullptr &&
        value_is(*globals.find("kept"), owner) && owner.as.obj->refcnt.load() == references + 1,
        "first fallback store must materialize independent owning storage");
    InterpreterFallbackGlobals copied = globals;
    Value removed = globals.take("kept");
    expect_true(result, globals.find("kept") == nullptr && globals.materialized() &&
        value_is(removed, owner) && copied.find("kept") != nullptr &&
        value_is(*copied.find("kept"), owner) && owner.as.obj->refcnt.load() == references + 2,
        "taking a binding must preserve its moved owner and deep-copy namespace isolation");
  }

  // Force the actual unsafe ordering: a fallback binding and a host owner
  // retain cleanup through C++ unwind, then its last owner dies after both
  // the Interpreter and Runtime. Context storage must survive without reentry.
  {
    auto late_audit = std::make_shared<CleanupAudit>();
    Value late_owner;
    try {
      std::ostringstream late_output;
      Runtime late_runtime(late_output);
      auto late_module = legacy_module();
      Value setter = Value::function(0, {}, Value::invalid(), late_module);
      Value getter = Value::function(1, {}, Value::invalid(), late_module);
      Interpreter late_interpreter(late_runtime);
      auto late_lifetime = std::make_shared<WrapperLifetime>();
      WrapperLifetimeGuard guard{late_lifetime};
      auto late_state = std::make_unique<CleanupState>();
      late_state->audit = late_audit; late_state->lifetime = late_lifetime;
      late_state->interpreter = &late_interpreter; late_state->runtime = &late_runtime;
      late_state->getter = getter;
      late_owner = Value::native_function(0, "fallback_late_cleanup", nullptr, late_state.get(), cleanup);
      late_state.release();
      expect_true(result, invoke(late_interpreter, setter, &late_owner, 1).errors.empty(),
          "unwind fixture must retain its actual fallback binding");
      throw UnwindMarker{};
    } catch (const UnwindMarker&) {
      expect_true(result, late_audit->calls == 0,
          "host owner keeps cleanup context alive through wrapper/runtime unwind");
    }
    value_set_invalid(late_owner);
    expect_true(result, late_audit->calls == 1 && late_audit->skipped_expired && !late_audit->absent,
        "expired wrapper cleanup must release its owned state once without stale reentry");
  }

  std::ostringstream output;
  Runtime runtime(output);
  auto module = legacy_module();
  std::vector<Value> functions;
  for (uint32_t index = 0; index != module->functions.size(); ++index)
    functions.push_back(Value::function(index, {}, Value::invalid(), module));
  Interpreter first(runtime);
  Interpreter second(runtime);
  auto lifetime = std::make_shared<WrapperLifetime>();
  // Declared after both wrappers: expire before either destructor, even when
  // a C++ exception skips the explicit cleanup store near the end of the test.
  WrapperLifetimeGuard lifetime_guard{lifetime};
  auto store = [&](Interpreter& target, int64_t number, uint32_t function = 0) {
    const Value argument = Value::int64(number);
    return invoke(target, functions[function], &argument, 1);
  };
  auto is_number = [&](const RuntimeResult& value, int64_t expected) {
    return value.errors.empty() && value.value.tag == ValueTag::Int64 && value.value.as.i64 == expected;
  };
  expect_true(result, store(first, 111).errors.empty() &&
      is_number(invoke(first, functions[1]), 111) &&
      is_number(invoke(first, functions[5]), 111),
      "legacy global and module-slot IR share one persistent Interpreter fallback");
  auto missing = invoke(second, functions[1]);
  expect_true(result, !missing.errors.empty(), "a separate Interpreter must not inherit fallback bindings");
  Value pending;
  (void)runtime.take_pending_exception(pending);
  value_set_invalid(pending);
  expect_true(result, store(second, 222, 4).errors.empty() &&
      is_number(invoke(second, functions[1]), 222) &&
      is_number(invoke(first, functions[1]), 111),
      "separate fallback stores must stay isolated when function IR is shared");
  auto builtin = invoke(first, functions[3]);
  const Value* expected_builtin = runtime.find_builtin("len");
  expect_true(result, builtin.errors.empty() && expected_builtin != nullptr &&
      value_is(builtin.value, *expected_builtin), "an absent fallback binding must preserve builtin resolution");

  // Callback context is heap-owned; explicit owners keep it alive during each
  // nested entry and no stored callable can outlive either Interpreter.
  for (bool same_interpreter : {false, true}) {
    auto state = std::make_unique<ReentryState>();
    state->target = same_interpreter ? &first : &second;
    state->setter = functions[0];
    state->getter = functions[1];
    state->next = same_interpreter ? 333 : 444;
    auto* observed = state.get();
    Value callback = runtime.make_native_function("lazy_globals_reenter", reenter, state.get(),
        [](void* data) { delete static_cast<ReentryState*>(data); });
    state.release();
    auto loaded = invoke(first, functions[6], &callback, 1);
    expect_true(result, observed->observed && is_number(loaded, same_interpreter ? 333 : 111),
        "nested entry preserves wrapper isolation and sees same-wrapper mutation after return");
  }
  runtime.register_raw_block_handler("lazy_test", "fallback", increment_raw);
  expect_true(result, is_number(invoke(first, functions[7]), 334) &&
      is_number(invoke(first, functions[1]), 334), "RawBlock fallback reads/stores must retain versioned ownership");

  Value module_globals = Value::module("lazy_globals_test");
  std::string error;
  expect_true(result, module_set_attr(module_globals, "fallback_value", Value::int64(777), error),
      "module globals fixture must initialize");
  Value module_getter = Value::function(1, {}, module_globals, module);
  Value dict_globals = Value::dict({{Value::string("fallback_value"), Value::int64(888)}});
  Value dict_getter = Value::function(1, {}, dict_globals, module);
  expect_true(result, is_number(invoke(first, module_getter), 777) &&
      is_number(invoke(first, dict_getter), 888) && is_number(invoke(first, functions[1]), 334),
      "retained module/dict namespaces must resolve before unrelated fallback storage");

  auto audit = std::make_shared<CleanupAudit>();
  auto state = std::make_unique<CleanupState>();
  state->audit = audit; state->lifetime = lifetime;
  state->interpreter = &first; state->runtime = &runtime; state->getter = functions[1];
  Value owned = Value::native_function(0, "fallback_owned_cleanup", nullptr, state.get(), cleanup);
  state.release();
  expect_true(result, invoke(first, functions[0], &owned, 1).errors.empty(), "fallback must own cleanup fixture");
  value_set_invalid(owned);
  expect_true(result, audit->calls == 0, "stored fallback owner must survive frame pop");
  expect_true(result, invoke(first, functions[2]).errors.empty() && audit->calls == 1 &&
      audit->absent && !audit->skipped_expired,
      "delete publishes an absent binding before the last owner's nested cleanup");
  // Also clean an unsuccessful implementation's retained owner while the
  // wrapper is still fully alive; no cleanup callback may reenter destruction.
  const Value none = Value::none();
  (void)invoke(first, functions[0], &none, 1);
  (void)runtime.take_pending_exception(pending);
  value_set_invalid(pending);
  expect_true(result, invoke(first, functions[2]).errors.empty() &&
      invoke(first, functions[2]).errors.empty(), "deleting an absent legacy fallback remains supported");
}

} // namespace xlang3::test
