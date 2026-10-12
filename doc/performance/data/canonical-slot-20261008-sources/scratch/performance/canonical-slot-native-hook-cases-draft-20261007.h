// Draft only: not registered, compiled, or executed. Copy to tests/cpp only
// after reviewing the R3 engine patch and preserve accepted Release first.
#pragma once

#include "test_harness.h"
#include "xlang3/object_model.h"
#include "executor/xlang_vm/xlang_vm_attr.h"
#include <memory>

namespace xlang3::test {

struct CanonicalSlotHookState {
  int64_t value = 0;
  uint64_t calls = 0;
};

struct CanonicalSlotHookAudit {
  std::vector<std::unique_ptr<CanonicalSlotHookState>> states;
  bool class_versions_unchanged = true;
};

inline bool canonical_slot_test_get_attr(
    const Value& self, const std::string& name, Value& out,
    std::string& error) {
  auto* instance = value_as_instance(self);
  auto* state = instance == nullptr ? nullptr
      : static_cast<CanonicalSlotHookState*>(instance->native_data);
  if (state == nullptr || name != "x") {
    error.clear();
    return false;
  }
  ++state->calls;
  value_assign_fast(out, Value::int64(state->value));
  return true;
}

inline bool canonical_slot_test_install(
    Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* context) {
  auto* audit = static_cast<CanonicalSlotHookAudit*>(context);
  auto* instance = argc == 2 ? value_as_instance(args[0]) : nullptr;
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (audit == nullptr || klass == nullptr || args[1].tag != ValueTag::Int64) {
    error = "invalid slot-hook installation arguments";
    return false;
  }
  const uint64_t old_version = klass->version;
  auto state = std::make_unique<CanonicalSlotHookState>();
  state->value = args[1].as.i64;
  if (!instance_set_native_data(
          args[0], "CanonicalSlotHookTest", state.get(), nullptr, error) ||
      !instance_set_native_attr_hooks(
          args[0], canonical_slot_test_get_attr, nullptr, nullptr, error))
    return false;
  audit->class_versions_unchanged &= klass->version == old_version;
  audit->states.push_back(std::move(state));
  value_set_none(out);
  return true;
}

inline bool canonical_slot_test_detach(
    Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  if (argc != 1 || !instance_set_native_attr_hooks(
          args[0], nullptr, nullptr, nullptr, error)) return false;
  value_set_none(out);
  return true;
}

inline void check_canonical_slot_native_hooks(CaseResult& result) {
  CanonicalSlotHookAudit audit; // Must outlive Runtime's instance teardown.
  std::ostringstream output;
  Runtime runtime(output);
  runtime.register_builtin("_slot_install", runtime.make_native_function(
      "_slot_install", canonical_slot_test_install, &audit));
  runtime.register_builtin("_slot_detach", runtime.make_native_function(
      "_slot_detach", canonical_slot_test_detach));

  // Check actual installation and the outlined warm entry directly. The hook
  // attaches to the same object without a class-version change. A class-only
  // guard is insufficient even if installation correctly refused hooked data.
  Value klass = Value::class_object("CanonicalSlotHookOwner", {},
                                   Value::invalid(), {"x"});
  Value plain = Value::instance(klass);
  auto* plain_instance = value_as_instance(plain);
  instance_slot_at(plain_instance, 0) = Value::int64(11);
  AttrSiteCache cache;
  Value out;
  std::string error;
  expect_true(result, xlang_vm_load_attr_cached(plain, "x", cache, out, error) &&
      out.tag == ValueTag::Int64 && out.as.i64 == 11 &&
      cache.kind == AttrSiteKind::InstanceSlot &&
      cache.value.tag == ValueTag::Invalid,
      "proven initialized canonical slot must install a nonowning index cache");
  const uint64_t original_version = value_as_class(klass)->version;
  CanonicalSlotHookState attached{111};
  expect_true(result, instance_set_native_data(
          plain, "CanonicalSlotHookTest", &attached, nullptr, error) &&
      instance_set_native_attr_hooks(
          plain, canonical_slot_test_get_attr, nullptr, nullptr, error) &&
      value_as_class(klass)->version == original_version,
      "native hook attachment must exercise unchanged class-version guards");
  expect_true(result, xlang_vm_load_attr_cached(plain, "x", cache, out, error) &&
      out.tag == ValueTag::Int64 && out.as.i64 == 111 && attached.calls == 1 &&
      cache.kind == AttrSiteKind::Empty,
      "outlined warmed slot read must consult newly attached instance hook");
  expect_true(result, instance_set_native_attr_hooks(
          plain, nullptr, nullptr, nullptr, error) &&
      xlang_vm_load_attr_cached(plain, "x", cache, out, error) &&
      out.as.i64 == 11 && cache.kind == AttrSiteKind::InstanceSlot,
      "detached hook must restore ordinary canonical slot eligibility");

  // Real VM execution covers LoadLocalAttr's delegated early AND late cached
  // reads. A fix guarding only the first InstanceSlot branch still fails here
  // because the second branch would return stored 11/23 instead of hook values.
  const std::string source = R"PY(
class SlotHookOwner:
    __slots__ = ('x',)

def check_local(item, expected):
    for unused in range(12):
        assert item.x == expected

def check_getattr(item, expected):
    assert getattr(item, 'x') == expected
    assert getattr(item, 'x', -1) == expected
    assert getattr(item, 'missing', -1) == -1

first, second = SlotHookOwner(), SlotHookOwner()
first.x, second.x = 11, 23
check_local(first, 11)
_slot_install(first, 111)
check_local(first, 111)
check_getattr(first, 111)
assert first.x == 111
_slot_detach(first)
check_local(first, 11)

_slot_install(second, 223)
for unused in range(6):
    check_local(first, 11)
    check_local(second, 223)
    check_local(first, 11)
check_getattr(second, 223)
assert second.x == 223
_slot_detach(second)
check_local(second, 23)
print('post-warm native hooks and mixed same-class instances retain dispatch: OK')
)PY";
  auto parsed = parse_source(source);
  expect_true(result, parsed.errors.empty(), "native slot hook source parses");
  if (!parsed.errors.empty()) return;
  auto lowered = lower_to_ir(parsed.module);
  expect_true(result, lowered.errors.empty(), "native slot hook source lowers");
  if (!lowered.errors.empty()) return;
  bool has_local_attr = false;
  for (const auto& function : lowered.module.functions)
    for (const auto& instruction : function.code)
      has_local_attr |= instruction.op == ir::Op::LoadLocalAttr;
  expect_true(result, has_local_attr,
      "hook fixture must exercise dynamic LoadLocalAttr, not unrelated slot IR");
  Interpreter interpreter(runtime);
  auto run = interpreter.run(std::make_shared<ir::Module>(std::move(lowered.module)));
  expect_true(result, run.errors.empty(), "VM native hook assertions pass");
  expect_true(result, output.str() ==
      "post-warm native hooks and mixed same-class instances retain dispatch: OK\n",
      "VM native hook case reaches every phase");
  uint64_t hook_calls = 0;
  for (const auto& state : audit.states) hook_calls += state->calls;
  expect_true(result, audit.class_versions_unchanged && hook_calls >= 12 * 7,
      "actual native callbacks run after unchanged-version attachment and mixed reads");
}

} // namespace xlang3::test
