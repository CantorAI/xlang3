#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"

namespace xlang3::test {

namespace cross_activation_constructor_cases {
struct ProfileState {
  std::shared_ptr<const ir::Module> module;
  uint32_t function_id = UINT32_MAX;
  uint32_t calls = 0;
  uint32_t returns = 0;
};
inline void destroy_profile(void* data) {
  delete static_cast<std::shared_ptr<ProfileState>*>(data);
}
inline bool profile(Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* data) {
  auto& state = **static_cast<std::shared_ptr<ProfileState>*>(data);
  auto* frame = argc == 3 ? value_as_frame(args[0]) : nullptr;
  auto* event = argc == 3 ? value_as_string(args[1]) : nullptr;
  if (frame != nullptr && event != nullptr &&
      frame->module.get() == state.module.get() && frame->function_id == state.function_id) {
    const auto name = string_object_view(*event);
    if (name == "call") ++state.calls;
    else if (name == "return") ++state.returns;
  }
  error.clear(); out = Value::none(); return true;
}
inline bool handle_native(Runtime&, const ClassObject& klass, const CallArgsView&,
    bool& handled, Value& out, std::string& error) {
  Value retained_class;
  retained_class.tag = ValueTag::Object;
  retained_class.as.obj = const_cast<Object*>(&klass.header);
  retain(retained_class);
  out = Value::instance(retained_class);
  handled = true;
  return object_set_attr(out, "value", Value::int64(500), error);
}
inline bool decline_native(Runtime&, const ClassObject&, const CallArgsView&,
    bool& handled, Value&, std::string& error) {
  handled = false; error.clear(); return true;
}
inline bool install_native(Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  auto* klass = argc == 1 ? value_as_class(args[0]) : nullptr;
  if (klass == nullptr) { error = "expected a class for native-hook proof"; return false; }
  // A native registration can publish a hook without changing Python attrs or
  // the class version. This deliberately tests the live warm-hook exclusion.
  klass->native_type_constructor = handle_native;
  klass->native_type_constructor_version = klass->version;
  error.clear(); out = Value::none(); return true;
}
struct RuntimeProfileReset {
  Runtime& runtime;
  ~RuntimeProfileReset() { runtime.set_profile_function(Value::none()); }
};
} // namespace cross_activation_constructor_cases

inline void check_cross_activation_constructor(CaseResult& result) {
  using namespace cross_activation_constructor_cases;
  const char* source = R"PY(
class CrossBase:
    def __init__(self, value=0):
        self.value = value

class CrossOwn(CrossBase):
    def __init__(self, value=0):
        self.value = value

class CrossInherited(CrossBase):
    pass

class CrossMeta(type):
    pass

class CrossMetaInherited(CrossBase, metaclass=CrossMeta):
    pass

class CrossNativeDeclines(CrossBase):
    pass

class CrossNativeLater(CrossBase):
    pass

class CrossLegacy(CrossBase):
    def __init__(self, value=0):
        self.value = value

def construct_once(cls, value):
    return cls(*(), **{'value': value})

def outer_driver(cls, count, install=False):
    total = 0
    for value in range(count):
        if install and value == 16:
            install_native(cls)
        obj = construct_once(cls, value)
        total += obj.value
    return total
)PY";
  auto parsed = parse_source(source);
  expect_true(result, parsed.errors.empty(), "cross-activation constructor proof parses");
  if (!parsed.errors.empty()) return;
  auto lowered = lower_to_ir(parsed.module);
  expect_true(result, lowered.errors.empty(), "cross-activation constructor proof lowers");
  if (!lowered.errors.empty()) return;
  uint32_t wrapper_id = UINT32_MAX;
  bool wrapper_shape = false;
  for (uint32_t id = 0; id < lowered.module.functions.size(); ++id) {
    const auto& function = lowered.module.functions[id];
    if (function.name != "construct_once") continue;
    wrapper_id = id;
    size_t call = function.code.size();
    bool straight_line = true;
    for (size_t ip = 0; ip < function.code.size(); ++ip) {
      const auto& instruction = function.code[ip];
      if (instruction.op == ir::Op::CallEx) { call = ip; break; }
      // Admit only the wrapper's ordinary argument materialization prefix;
      // there is no branch or incoming edge that can bypass this CallEx.
      switch (instruction.op) {
        case ir::Op::LoadConst:
        case ir::Op::LoadLocal:
        case ir::Op::LoadLocalConst:
        case ir::Op::MakeTuple:
        case ir::Op::MakeDict: break;
        default: straight_line = false; break;
      }
    }
    if (straight_line && call + 2 < function.code.size()) {
      const auto& explicit_return = function.code[call + 1];
      const auto& implicit_return = function.code[call + 2];
      // FunctionLowerer::finish always appends an implicit None return. It is
      // unreachable after this direct result return, not a second activation.
      // Prove that exact tail instead of counting every return opcode as live.
      wrapper_shape = explicit_return.op == ir::Op::Return &&
          explicit_return.a == function.code[call].dst &&
          implicit_return.op == ir::Op::ReturnConst &&
          implicit_return.a < function.constants.size() &&
          function.constants[implicit_return.a].tag == ValueTag::None &&
          call + 3 == function.code.size();
    }
  }
  expect_true(result, wrapper_id != UINT32_MAX && wrapper_shape,
      "wrapper has one reachable CallEx/result Return and only the unreachable implicit None tail");
  if (wrapper_id == UINT32_MAX || !wrapper_shape) return;
  std::ostringstream output;
  // Module owners outlive Runtime, and native profile state holds no raw
  // Runtime/Interpreter pointer. Failure/unwind cannot leave stale cleanup.
  Value globals = Value::module("cross_activation_constructor_audit");
  std::shared_ptr<const ir::Module> module =
      std::make_shared<ir::Module>(std::move(lowered.module));
  auto state = std::make_shared<ProfileState>();
  state->module = module;
  state->function_id = wrapper_id;
  Runtime runtime(output);
  Interpreter interpreter(runtime);
  RuntimeProfileReset reset{runtime};
  auto setup = interpreter.run_module(*module, globals, module);
  expect_true(result, setup.errors.empty(), "cross-activation setup uses actual DLL entry");
  if (!setup.errors.empty()) return;
  Value install = Value::native_function(0, "install_native", install_native);
  std::string error;
  expect_true(result, module_set_attr(globals, "install_native", install, error),
      "native hook proof registers its public callable");
  Value driver, wrapper;
  if (!module_get_attr(globals, "outer_driver", driver, error) ||
      !module_get_attr(globals, "construct_once", wrapper, error)) {
    expect_true(result, false, "cross-activation proof obtains its live functions"); return;
  }
  auto profile_context = std::make_unique<std::shared_ptr<ProfileState>>(state);
  Value observer = Value::native_function(0, "cross_activation_profile", profile,
      profile_context.get(), destroy_profile);
  profile_context.release();
  runtime.set_profile_function(observer);
  struct CounterScope {
    bool previous = xlang_perf_counters().enabled.load(std::memory_order_relaxed);
    CounterScope() { xlang_perf_set_enabled(true); }
    ~CounterScope() { xlang_perf_set_enabled(previous); }
  } counters;
  const auto kind = xlang_perf_kind_index(ObjectKind::BoundMethod);
  const auto allocations = [&] {
    return xlang_perf_counters().object_allocations[kind].load(std::memory_order_relaxed);
  };
  for (const char* name : {"CrossOwn", "CrossInherited", "CrossMetaInherited"}) {
    Value klass;
    if (!module_get_attr(globals, name, klass, error)) {
      expect_true(result, false, std::string("cross-activation missing class ") + name); continue;
    }
    state->calls = state->returns = 0;
    const auto before = allocations();
    const Value args[] = {klass, Value::int64(32)};
    Value answer;
    const bool ok = runtime_call_callable(runtime, driver, args, 2, answer, error);
    const auto after = allocations();
    expect_true(result, ok && answer.tag == ValueTag::Int64 && answer.as.i64 == 496,
        std::string("all 32 returned-wrapper initialized values match: ") + name);
    expect_true(result, state->calls == 32 && state->returns == 32,
        std::string("actual profile proves 32 separate inner call/return activations: ") + name);
    expect_true(result, after == before + 1,
        std::string("one cold bound initializer across 32 returned activations: ") + name);
  }
  Value klass;
  if (!module_get_attr(globals, "CrossInherited", klass, error)) {
    expect_true(result, false, "host-entry negative obtains class"); return;
  }
  const auto before_hosts = allocations();
  int64_t host_sum = 0;
  for (int64_t value = 0; value < 32; ++value) {
    const Value args[] = {klass, Value::int64(value)};
    Value answer, member;
    const bool ok = runtime_call_callable(runtime, wrapper, args, 2, answer, error) &&
        object_get_attr(answer, "value", member, error) && member.tag == ValueTag::Int64;
    expect_true(result, ok, "each separate host entry uses actual wrapper dispatch");
    if (ok) host_sum += member.as.i64;
  }
  expect_true(result, host_sum == 496 && allocations() == before_hosts + 32,
      "outer-entry teardown intentionally clears all prepared weak constructor associations");

  if (!module_get_attr(globals, "CrossNativeDeclines", klass, error)) {
    expect_true(result, false, "declining native hook proof obtains class"); return;
  }
  value_as_class(klass)->native_type_constructor = decline_native;
  value_as_class(klass)->native_type_constructor_version = value_as_class(klass)->version;
  const auto before_decline = allocations();
  const Value decline_args[] = {klass, Value::int64(32)};
  Value answer;
  const bool decline_ok = runtime_call_callable(runtime, driver, decline_args, 2, answer, error);
  expect_true(result, decline_ok && answer.tag == ValueTag::Int64 && answer.as.i64 == 496 &&
      allocations() == before_decline + 32,
      "a registered native hook's transient decline never certifies a persistent Python plan");

  if (!module_get_attr(globals, "CrossNativeLater", klass, error)) {
    expect_true(result, false, "new native hook proof obtains class"); return;
  }
  const uint64_t class_version = value_as_class(klass)->version;
  const auto before_install = allocations();
  const Value install_args[] = {klass, Value::int64(32), Value::boolean(true)};
  const bool install_ok = runtime_call_callable(runtime, driver, install_args, 3, answer, error);
  expect_true(result, install_ok && answer.tag == ValueTag::Int64 && answer.as.i64 == 8120 &&
      value_as_class(klass)->version == class_version && allocations() == before_install + 1,
      "a live newly registered hook handles calls despite unchanged cached class generation");

  if (!module_get_attr(globals, "CrossLegacy", klass, error)) {
    expect_true(result, false, "legacy nonowning-IR proof obtains class"); return;
  }
  auto* legacy_class = value_as_class(klass);
  if (legacy_class == nullptr || legacy_class->attrs.find("__init__") == legacy_class->attrs.end()) {
    expect_true(result, false, "legacy proof owns a declared initializer"); return;
  }
  Value original = legacy_class->attrs.find("__init__")->second;
  auto* original_function = value_as_function(original);
  if (original_function == nullptr) {
    expect_true(result, false, "legacy declared initializer is an actual Python function"); return;
  }
  Value legacy = Value::function(original_function->function_id,
      original_function->closure, original_function->globals_module);
  expect_true(result, object_set_attr(klass, "__init__", legacy, error),
      "legacy initializer is installed through the actual class mutation API");
  const auto before_legacy = allocations();
  const Value legacy_args[] = {klass, Value::int64(32)};
  const bool legacy_ok = runtime_call_callable(runtime, driver, legacy_args, 2, answer, error);
  expect_true(result, legacy_ok && answer.tag == ValueTag::Int64 && answer.as.i64 == 496 &&
      allocations() == before_legacy + 32,
      "legacy initializer without owned IR remains correct and cold after every inner return");
}

inline void check_inherited_call_ex_constructor(CaseResult& result) {
  check_cross_activation_constructor(result);
  const char* source = R"PY(
class AuditBase:
    def __init__(self, x=0, *, flag=1):
        self.x = x

class AuditOwn(AuditBase):
    def __init__(self, x=0, *, flag=1):
        self.x = x

class AuditInherited(AuditBase):
    pass

class AuditMeta(type):
    pass

class AuditMetaInherited(AuditBase, metaclass=AuditMeta):
    pass

def exercise(cls, count, rename=False):
    total = 0
    for index in range(count):
        if rename and index == 16:
            AuditBase.__name__ = 'AuditRenamedBase'
        obj = cls(*(), **{'x': index, 'flag': 1})
        total += obj.x
    return total
)PY";
  auto parsed = parse_source(source);
  expect_true(result, parsed.errors.empty(), "expanded inherited constructor proof parses");
  if (!parsed.errors.empty()) return;
  auto lowered = lower_to_ir(parsed.module);
  expect_true(result, lowered.errors.empty(), "expanded inherited constructor proof lowers");
  if (!lowered.errors.empty()) return;
  bool expanded = false;
  for (const auto& function : lowered.module.functions)
    if (function.name == "exercise")
      for (const auto& instruction : function.code)
        expanded |= instruction.op == ir::Op::CallEx;
  expect_true(result, expanded, "eligibility proof must actually execute the expanded call opcode");
  if (!expanded) return;
  std::ostringstream output;
  Runtime runtime(output);
  Interpreter interpreter(runtime);
  Value globals = Value::module("inherited_call_ex_constructor_audit");
  auto module = std::make_shared<ir::Module>(std::move(lowered.module));
  auto setup = interpreter.run_module(*module, globals, module);
  expect_true(result, setup.errors.empty(), "expanded constructor proof setup runs");
  if (!setup.errors.empty()) return;
  Value exercise;
  std::string error;
  expect_true(result, module_get_attr(globals, "exercise", exercise, error),
      "eligibility proof obtains its Python loop");
  if (exercise.tag == ValueTag::Invalid) return;
  // Only count real object allocations inside one live loop activation. Do not
  // infer elapsed costs, cross-DLL malloc totals, or reuse across popped frames.
  struct CounterScope {
    bool previous = xlang_perf_counters().enabled.load(std::memory_order_relaxed);
    CounterScope() { xlang_perf_set_enabled(true); }
    ~CounterScope() { xlang_perf_set_enabled(previous); }
  } counters;
  const auto kind = xlang_perf_kind_index(ObjectKind::BoundMethod);
  for (const char* name : {"AuditOwn", "AuditInherited", "AuditMetaInherited"}) {
    Value klass;
    error.clear();
    if (!module_get_attr(globals, name, klass, error)) {
      expect_true(result, false, std::string("missing constructor proof class ") + name);
      continue;
    }
    const auto before = xlang_perf_counters().object_allocations[kind].load(std::memory_order_relaxed);
    const Value args[] = {klass, Value::int64(32)};
    Value answer;
    const bool ok = runtime_call_callable(runtime, exercise, args, 2, answer, error);
    const auto after = xlang_perf_counters().object_allocations[kind].load(std::memory_order_relaxed);
    expect_true(result, ok && answer.tag == ValueTag::Int64 && answer.as.i64 == 496,
        std::string("expanded constructor proof preserves all 32 initialized values: ") + name);
    expect_true(result, after == before + 1,
        std::string("exactly one cold bound initializer allocation then warm inherited/own calls: ") + name);
  }
  Value base, child, sibling;
  error.clear();
  if (!module_get_attr(globals, "AuditBase", base, error) ||
      !module_get_attr(globals, "AuditInherited", child, error) ||
      !module_get_attr(globals, "AuditMetaInherited", sibling, error)) {
    expect_true(result, false, "rename proof obtains live base and both descendants");
    return;
  }
  const auto base_version = value_as_class(base)->version;
  const auto child_version = value_as_class(child)->version;
  const auto sibling_version = value_as_class(sibling)->version;
  const auto before = xlang_perf_counters().object_allocations[kind].load(std::memory_order_relaxed);
  const Value args[] = {child, Value::int64(32), Value::boolean(true)};
  Value answer;
  const bool ok = runtime_call_callable(runtime, exercise, args, 3, answer, error);
  const auto after = xlang_perf_counters().object_allocations[kind].load(std::memory_order_relaxed);
  expect_true(result, ok && answer.tag == ValueTag::Int64 && answer.as.i64 == 496 &&
      value_as_class(base)->name == "AuditRenamedBase" &&
      value_as_class(base)->version != base_version &&
      value_as_class(child)->version != child_version &&
      value_as_class(sibling)->version != sibling_version,
      "cold name publication invalidates both descendants without changing initialized values");
  expect_true(result, after == before + 2,
      "same live expanded-call cache deopts once on base rename and reacquires the bound initializer");
}
} // namespace xlang3::test
