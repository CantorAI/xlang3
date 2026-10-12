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
