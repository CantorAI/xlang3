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
