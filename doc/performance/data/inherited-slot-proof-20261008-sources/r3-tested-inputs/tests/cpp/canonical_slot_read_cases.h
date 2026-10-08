/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once

#include "test_harness.h"
#include "xlang3/object_model.h"
#include "xlang3/module_object.h"
#include "xlang3/sequence.h"
#include "executor/xlang_vm/xlang_vm_attr.h"
#include <memory>
#include "serialize/block_stream.h"
#include "serialize/value_graph.h"

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
  // Custom test helpers belong to the executed module namespace. Updating
  // Runtime's builtin registry does not update captured Python builtins.
  Value globals = Value::module("canonical_slot_hook_test");
  std::string setup_error;
  expect_true(result, module_set_attr(globals, "_slot_install",
      runtime.make_native_function("_slot_install", canonical_slot_test_install, &audit),
      setup_error), "native install helper is visible to the test module");
  expect_true(result, module_set_attr(globals, "_slot_detach",
      runtime.make_native_function("_slot_detach", canonical_slot_test_detach),
      setup_error), "native detach helper is visible to the test module");

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
        actual = item.x
        assert actual == expected, ('local', actual, expected)

def check_getattr(item, expected):
    assert getattr(item, 'x') == expected, 'getattr'
    assert getattr(item, 'x', -1) == expected, 'getattr default'
    assert getattr(item, 'missing', -1) == -1, 'missing default'

first, second = SlotHookOwner(), SlotHookOwner()
first.x, second.x = 11, 23
check_local(first, 11)
_slot_install(first, 111)
check_local(first, 111)
check_getattr(first, 111)
assert first.x == 111, 'module first read'
_slot_detach(first)
check_local(first, 11)

_slot_install(second, 223)
for unused in range(6):
    check_local(first, 11)
    check_local(second, 223)
    check_local(first, 11)
check_getattr(second, 223)
assert second.x == 223, 'module second read'
_slot_detach(second)
check_local(second, 23)
class InheritedSlotHookOwner(SlotHookOwner):
    __slots__ = ()
third = InheritedSlotHookOwner()
third.x = 31
check_local(third, 31)
_slot_install(third, 331)
check_local(third, 331)
check_getattr(third, 331)
_slot_detach(third)
check_local(third, 31)
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
  auto module = std::make_shared<ir::Module>(std::move(lowered.module));
  auto run = interpreter.run_module(*module, globals, module);
  for (const auto& error : run.errors)
    expect_true(result, false, "native hook runtime diagnostic: " + error);
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

namespace xlang3::test {

inline void check_canonical_slot_read_eligibility(CaseResult& result) {
  std::ostringstream output;
  Runtime runtime(output);
  const auto make_owner = [](const char* name) {
    return Value::class_object(name, {}, Value::invalid(), {"padding", "x"});
  };
  Value owner = make_owner("CanonicalSlotReadOwner");
  Value receiver = Value::instance(owner);
  auto* instance = value_as_instance(receiver);
  instance_slot_at(instance, 0) = Value::none();
  instance_slot_at(instance, 1) = Value::int64(41);
  std::string error;
  Value found;
  AttrSiteCache cache;
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && cache.index == 1 &&
      cache.value.tag == ValueTag::Invalid && found.tag == ValueTag::Int64 &&
      found.as.i64 == 41,
      "initialized exact-owner canonical slot installs its actual nonowning index");
  instance_slot_at(instance, 1) = Value::int64(43);
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      found.tag == ValueTag::Int64 && found.as.i64 == 43,
      "warm slot read sees current storage rather than a cached payload");
  value_set_invalid(instance_slot_at(instance, 1));
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && value_as_slot_descriptor(found) != nullptr,
      "deleted slots restore raw descriptor dispatch for the VM's missing/getattr semantics");
  instance_slot_at(instance, 1) = Value::int64(47);
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 47,
      "reinitialized exact-owner slot regains eligibility after missing fallback");

  Value descriptor;
  expect_true(result, object_get_attr(owner, "x", descriptor, error),
      "eligibility probes obtain the owner's canonical descriptor");
  expect_true(result, object_set_attr(owner, "alias", descriptor, error),
      "descriptor alias is installed with normal class invalidation");
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(receiver, "alias", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && value_is(found, descriptor),
      "aliased descriptor name must remain the original descriptor operation");

  Value inherited = Value::class_object("CanonicalSlotInherited", {}, owner, {"extra"});
  Value inherited_receiver = Value::instance(inherited);
  const auto inherited_x = value_as_class(inherited)->instance_slot_indices.at("x");
  instance_slot_at(value_as_instance(inherited_receiver), inherited_x) = Value::int64(53);
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(inherited_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && cache.index == inherited_x && found.as.i64 == 53,
      "known unique inherited declarations install the receiver's effective index");

  // This dynamic-type shape exposes declaration dedup: class construction may
  // flatten x onto the inherited slot and omit its own descriptor. A descriptor
  // count alone cannot prove that the Python declaration was unambiguous.
  Value duplicate = Value::class_object("CanonicalSlotDuplicate",
      {{"__slots__", Value::tuple({Value::string("x")})}}, owner);
  Value duplicate_receiver = Value::instance(duplicate);
  instance_slot_at(value_as_instance(duplicate_receiver),
      value_as_class(duplicate)->instance_slot_indices.at("x")) = Value::int64(59);
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(duplicate_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor,
      "hidden duplicate declarations must not become canonical index hits");

  // Also reject an own descriptor naming an ancestor's flattened slot. This
  // exercises the ancestor-layout guard independently of owner identity.
  Value own_duplicate_descriptor = slot_descriptor("CanonicalSlotDuplicate", "x", inherited_x);
  slot_descriptor_set_owner_class(own_duplicate_descriptor, duplicate);
  expect_true(result, object_set_attr(duplicate, "x", own_duplicate_descriptor, error),
      "own duplicate descriptor setup succeeds");
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(duplicate_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && value_is(found, own_duplicate_descriptor),
      "exact owner alone must not authorize an ancestor's same-named storage");

  Value foreign = make_owner("CanonicalSlotForeign");
  Value foreign_descriptor;
  expect_true(result, object_get_attr(foreign, "x", foreign_descriptor, error) &&
      object_set_attr(owner, "x", foreign_descriptor, error),
      "foreign owner descriptor setup uses normal class mutation");
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && value_is(found, foreign_descriptor),
      "wrong-owner descriptor must retain its original applicability checks");
  expect_true(result, object_set_attr(owner, "x", descriptor, error),
      "canonical descriptor restores after foreign-owner fallback");

  // An impossible class/descriptor shape stays generic without repeating the
  // promotion proof on a warmed Descriptor entry. A different receiver class
  // or descriptor replacement must invalidate that negative result.
  Value unknown_inherited = Value::class_object("CanonicalSlotUnknownInherited", {}, owner, {"extra"});
  class_forget_slot_declarations(value_as_class(unknown_inherited));
  Value unknown_receiver = Value::instance(unknown_inherited);
  instance_slot_at(value_as_instance(unknown_receiver), inherited_x) = Value::int64(53);
  cache = AttrSiteCache{};
  for (int repetition = 0; repetition < 3; ++repetition) {
    expect_true(result, xlang_vm_load_attr_cached(
            unknown_receiver, "x", cache, found, error) &&
        cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
        value_is(found, descriptor),
        "unknown inherited shape rejection remains Descriptor-only and guarded on repeated hits");
  }
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && cache.index == 1 && found.as.i64 == 47,
      "a negative unknown cache must not poison an exact-owner instance at the same site");

  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(receiver, "alias", cache, found, error) &&
      cache.index == UINT32_MAX && cache.kind == AttrSiteKind::Descriptor,
      "alias shape rejection receives the Descriptor-only sentinel");
  Value alias_changed = slot_descriptor("CanonicalSlotReadOwner", "alias", 1);
  slot_descriptor_set_owner_class(alias_changed, owner);
  // This is intentionally not a new layout: renamed descriptor metadata cannot
  // prove a matching class index, so it must stay generic after version change.
  expect_true(result, object_set_attr(owner, "alias", alias_changed, error) &&
      xlang_vm_load_attr_cached(receiver, "alias", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX &&
      value_is(found, alias_changed),
      "class mutation returns the new descriptor while old owning cache cleanup stays retryable");
  expect_true(result, xlang_vm_load_attr_cached(receiver, "alias", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
      value_is(found, alias_changed),
      "the following safe warm read recomputes the changed alias's negative shape");

  cache = AttrSiteCache{};
  value_set_invalid(instance_slot_at(instance, 1));
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX,
      "uninitialized receiver state must remain retryable, never a class-shape rejection");
  instance_slot_at(instance, 1) = Value::int64(47);
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 47,
      "a later initialized receiver must promote without requiring a class-version change");

  Value foreign_restore_probe;
  expect_true(result, object_set_attr(owner, "x", foreign_descriptor, error) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, foreign_restore_probe, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX,
      "wrong-owner rejection can become a guarded negative descriptor cache");
  expect_true(result, object_set_attr(owner, "x", descriptor, error) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX &&
      value_is(found, descriptor),
      "restoring canonical descriptor clears negative eligibility while old owner cleanup stays generic");
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 47,
      "the following safe canonical hit promotes after restored descriptor cleanup");


  // An arbitrary stale cache owner may run a finalizer on release; the proof
  // must decline promotion and leave cleanup to original descriptor dispatch.
  bool old_cache_released = false;
  cache = AttrSiteCache{};
  cache.value = Value::native_function(0, "old_slot_cache", nullptr,
      &old_cache_released, [](void* state) { *static_cast<bool*>(state) = true; },
      nullptr, false, nullptr, true);
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      old_cache_released && cache.kind == AttrSiteKind::Descriptor &&
      value_is(found, descriptor),
      "unsafe old cache values must use original cleanup and descriptor entry");

  expect_true(result, cache.index != UINT32_MAX &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 47,
      "unsafe old cache release must not poison the following safe canonical hit");

  // A real property/function owner can become the sole cached owner after
  // class mutation. Cold rejected/retryable installation must drop its weak
  // accessor flags BEFORE releasing that old property and function.
  auto property_module = std::make_shared<ir::Module>();
  property_module->functions.emplace_back();
  auto& property_function = property_module->functions.back();
  property_function.params = {"self"};
  property_function.locals = {"self"};
  property_function.register_count = 2;
  property_function.code = {
      {ir::Op::LoadLocal, 0, 0, 0, 0},
      {ir::Op::LoadInstanceSlot, 1, 0, 1, 0},
      {ir::Op::Return, 0, 1, 0, 0},
  };
  Value property_function_value = Value::function(
      0, {}, Value::none(), property_module);
  auto* old_accessor = value_as_function(property_function_value);
  Value old_property = Value::property(
      property_function_value, Value::none(), Value::none(), Value::none());
  expect_true(result, object_set_attr(owner, "x", old_property, error),
      "property transition starts with a live owning accessor");
  cache = AttrSiteCache{};
  cache.kind = AttrSiteKind::Descriptor;
  cache.owner = &value_as_class(owner)->header;
  cache.version = value_as_class(owner)->version;
  cache.value = old_property;
  cache.index = UINT32_MAX;
  cache.getter_inline = true;
  cache.getter_slot = 1;
  cache.accessor_function = old_accessor;
  cache.accessor_code_version = old_accessor->code_version;
  Value retained_constant = Value::list({Value::int64(79)});
  cache.getter_const = retained_constant;
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
      cache.getter_inline && cache.accessor_function == old_accessor,
      "same-descriptor warm property hit preserves its accessor despite negative slot shape");
  expect_true(result, object_set_attr(owner, "x", foreign_descriptor, error) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX &&
      !cache.getter_inline && cache.accessor_function == nullptr &&
      value_is(cache.getter_const, retained_constant) && value_is(found, foreign_descriptor),
      "property-to-foreign cleanup clears stale accessor while retaining retry and owning constants");
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
      !cache.getter_inline && cache.accessor_function == nullptr &&
      value_is(found, foreign_descriptor),
      "the following safe foreign-owner hit caches only the stable negative shape");
  expect_true(result, object_set_attr(owner, "x", old_property, error),
      "retry transition restores the real property accessor");
  cache = AttrSiteCache{};
  cache.kind = AttrSiteKind::Descriptor;
  cache.owner = &value_as_class(owner)->header;
  cache.version = value_as_class(owner)->version;
  cache.value = old_property;
  cache.index = UINT32_MAX;
  cache.getter_inline = true;
  cache.setter_inline = true;
  cache.deleter_inline = true;
  cache.getter_slot = 1;
  cache.accessor_function = old_accessor;
  cache.accessor_code_version = old_accessor->code_version;
  cache.getter_const = retained_constant;
  value_set_invalid(found);
  value_set_invalid(property_function_value);
  value_set_invalid(old_property);
  expect_true(result, object_set_attr(owner, "x", descriptor, error),
      "class replacement leaves old accessor owned only by the stale cache");
  value_set_invalid(instance_slot_at(instance, 1));
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX &&
      !cache.getter_inline && !cache.setter_inline && !cache.deleter_inline &&
      cache.accessor_function == nullptr && cache.accessor_code_version == 0 &&
      value_is(cache.getter_const, retained_constant) && value_is(found, descriptor),
      "property-to-uninitialized slot clears weak accessor before cleanup and keeps retry possible");
  instance_slot_at(instance, 1) = Value::int64(47);
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 47,
      "slot write after property cleanup permits promotion under unchanged class version");

  // Owning Descriptor caches still die at frame return. A negative scalar
  // marker must never justify retaining the descriptor or its owning class.
  ir::Module frame_module;
  frame_module.functions.emplace_back();
  frame_module.functions[0].register_count = 2;
  frame_module.functions[0].names = {"x"};
  frame_module.functions[0].code.push_back({ir::Op::LoadAttr, 1, 0, 0, 0});
  const std::vector<Value> frame_closure;
  XlangVMFrame frame(frame_module, 0, CallArgsView{}, frame_closure,
                    Value::none(), {}, 0, false);
  const auto descriptor_refs = descriptor.as.obj->refcnt.load();
  frame.instr_cache[0].domain = XlangVMCacheDomain::Attr;
  frame.instr_cache[0].attr.kind = AttrSiteKind::Descriptor;
  frame.instr_cache[0].attr.index = UINT32_MAX;
  frame.instr_cache[0].attr.value = descriptor;
  frame.clear_for_pop();
  expect_true(result, frame.instr_cache[0].attr.kind == AttrSiteKind::Empty &&
      descriptor.as.obj->refcnt.load() == descriptor_refs,
      "negative Descriptor marker must reset and release its owner on frame return");

  // Output cleanup can destroy the very storage borrowed by the hit, then
  // replace the descriptor. The result must be owned before either mutation;
  // a later access must observe the new class version and dispatch normally.
  Value finalizer_owner = make_owner("CanonicalSlotFinalizer");
  Value finalizer_receiver = Value::instance(finalizer_owner);
  instance_slot_at(value_as_instance(finalizer_receiver), 1) =
      Value::list({Value::int64(67)});
  Value replacement = Value::property(
      Value::none(), Value::none(), Value::none(), Value::none());
  struct OutputCleanup {
    Value* receiver;
    Value* klass;
    Value replacement;
    bool called = false;
    bool changed = false;
  } cleanup{&finalizer_receiver, &finalizer_owner, replacement};
  Value old_output = Value::native_function(0, "slot_output_cleanup", nullptr,
      &cleanup, [](void* opaque) {
        auto& state = *static_cast<OutputCleanup*>(opaque);
        state.called = true;
        value_set_invalid(instance_slot_at(value_as_instance(*state.receiver), 1));
        std::string error;
        state.changed = object_set_attr(*state.klass, "x", state.replacement, error);
      }, nullptr, false, nullptr, true);
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(
          finalizer_receiver, "x", cache, old_output, error) &&
      cleanup.called && cleanup.changed && value_as_list(old_output) != nullptr &&
      value_as_list(old_output)->items[0].as.i64 == 67,
      "canonical hit must own result before old output clears slot and replaces descriptor");
  expect_true(result, xlang_vm_load_attr_cached(
          finalizer_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && value_is(found, replacement),
      "next read must observe descriptor replacement performed by output finalizer");

  Value alias_owner = make_owner("CanonicalSlotSoleReceiver");
  Value alias_receiver = Value::instance(alias_owner);
  instance_slot_at(value_as_instance(alias_receiver), 1) = Value::list({Value::int64(71)});
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(
          alias_receiver, "x", cache, alias_receiver, error) &&
      value_as_list(alias_receiver) != nullptr &&
      value_as_list(alias_receiver)->items[0].as.i64 == 71,
      "result survives when destination replaces the sole receiver owner");
}

inline void check_inherited_slot_declaration_proof(CaseResult& result) {
  std::ostringstream output;
  Runtime runtime(output);
  std::string error;
  Value declarations = Value::list({Value::string("padding"), Value::string("x")});
  Value base = Value::class_object("InheritedProofBase", {{"__slots__", declarations}});
  Value child = Value::class_object("InheritedProofChild", {}, base, {"extra"});
  Value receiver = Value::instance(child);
  auto* instance = value_as_instance(receiver);
  const auto index = value_as_class(child)->instance_slot_indices.at("x");
  instance_slot_at(instance, index) = Value::int64(101);
  Value descriptor;
  const bool canonical_ok = object_get_attr(base, "x", descriptor, error) &&
      value_as_slot_descriptor(descriptor) != nullptr;
  expect_true(result, canonical_ok,
      "inherited proof obtains canonical owner descriptor");
  if (!canonical_ok) return;
  AttrSiteCache cache;
  Value found;
  const auto descriptor_refs = descriptor.as.obj->refcnt.load();
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && cache.index == index &&
      cache.owner == &value_as_class(child)->header && cache.value.tag == ValueTag::Invalid &&
      found.tag == ValueTag::Int64 && found.as.i64 == 101 &&
      descriptor.as.obj->refcnt.load() == descriptor_refs,
      "unique inherited declaration installs receiver index without retaining descriptor");
  value_as_list(declarations)->items.clear();
  expect_true(result, object_delete_attr(base, "__slots__", error),
      "declaration namespace may be deleted after construction");
  cache = AttrSiteCache{};
  expect_true(result, value_as_class(base)->own_instance_slot_declarations ==
          std::vector<std::string>({"padding", "x"}) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 101,
      "immutable declaration history survives original list mutation and attribute deletion");
  value_set_invalid(instance_slot_at(instance, index));
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index != UINT32_MAX && value_is(found, descriptor),
      "deleted inherited storage returns to retryable original descriptor dispatch");
  instance_slot_at(instance, index) = Value::int64(103);
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 103,
      "live write restores inherited promotion without class mutation");

  Value hidden = Value::class_object("InheritedProofHiddenDuplicate",
      {{"__slots__", Value::tuple({Value::string("x")})}}, base);
  Value hidden_receiver = Value::instance(hidden);
  instance_slot_at(value_as_instance(hidden_receiver),
      value_as_class(hidden)->instance_slot_indices.at("x")) = Value::int64(107);
  expect_true(result, value_as_class(hidden)->attrs.find("x") == value_as_class(hidden)->attrs.end() &&
      value_as_class(hidden)->own_instance_slot_declarations == std::vector<std::string>({"x"}),
      "hidden duplicate has durable own declaration even when flattened layout omitted its descriptor");
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(hidden_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX,
      "durable own occurrence count rejects hidden cross-class duplicate");
  Value repeated = Value::class_object("InheritedProofRepeated",
      {{"__slots__", Value::tuple({Value::string("x"), Value::string("x")})}});
  Value repeated_child = Value::class_object("InheritedProofRepeatedChild", {}, repeated);
  Value repeated_receiver = Value::instance(repeated_child);
  instance_slot_at(value_as_instance(repeated_receiver), 0) = Value::int64(109);
  cache = AttrSiteCache{};
  expect_true(result, value_as_class(repeated)->own_instance_slot_declarations.size() == 2 &&
      xlang_vm_load_attr_cached(repeated_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX,
      "inherited proof retains and rejects repeated own declaration occurrences");

  expect_true(result, object_set_attr(base, "alias", descriptor, error),
      "inherited alias setup succeeds");
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(receiver, "alias", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX,
      "inherited descriptor alias remains generic");
  Value foreign = Value::class_object("InheritedProofForeign", {}, Value::invalid(), {"x"});
  Value foreign_descriptor;
  expect_true(result, object_get_attr(foreign, "x", foreign_descriptor, error) &&
      object_set_attr(base, "x", foreign_descriptor, error),
      "foreign inherited descriptor setup succeeds");
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX && value_is(found, foreign_descriptor),
      "foreign descriptor owner outside MRO cannot authorize inherited storage");
  expect_true(result, object_set_attr(base, "x", descriptor, error),
      "canonical inherited descriptor restores");
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot,
      "restored owner descriptor regains inherited eligibility");

  CanonicalSlotHookState attached{113};
  const auto original_version = value_as_class(child)->version;
  expect_true(result, instance_set_native_data(receiver, "InheritedProofHook", &attached, nullptr, error) &&
      instance_set_native_attr_hooks(receiver, canonical_slot_test_get_attr, nullptr, nullptr, error) &&
      value_as_class(child)->version == original_version &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Empty && found.as.i64 == 113 && attached.calls == 1,
      "native hook attached after inherited promotion must dispatch under unchanged class version");
  expect_true(result, instance_set_native_attr_hooks(receiver, nullptr, nullptr, nullptr, error) &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 103,
      "native hook detachment permits inherited promotion again");
  expect_true(result, instance_set_native_data(receiver, "", nullptr, nullptr, error),
      "serialization fixture detaches nonserializable native test state");

  serialize::BlockStream stream;
  expect_true(result, serialize::write_value_graph(runtime, stream, receiver, error),
      "inherited receiver graph serializes");
  std::string bytes(static_cast<size_t>(stream.Size()), '\0');
  expect_true(result, !bytes.empty() && stream.FullCopyTo(bytes.data(),
      static_cast<serialize::STREAM_SIZE>(bytes.size())), "graph bytes retain full serialized payload");
  serialize::BlockStream input(bytes.data(), static_cast<serialize::STREAM_SIZE>(bytes.size()), false);
  Value restored;
  const bool restored_ok = serialize::read_value_graph(runtime, input, restored, error);
  expect_true(result, restored_ok, "inherited receiver graph restores");
  if (restored_ok) {
    auto* restored_instance = value_as_instance(restored);
    auto* restored_class = restored_instance == nullptr ? nullptr : value_as_class(restored_instance->klass);
    cache = AttrSiteCache{};
    expect_true(result, restored_class != nullptr && !restored_class->own_instance_slot_declarations_known &&
        xlang_vm_load_attr_cached(restored, "x", cache, found, error) &&
        cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
        instance_slot_at(restored_instance, index).as.i64 == 103,
        "serialized inherited layouts explicitly stay unknown and retain original storage");
  }

  Value native_child = Value::class_object("InheritedProofNativeChild", {}, base, {"extra"});
  Value native_grandchild = Value::class_object("InheritedProofNativeGrandchild", {}, native_child);
  Value native_receiver = Value::instance(native_grandchild);
  instance_slot_at(value_as_instance(native_receiver), index) = Value::int64(127);
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(native_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot, "native mutation probe starts promoted");
  Value empty_base = Value::class_object("InheritedProofEmptyBase", {});
  const auto native_version = value_as_class(native_child)->version;
  const auto native_grandchild_version = value_as_class(native_grandchild)->version;
  expect_true(result, class_set_base(native_child, empty_base, error) &&
      !value_as_class(native_child)->own_instance_slot_declarations_known &&
      value_as_class(native_child)->version != native_version &&
      value_as_class(native_grandchild)->version != native_grandchild_version &&
      xlang_vm_load_attr_cached(native_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX,
      "SDK-compatible base mutation invalidates warm inherited cache and marks history unknown");

  Value rebind_base = Value::class_object("InheritedProofRebindBase", {}, Value::invalid(), {"x"});
  Value rebind_child = Value::class_object("InheritedProofRebindChild", {}, rebind_base);
  Value rebind_receiver = Value::instance(rebind_child);
  instance_slot_at(value_as_instance(rebind_receiver), 0) = Value::int64(131);
  Value rebound_descriptor;
  expect_true(result, object_get_attr(rebind_base, "x", rebound_descriptor, error),
      "rebind probe obtains descriptor");
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(rebind_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot, "rebind probe starts promoted");
  const auto rebind_version = value_as_class(rebind_child)->version;
  slot_descriptor_set_owner_class(rebound_descriptor, foreign);
  expect_true(result, value_as_class(rebind_child)->version != rebind_version &&
      !value_as_class(rebind_base)->own_instance_slot_declarations_known &&
      xlang_vm_load_attr_cached(rebind_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX,
      "real native owner rebind invalidates descendants and cannot retain inherited proof");

  Value lifetime_base = Value::class_object("InheritedProofLifetimeBase", {}, Value::invalid(), {"x"});
  Value lifetime_child = Value::class_object("InheritedProofLifetimeChild", {}, lifetime_base);
  Value lifetime_receiver = Value::instance(lifetime_child);
  instance_slot_at(value_as_instance(lifetime_receiver), 0) = Value::list({Value::int64(137)});
  struct Cleanup { Value* receiver; Value* base; Value replacement; bool called = false; bool changed = false; };
  Cleanup cleanup{&lifetime_receiver, &lifetime_base,
      Value::property(Value::none(), Value::none(), Value::none(), Value::none())};
  Value old_output = Value::native_function(0, "inherited_slot_cleanup", nullptr, &cleanup,
      [](void* opaque) {
        auto& state = *static_cast<Cleanup*>(opaque);
        state.called = true;
        value_set_invalid(instance_slot_at(value_as_instance(*state.receiver), 0));
        std::string error;
        state.changed = object_set_attr(*state.base, "x", state.replacement, error);
      }, nullptr, false, nullptr, true);
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(lifetime_receiver, "x", cache, old_output, error) &&
      cleanup.called && cleanup.changed && value_as_list(old_output) != nullptr &&
      value_as_list(old_output)->items[0].as.i64 == 137,
      "inherited promotion owns result before output finalizer destroys storage and replaces base descriptor");
  expect_true(result, xlang_vm_load_attr_cached(lifetime_receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::Descriptor && value_is(found, cleanup.replacement),
      "next inherited read dispatches replacement after output-finalizer invalidation");
  Value sole_base = Value::class_object("InheritedProofSoleBase", {}, Value::invalid(), {"x"});
  Value sole_child = Value::class_object("InheritedProofSoleChild", {}, sole_base);
  Value sole_receiver = Value::instance(sole_child);
  instance_slot_at(value_as_instance(sole_receiver), 0) = Value::list({Value::int64(139)});
  cache = AttrSiteCache{};
  expect_true(result, xlang_vm_load_attr_cached(sole_receiver, "x", cache, sole_receiver, error) &&
      value_as_list(sole_receiver) != nullptr && value_as_list(sole_receiver)->items[0].as.i64 == 139,
      "inherited result survives replacing the sole receiver owner");
}
inline void check_displaced_inherited_slot_fallback(CaseResult& result) {
  std::ostringstream output;
  Runtime runtime(output);
  uint32_t fallback_calls = 0;
  Value fallback = runtime.make_native_function("displaced.__getattr__",
      [](Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* opaque) {
        if (argc != 2) { error = "displaced hook expects self and name"; return false; }
        ++*static_cast<uint32_t*>(opaque);
        value_set_int64(out, 211);
        return true;
      }, &fallback_calls);
  Value padding_base = Value::class_object("DisplacedPadding", {}, Value::invalid(), {"padding"});
  Value x_base = Value::class_object("DisplacedX", {}, Value::invalid(), {"x"});
  Value child = Value::class_object("DisplacedChild", {{"__getattr__", fallback}}, padding_base);
  std::string error;
  const bool layout_ok = class_set_base_for_construction(child, x_base, error);
  expect_true(result, layout_ok, "displaced secondary-base construction succeeds");
  if (!layout_ok) return;
  Value descriptor;
  const bool descriptor_ok = object_get_attr(x_base, "x", descriptor, error) &&
      value_as_slot_descriptor(descriptor) != nullptr;
  expect_true(result, descriptor_ok, "secondary base provides canonical x descriptor");
  if (!descriptor_ok) return;
  auto* slot = value_as_slot_descriptor(descriptor);
  const auto effective_index = value_as_class(child)->instance_slot_indices.at("x");
  expect_true(result, slot->index == 0 && effective_index == 1 &&
      value_as_class(child)->own_instance_slot_declarations_known,
      "test must expose known declarations with displaced raw descriptor index");
  Value receiver = Value::instance(child);
  instance_slot_at(value_as_instance(receiver), effective_index) = Value::int64(197);
  AttrSiteCache cache;
  Value found;
  for (int repetition = 0; repetition < 3; ++repetition) {
    expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
        cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX && value_is(found, descriptor),
        "displaced inherited descriptor must keep raw-index missing/getattr dispatch on warm hits");
  }
  Value globals = Value::module("displaced_inherited_slot_test");
  expect_true(result, module_set_attr(globals, "subject", receiver, error),
      "displaced VM subject is visible to its module");
  const auto execute = [&](const std::string& source) {
    auto parsed = parse_source(source);
    expect_true(result, parsed.errors.empty(), "displaced VM fixture parses");
    if (!parsed.errors.empty()) return;
    auto lowered = lower_to_ir(parsed.module);
    expect_true(result, lowered.errors.empty(), "displaced VM fixture lowers");
    if (!lowered.errors.empty()) return;
    Interpreter interpreter(runtime);
    auto module = std::make_shared<ir::Module>(std::move(lowered.module));
    const auto run = interpreter.run_module(*module, globals, module);
    for (const auto& diagnostic : run.errors)
      expect_true(result, false, "displaced VM diagnostic: " + diagnostic);
    expect_true(result, run.errors.empty(), "displaced VM preserves original descriptor behavior");
  };
  execute(R"PY(
def read(item):
    for unused in range(8):
        assert item.x == 211
read(subject)
assert subject.x == 211
)PY");
  expect_true(result, fallback_calls == 9,
      "invalid raw descriptor storage calls original getattr once for every local/module read");
  instance_slot_at(value_as_instance(receiver), slot->index) = Value::int64(199);
  execute("assert subject.x == 197\n");
  expect_true(result, fallback_calls == 9,
      "initialized raw index then permits original name remapping without spurious getattr");
}

inline void check_canonical_slot_read_cases(CaseResult& result) {
  check_canonical_slot_read_eligibility(result);
  check_canonical_slot_native_hooks(result);
  check_inherited_slot_declaration_proof(result);
  check_displaced_inherited_slot_fallback(result);
}

} // namespace xlang3::test
