  // An impossible class/descriptor shape stays generic without repeating the
  // promotion proof on a warmed Descriptor entry. A different receiver class
  // or descriptor replacement must invalidate that negative result.
  cache = AttrSiteCache{};
  for (int repetition = 0; repetition < 3; ++repetition) {
    expect_true(result, xlang_vm_load_attr_cached(
            inherited_receiver, "x", cache, found, error) &&
        cache.kind == AttrSiteKind::Descriptor && cache.index == UINT32_MAX &&
        value_is(found, descriptor),
        "inherited shape rejection remains Descriptor-only and guarded on repeated hits");
  }
  expect_true(result, xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && cache.index == 1 && found.as.i64 == 47,
      "a negative inherited cache must not poison an exact-owner instance at the same site");

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

