#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"

namespace xlang3::test {

inline void check_inherited_call_ex_constructor(CaseResult& result) {
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
