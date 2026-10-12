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

