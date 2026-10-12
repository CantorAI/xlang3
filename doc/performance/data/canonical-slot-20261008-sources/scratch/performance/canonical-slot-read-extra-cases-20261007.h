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
      cache.kind == AttrSiteKind::Descriptor && value_is(found, descriptor),
      "initial controlled trial must keep inherited slots generic even when initialized");

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

inline void check_canonical_slot_read_cases(CaseResult& result) {
  check_canonical_slot_read_eligibility(result);
  check_canonical_slot_native_hooks(result);
}

} // namespace xlang3::test
