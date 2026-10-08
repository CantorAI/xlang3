#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "executor/xlang_vm/xlang_frame.h"

namespace xlang3::test {
namespace exception_loop_owner_cases {
inline ir::Module module_with(std::vector<ir::Instr> code) {
  ir::Module module;
  module.functions.emplace_back();
  auto& function = module.functions[0];
  function.name = "exception_loop_owner";
  function.register_count = 7;
  function.locals = {"error", "borrowed"};
  function.code = std::move(code);
  return module;
}
struct State { Value original; uint32_t expected_refs = 0; uint32_t observed_refs = 0; unsigned calls = 0; };
inline bool probe(Runtime& runtime, const Value*, uint32_t argc, Value& out,
    std::string& error, void* pointer) {
  if (argc != 0) { error = "unexpected exception owner probe arity"; return false; }
  auto& state = **static_cast<std::shared_ptr<State>*>(pointer);
  Value gc, collect, ignored;
  if (!runtime.import_module("gc", gc, error) ||
      !module_get_attr(gc, "collect", collect, error) ||
      !runtime_call_callable(runtime, collect, nullptr, 0, ignored, error)) return false;
  ++state.calls;
  state.observed_refs = state.original.as.obj->refcnt.load();
  out = Value::none();
  return true;
}
inline void cleanup(void* pointer) { delete static_cast<std::shared_ptr<State>*>(pointer); }
} // namespace exception_loop_owner_cases

inline void check_exception_loop_owner_cases(CaseResult& result) {
  using namespace exception_loop_owner_cases;
  const std::vector<Value> closure;
  auto check_metadata = [&](std::vector<ir::Instr> code, bool carried,
                            size_t last_use, const char* message) {
    auto module = module_with(std::move(code));
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    expect_true(result, frame.execution_metadata->register_loop_carried[0] == carried &&
        frame.execution_metadata->register_last_use[0] == last_use, message);
  };
  check_metadata({{ir::Op::SetupExcept, 2, 0, 0, 0},
      {ir::Op::Jump, 5, 0, 0, 0}, {ir::Op::LoadException, 0, 0, 0, 0},
      {ir::Op::StoreLocal, 0, 0, 0, 0}, {ir::Op::ClearException, 0, 0, 0, 0},
      {ir::Op::Jump, 0, 0, 0, 0}}, false, 3,
      "handler LoadException dominates its only read and is recreated before each use");
  check_metadata({{ir::Op::JumpIfFalse, 2, 4, 0, 0},
      {ir::Op::LoadException, 0, 0, 0, 0}, {ir::Op::StoreLocal, 0, 0, 0, 0},
      {ir::Op::Jump, 0, 0, 0, 0}}, true, SIZE_MAX,
      "ordinary bypass preserves SDK prior-iteration exception owners");
  check_metadata({{ir::Op::StoreLocal, 0, 0, 0, 0},
      {ir::Op::LoadException, 0, 0, 0, 0}, {ir::Op::Jump, 0, 0, 0, 0}}, true, SIZE_MAX,
      "read before LoadException preserves the carried exception owner");
  check_metadata({{ir::Op::LoadException, 0, 0, 0, 0},
      {ir::Op::StoreLocal, 0, 0, 0, 0}, {ir::Op::Jump, 1, 0, 0, 0},
      {ir::Op::Jump, 0, 0, 0, 0}}, true, SIZE_MAX,
      "outer recreation never clears an inner-loop carried owner");
  check_metadata({{ir::Op::LoadException, 0, 0, 0, 0},
      {ir::Op::StoreLocal, 0, 0, 0, 0}, {ir::Op::IsJumpIfFalse, 1, 3, 4, 0},
      {ir::Op::Jump, 0, 0, 0, 0}}, true, SIZE_MAX,
      "fused backedge bypassing the exception producer retains the old owner");
  check_metadata({{ir::Op::SetupExcept, 2, 0, 0, 0},
      {ir::Op::LoadException, 0, 0, 0, 0}, {ir::Op::StoreLocal, 0, 0, 0, 0},
      {ir::Op::Jump, 0, 0, 0, 0}}, true, SIZE_MAX,
      "exceptional edge entering after the producer rejects retirement");
  check_metadata({{ir::Op::LoadException, 0, 0, 0, 0},
      {ir::Op::SetupExcept, 2, 0, 0, 0}, {ir::Op::StoreLocal, 0, 0, 0, 0},
      {ir::Op::Call, 6, 5, 0, 0}, {ir::Op::Jump, 0, 0, 0, 0}}, true, SIZE_MAX,
      "handler installed after the producer can reenter its consumed read from a later failure");
  check_metadata({{ir::Op::LoadException, 0, 0, 0, 0},
      {ir::Op::SetupWith, 2, 4, 0, 0}, {ir::Op::StoreLocal, 0, 0, 0, 0},
      {ir::Op::Call, 6, 5, 0, 0}, {ir::Op::Jump, 0, 0, 0, 0}}, true, SIZE_MAX,
      "with-handler targets cannot reuse a moved exception snapshot without recreating it");
  check_metadata({{ir::Op::LoadException, 0, 0, 0, 0},
      {ir::Op::StoreLocal, 0, 0, 0, 0}, {ir::Op::JumpIfFalse, 0, 4, 0, 0},
      {ir::Op::StoreLocal, 1, 0, 0, 0}}, false, 3,
      "future exception alias read keeps its later linear last use");
  // A local projection is borrowed, not an owning exception snapshot. Keep
  // its older conservative loop/call-transfer policy completely unchanged.
  {
    auto module = module_with({{ir::Op::LoadException, 0, 0, 0, 0},
        {ir::Op::StoreLocal, 0, 0, 0, 0}, {ir::Op::LoadGlobalLocal, 2, 0, 1, 0},
        {ir::Op::StoreLocal, 1, 1, 0, 0}, {ir::Op::Jump, 0, 0, 0, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    Value owner = Value::list({});
    frame.locals[0] = owner;
    value_borrow_assign_fast(frame.regs[1], frame.locals[0]);
    const auto references = owner.as.obj->refcnt.load();
    expect_true(result, !frame.execution_metadata->register_loop_carried[0] &&
        frame.execution_metadata->register_loop_carried[1] &&
        frame.execution_metadata->register_last_use[1] == SIZE_MAX &&
        (frame.regs[1].flags & kXlangValueBorrowedRefFlag) != 0 &&
        owner.as.obj->refcnt.load() == references,
        "exception proof does not promote or make borrowed local projections transferable");
  }
  // Actual exported Interpreter execution, with registered gc.collect inside
  // a native probe. A CFG backedge is present but untaken for this bounded
  // ownership audit; the strict Python fixture exercises three real iterations.
  {
    std::ostringstream output;
    Runtime runtime(output);
    Value outer = runtime.make_exception("LookupError", "outer preserved");
    Value original = runtime.make_exception("RuntimeError", "inner marker");
    Value traceback_marker = Value::traceback(Value::frame({}, 0, Value::none()), Value::none(), 1);
    std::string error;
    (void)object_set_attr(original, "__context__", outer, error);
    (void)object_set_attr(original, "__traceback__", traceback_marker, error);
    runtime.set_active_exception(outer);
    auto state = std::make_shared<State>();
    state->original = original;
    auto context = std::make_unique<std::shared_ptr<State>>(state);
    Value probe_function = runtime.make_native_function("exception_owner_probe", probe,
        context.get(), cleanup);
    context.release();
    auto module = std::make_shared<ir::Module>(module_with({
        {ir::Op::LoadConst, 0, 0, 0, 0}, {ir::Op::LoadConst, 4, 2, 0, 0},
        {ir::Op::SetException, 0, 0, 0, 0}, {ir::Op::LoadException, 1, 0, 0, 0},
        {ir::Op::StoreLocal, 0, 1, 0, 0}, {ir::Op::LoadConst, 2, 1, 0, 0},
        {ir::Op::StoreLocal, 0, 2, 0, 0}, {ir::Op::DeleteLocal, 0, 0, 0, 0},
        {ir::Op::ClearException, 0, 0, 0, 0}, {ir::Op::JumpIfFalse, 3, 4, 0, 0},
        {ir::Op::LoadConst, 5, 3, 0, 0}, {ir::Op::Call, 6, 5, 0, 0},
        {ir::Op::ReturnConst, 0, 1, 0, 0}}));
    module->functions[0].constants = {original, Value::none(), Value::boolean(true), probe_function};
    module->functions[0].call_args = {{}};
    state->expected_refs = original.as.obj->refcnt.load();
    Interpreter interpreter(runtime);
    RuntimeResult run = interpreter.run(module);
    expect_true(result, run.errors.empty() && state->calls == 1 &&
        state->observed_refs == state->expected_refs,
        "handler cleanup and GC leave no owning LoadException loop register");
    Value context_after, traceback_after;
    expect_true(result, value_is(runtime.active_exception(), outer) &&
        object_get_attr(original, "__context__", context_after, error) && value_is(context_after, outer) &&
        object_get_attr(original, "__traceback__", traceback_after, error) && value_is(traceback_after, traceback_marker),
        "temporary retirement preserves caller active exception and retained context/traceback aliases");
  }
}
} // namespace xlang3::test
