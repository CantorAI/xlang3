#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"

namespace xlang3::test {
namespace runtime_frame_context_cases {
inline bool marker(const Value& mapping, int64_t expected) {
  Value value;
  std::string error;
  return mapping_get_item(mapping, Value::string("marker"), value, error) &&
      value.tag == ValueTag::Int64 && value.as.i64 == expected;
}
inline std::shared_ptr<const ir::Module> make_module() {
  auto module = std::make_shared<ir::Module>();
  module->source_file = "runtime_frame_context_public_api.py";
  module->entry = 0;
  module->functions.resize(3);
  for (unsigned index = 0; index != 3; ++index) {
    auto& function = module->functions[index];
    function.name = index == 0 ? "module" : index == 1 ? "before" : "after";
    function.locals = {"marker"};
    function.params = {"marker"};
    function.register_count = 1;
    function.code = {{ir::Op::LoadLocal, 0, 0, 0, 0},
        {ir::Op::Return, 0, 0, 0, 0}};
  }
  return module;
}
struct CleanupState {
  std::shared_ptr<bool> live;
  Runtime* runtime = nullptr;
  const ir::Module* module = nullptr;
  Value reentrant_globals;
  unsigned calls = 0;
  bool order_ok = true;
};
inline bool token(Runtime&, const Value*, uint32_t, Value& out,
    std::string& error, void*) {
  out = Value::none(); error.clear(); return true;
}
inline void cleanup(void* context) {
  auto state = *static_cast<std::shared_ptr<CleanupState>*>(context);
  delete static_cast<std::shared_ptr<CleanupState>*>(context);
  ++state->calls;
  if (!*state->live) return;
  state->order_ok &= state->runtime->current_frame_module_owner() != nullptr &&
      state->runtime->current_frame_module_owner()->get() == state->module &&
      state->runtime->current_frame_function_id() == 1 &&
      marker(state->runtime->current_locals_snapshot(), 11);
  state->runtime->set_current_globals_module(state->reentrant_globals);
}
struct StateGuard {
  Runtime& runtime;
  std::shared_ptr<bool> live;
  ~StateGuard() {
    *live = false;
    runtime.set_current_frame_stack(nullptr, 0);
    runtime.clear_current_frame();
  }
};
} // namespace runtime_frame_context_cases

inline void check_runtime_frame_context(CaseResult& result) {
  using namespace runtime_frame_context_cases;
  auto module = make_module();
  std::ostringstream output;
  Runtime runtime(output);
  auto live = std::make_shared<bool>(true);
  StateGuard guard{runtime, live};
  Value globals = Value::dict({});
  Value before[1] = {Value::int64(11)}, after[1] = {Value::int64(22)};
  const auto& names = module->functions[1].locals;
  size_t instruction = 1;
  RuntimeFrameView view{&module, &globals, &names, after, &instruction, 1, 2};
  runtime.set_current_globals_module(globals);
  runtime.set_current_frame_locals(&names, before, 1);
  runtime.set_current_frame(&module, 1, &globals, 0);
  runtime.set_current_frame_stack(&view, 1);
  Value hook = Value::native_function(0, "frame_context_hook", token, nullptr);
  Value exception = runtime.make_exception("LookupError", "frame context");
  runtime.set_trace_function(hook);
  runtime.set_profile_function(hook);
  runtime.set_trace_dispatch_active(true);
  runtime.set_profile_dispatch_active(true);
  runtime.set_active_exception(exception);
  runtime.set_pending_exception(exception);
  const auto refs = globals.as.obj->refcnt.load();
  bool admitted = true;
  for (unsigned index = 0; index != 32; ++index) {
    admitted &= runtime.try_set_current_frame_from_stack();
  }
  expect_true(result, admitted && globals.as.obj->refcnt.load() == refs,
      "32 public same-globals calls admit the owner-free frame-context path");
  runtime.set_current_frame_stack(nullptr, 0);
  // Clearing only the view exposes the legacy fields written by the new API.
  Value snapshot = runtime.current_frame_snapshot();
  auto* frame = value_as_frame(snapshot);
  expect_true(result, runtime.current_frame_module_owner() == &module &&
      runtime.current_frame_function_id() == 2 && marker(runtime.current_locals_snapshot(), 22) &&
      frame != nullptr && frame->module.get() == module.get() && frame->function_id == 2 &&
      frame->instruction_index == 1 && value_is(frame->globals_module, globals),
      "coalesced stores publish current legacy module/function/IP/locals/globals");
  expect_true(result, value_is(runtime.trace_function(), hook) &&
      value_is(runtime.profile_function(), hook) && runtime.trace_dispatch_active() &&
      runtime.profile_dispatch_active() && value_is(runtime.active_exception(), exception),
      "frame-context update preserves observer recursion flags and handled exception");
  Value pending;
  expect_true(result, runtime.take_pending_exception(pending) && value_is(pending, exception),
      "frame-context update preserves original pending exception ownership");
  value_set_invalid(snapshot);
  runtime.set_trace_function(Value::none());
  runtime.set_profile_function(Value::none());
  runtime.set_trace_dispatch_active(false);
  runtime.set_profile_dispatch_active(false);
  runtime.clear_active_exception();

  Value changed = Value::dict({});
  view.globals_module = &changed;
  runtime.set_current_frame_stack(&view, 1);
  expect_true(result, !runtime.try_set_current_frame_from_stack() &&
      value_is(runtime.current_globals_module(), globals),
      "changed globals decline without replacing the old owner");
  view.globals_module = nullptr;
  expect_true(result, !runtime.try_set_current_frame_from_stack(),
      "missing globals decline without publishing a partial frame context");
  view.globals_module = &globals;
  view.module_owner = nullptr;
  expect_true(result, !runtime.try_set_current_frame_from_stack(),
      "missing module-owner pointer declines the common path");
  view.module_owner = &module;
  view.instruction_index = nullptr;
  expect_true(result, !runtime.try_set_current_frame_from_stack(),
      "missing live instruction pointer declines the common path");
  runtime.set_current_frame_stack(nullptr, 0);
  expect_true(result, !runtime.try_set_current_frame_from_stack() &&
      runtime.current_frame_function_id() == 2 && marker(runtime.current_locals_snapshot(), 22),
      "empty stack declines and all prior legacy fields remain current");

  // A finalizer reached by the original changed-globals setter must still see
  // the previous legacy locals/frame and may reenter the same Runtime.
  auto state = std::make_shared<CleanupState>();
  state->live = live; state->runtime = &runtime; state->module = module.get();
  state->reentrant_globals = Value::dict({});
  Value old_globals = Value::dict({{Value::string("token"),
      Value::native_function(1, "frame_context_owner", token,
          new std::shared_ptr<CleanupState>(state), cleanup)}});
  runtime.set_current_globals_module(old_globals);
  runtime.set_current_frame_locals(&names, before, 1);
  runtime.set_current_frame(&module, 1, &old_globals, 0);
  value_set_invalid(old_globals);
  view.globals_module = &changed; view.instruction_index = &instruction;
  runtime.set_current_frame_stack(&view, 1);
  expect_true(result, !runtime.try_set_current_frame_from_stack() && state->calls == 0,
      "declining the common path never retires a changed globals owner");
  runtime.set_current_frame_stack(nullptr, 0);
  runtime.set_current_globals_module(changed);
  runtime.set_current_frame_locals(&names, after, 1);
  runtime.set_current_frame(&module, 2, &changed, 1);
  expect_true(result, state->calls == 1 && state->order_ok &&
      value_is(runtime.current_globals_module(), changed) &&
      marker(runtime.current_locals_snapshot(), 22),
      "original fallback retires globals before locals/frame stores despite cleanup reentry");

  // Each Runtime uses its own TLS state, even when the cache last served another.
  std::ostringstream other_output;
  Runtime other(other_output);
  auto other_live = std::make_shared<bool>(true);
  StateGuard other_guard{other, other_live};
  Value other_globals = Value::dict({});
  RuntimeFrameView other_view{&module, &other_globals, &names, before, &instruction, 1, 1};
  other.set_current_globals_module(other_globals);
  other.set_current_frame_stack(&other_view, 1);
  expect_true(result, other.try_set_current_frame_from_stack(),
      "another Runtime independently admits its same-globals view");
  runtime.set_current_frame_stack(&view, 1);
  expect_true(result, runtime.try_set_current_frame_from_stack() &&
      value_is(runtime.current_globals_module(), changed),
      "switching the TLS cache back does not borrow another Runtime's globals");
  runtime.set_current_frame_stack(nullptr, 0);
  expect_true(result, marker(runtime.current_locals_snapshot(), 22),
      "nested Runtime activity preserves the original live local context");
}
} // namespace xlang3::test
