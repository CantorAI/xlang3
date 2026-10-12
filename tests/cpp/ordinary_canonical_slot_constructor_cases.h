#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"

namespace xlang3::test {
namespace ordinary_canonical_slot_constructor_cases {
struct CounterScope {
  bool previous = xlang_perf_counters().enabled.load(std::memory_order_relaxed);
  CounterScope() { xlang_perf_set_enabled(true); }
  ~CounterScope() { xlang_perf_set_enabled(previous); }
};
inline uint64_t stores() {
  return xlang_perf_counters().opcode_dispatches[
      static_cast<size_t>(ir::Op::StoreAttr)].load(std::memory_order_relaxed);
}
struct PublicationState {
  Value klass;
  Value replacement;
  std::vector<int64_t> observed;
  unsigned cleanup = 0;
  bool active = true;
  bool mutation_ok = true;
};
inline void destroy_context(void* pointer) {
  delete static_cast<std::shared_ptr<PublicationState>*>(pointer);
}
inline void destroy_output(void* pointer) {
  auto& state = **static_cast<std::shared_ptr<PublicationState>*>(pointer);
  ++state.cleanup;
  if (state.active) {
    std::string error;
    state.mutation_ok &= object_set_attr(state.klass, "__init__", state.replacement, error);
  }
  destroy_context(pointer);
}
inline bool token_call(Runtime&, const Value*, uint32_t, Value& out,
    std::string& error, void*) {
  out = Value::none(); error.clear(); return true;
}
inline bool make_output(Runtime&, const Value*, uint32_t argc, Value& out,
    std::string& error, void* pointer) {
  if (argc != 0) { error = "unexpected publication factory arity"; return false; }
  const auto& state = *static_cast<std::shared_ptr<PublicationState>*>(pointer);
  auto owner = std::make_unique<std::shared_ptr<PublicationState>>(state);
  out = Value::native_function(0, "old_constructor_output", token_call,
      owner.get(), destroy_output);
  owner.release(); error.clear(); return true;
}
inline bool record(Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* pointer) {
  auto& state = **static_cast<std::shared_ptr<PublicationState>*>(pointer);
  Value field;
  if (argc != 1 || !object_get_attr(args[0], "obj", field, error) ||
      field.tag != ValueTag::Int64) {
    if (error.empty()) error = "invalid publication result";
    return false;
  }
  state.observed.push_back(field.as.i64);
  out = Value::boolean(state.observed.size() < 2); error.clear(); return true;
}
inline Value callback(const char* name, NativeFunctionCallback call,
    const std::shared_ptr<PublicationState>& state) {
  auto owner = std::make_unique<std::shared_ptr<PublicationState>>(state);
  Value result = Value::native_function(0, name, call, owner.get(), destroy_context);
  owner.release(); return result;
}
struct PublicationReset {
  std::shared_ptr<PublicationState> state;
  ~PublicationReset() {
    // This guard is destroyed before Runtime. A failed proof/unwind may leave
    // a token in a traceback or module; later cleanup must not mutate a class.
    state->active = false;
    value_set_invalid(state->klass);
    value_set_invalid(state->replacement);
  }
};
inline bool native_constructor(Runtime&, const ClassObject& klass,
    const CallArgsView&, bool& handled, Value& out, std::string& error) {
  Value owner;
  owner.tag = ValueTag::Object;
  owner.as.obj = const_cast<Object*>(&klass.header);
  retain(owner);
  out = Value::instance(owner);
  handled = true;
  return object_set_attr(out, "obj", Value::int64(900), error);
}
struct ArgumentOwnershipState {
  unsigned cleanup = 0;
  unsigned created = 0;
  unsigned inspections = 0;
  uint32_t inspected_refs = 0;
};
inline void destroy_argument_context(void* pointer) {
  delete static_cast<std::shared_ptr<ArgumentOwnershipState>*>(pointer);
}
inline void destroy_argument(void* pointer) {
  ++(**static_cast<std::shared_ptr<ArgumentOwnershipState>*>(pointer)).cleanup;
  destroy_argument_context(pointer);
}
inline bool make_argument(Runtime&, const Value*, uint32_t argc, Value& out,
    std::string& error, void* pointer) {
  if (argc != 0) { error = "unexpected argument factory arity"; return false; }
  const auto& state = *static_cast<std::shared_ptr<ArgumentOwnershipState>*>(pointer);
  auto owner = std::make_unique<std::shared_ptr<ArgumentOwnershipState>>(state);
  out = Value::native_function(0, "constructor_argument_payload", token_call,
      owner.get(), destroy_argument);
  owner.release(); ++state->created; error.clear(); return true;
}
inline bool inspect_argument(Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* pointer) {
  auto& state = **static_cast<std::shared_ptr<ArgumentOwnershipState>*>(pointer);
  Value field;
  if (argc != 1 || !object_get_attr(args[0], "obj", field, error) ||
      value_as_native_function(field) == nullptr) {
    if (error.empty()) error = "constructor lost its owning argument";
    return false;
  }
  ++state.inspections;
  state.inspected_refs = field.as.obj->refcnt.load(std::memory_order_relaxed);
  // The constructed slot and this temporary are the two legitimate owners.
  // A third owner here is a dead explicit argument retained by the caller.
  out = Value::boolean(state.inspected_refs == 2 && state.cleanup + 1 == state.created);
  error.clear(); return true;
}
inline Value argument_callback(const char* name, NativeFunctionCallback call,
    const std::shared_ptr<ArgumentOwnershipState>& state) {
  auto owner = std::make_unique<std::shared_ptr<ArgumentOwnershipState>>(state);
  Value answer = Value::native_function(0, name, call, owner.get(), destroy_argument_context);
  owner.release(); return answer;
}
} // namespace ordinary_canonical_slot_constructor_cases

inline void check_class_canonical_slot_plan_persistence(CaseResult& result);

inline void check_ordinary_canonical_slot_constructor(CaseResult& result) {
  using namespace ordinary_canonical_slot_constructor_cases;
  const char* source = R"PY(
class OwnSlotBox:
    __slots__ = ('obj',)
    def __init__(self, obj):
        self.obj = obj

class OwnSlotAlias:
    __slots__ = ('obj',)
    def __init__(self, obj):
        self.alias = obj
OwnSlotAlias.alias = OwnSlotAlias.obj

class OwnSlotDuplicate:
    __slots__ = ('obj', 'obj')
    def __init__(self, obj):
        self.obj = obj

class InheritedSlotBox(OwnSlotBox):
    __slots__ = ()

class SetterSlotBox:
    __slots__ = ('obj',)
    def __init__(self, obj):
        self.obj = obj
    def __setattr__(self, name, value):
        object.__setattr__(self, name, value + 10)

class PropertySlotBox:
    __slots__ = ('obj',)
    def __init__(self, obj):
        self.obj = obj
property_slot = PropertySlotBox.obj
def property_get(self):
    return property_slot.__get__(self)
def property_set(self, value):
    property_slot.__set__(self, value + 100)
PropertySlotBox.obj = property(property_get, property_set)

class NewSlotBox:
    __slots__ = ('obj',)
    def __new__(cls, obj):
        return object.__new__(cls)
    def __init__(self, obj):
        self.obj = obj

class OwnMeta(type):
    def __call__(cls, obj):
        return super().__call__(obj + 20)
class MetaSlotBox(metaclass=OwnMeta):
    __slots__ = ('obj',)
    def __init__(self, obj):
        self.obj = obj

class PushedArgumentBox:
    __slots__ = ('obj',)
    def __init__(self, obj):
        self.obj = obj
    def __setattr__(self, name, value):
        object.__setattr__(self, name, value)

class OverwrittenArgumentBox:
    __slots__ = ('obj',)
    def __init__(self, first, second):
        self.obj = first
        self.obj = second

class PublicationSlotBox:
    __slots__ = ('obj',)
    def __init__(self, obj):
        self.obj = obj
def replacement_init(self, obj):
    self.obj = obj + 100

def same_frame(cls):
    total = 0
    for value in range(32):
        item = cls(value)
        total += item.obj
    return total
def returned_once(cls, value):
    return cls(value)
def returned_driver(cls):
    total = 0
    for value in range(32):
        item = returned_once(cls, value)
        total += item.obj
    return total
def sorted_key(value):
    return OwnSlotBox(value).obj
def sorted_driver():
    return sum(sorted(range(31, -1, -1), key=sorted_key))
)PY";
  auto parsed = parse_source(source);
  expect_true(result, parsed.errors.empty(), "canonical constructor proof parses");
  if (!parsed.errors.empty()) return;
  auto lowered = lower_to_ir(parsed.module);
  expect_true(result, lowered.errors.empty(), "canonical constructor proof lowers");
  if (!lowered.errors.empty()) return;
  bool dynamic_initializer = false;
  bool returned_ordinary_call = false;
  for (const auto& function : lowered.module.functions) {
    if (function.name == "__init__" && function.code.size() == 3 &&
        function.code[0].op == ir::Op::LoadLocalPair &&
        function.code[1].op == ir::Op::StoreAttr &&
        function.code[2].op == ir::Op::ReturnConst)
      dynamic_initializer = true;
    if (function.name == "returned_once")
      for (const auto& instruction : function.code)
        returned_ordinary_call |= instruction.op == ir::Op::Call;
  }
  expect_true(result, dynamic_initializer && returned_ordinary_call,
      "proof exercises dynamic initializer StoreAttr and ordinary Call, not already static-slot IR/CallEx");
  if (!dynamic_initializer || !returned_ordinary_call) return;

  // Native token owners and IR/module namespace outlive Runtime. Never retain
  // a raw Runtime/Interpreter pointer in a destructor or callback context.
  auto state = std::make_shared<PublicationState>();
  Value globals = Value::module("ordinary_canonical_slot_constructor_audit");
  auto mutable_module = std::make_shared<ir::Module>(std::move(lowered.module));
  const uint32_t publication_id = static_cast<uint32_t>(mutable_module->functions.size());
  mutable_module->functions.emplace_back();
  auto& publication = mutable_module->functions.back();
  publication.name = "publication_probe";
  publication.params = {"cls"}; publication.locals = {"cls"};
  publication.register_count = 6;
  publication.constants = {callback("make_output", make_output, state),
      Value::int64(1), Value::int64(2), callback("record_output", record, state)};
  publication.call_args = {{}, {3}, {1}};
  // The first Call's token is the sole old output owner at the reused class
  // Call at IP4. Its cleanup replaces __init__, so the second trip through
  // that SAME warmed site must not certify the old plan with a fresh version.
  publication.code = {{ir::Op::LoadConst, 0, 0, 0, 0},
      {ir::Op::Call, 1, 0, 0, 0}, {ir::Op::LoadLocal, 2, 0, 0, 0},
      {ir::Op::LoadConst, 3, 1, 0, 0}, {ir::Op::Call, 1, 2, 1, 0},
      {ir::Op::LoadConst, 4, 3, 0, 0}, {ir::Op::Call, 5, 4, 2, 0},
      {ir::Op::JumpIfFalse, 10, 5, 0, 0}, {ir::Op::LoadConst, 3, 2, 0, 0},
      {ir::Op::Jump, 4, 0, 0, 0}, {ir::Op::Return, 0, 1, 0, 0}};
  auto argument_state = std::make_shared<ArgumentOwnershipState>();
  std::vector<uint32_t> argument_probe_ids;
  // These caller functions are executed by the DLL. The test reads only its
  // published metadata and ownership effects; it does not compile a VM copy.
  for (unsigned shape = 0; shape != 4; ++shape) {
    argument_probe_ids.push_back(static_cast<uint32_t>(mutable_module->functions.size()));
    mutable_module->functions.emplace_back();
    auto& probe = mutable_module->functions.back();
    probe.name = "constructor_argument_probe_" + std::to_string(shape);
    probe.params = {"cls"}; probe.locals = {"cls"}; probe.register_count = 7;
    probe.constants = {argument_callback("make_argument", make_argument, argument_state),
        argument_callback("inspect_argument", inspect_argument, argument_state), Value::boolean(false)};
    const uint32_t instance_reg = shape == 1 ? 2 : 3;
    probe.call_args = {{}, {2}, {instance_reg}};
    probe.code = {{ir::Op::LoadLocal, 0, 0, 0, 0},
        {ir::Op::LoadConst, 1, 0, 0, 0}, {ir::Op::Call, 2, 1, 0, 0}};
    if (shape == 2) {
      probe.code.push_back({ir::Op::LoadConst, 6, 2, 0, 0});
      probe.code.push_back({ir::Op::JumpIfFalse, 5, 6, 0, 0});
    } else if (shape == 3) {
      probe.code.push_back({ir::Op::SetupExcept, 7, 0, 0, 0});
      probe.code.push_back({ir::Op::PopExcept, 0, 0, 0, 0});
    }
    probe.code.push_back({ir::Op::Call, instance_reg, 0, 1, 0});
    if (shape < 2) {
      probe.code.push_back({ir::Op::LoadConst, 4, 1, 0, 0});
      probe.code.push_back({ir::Op::Call, 5, 4, 2, 0});
      probe.code.push_back({ir::Op::Return, 0, 5, 0, 0});
    } else {
      probe.code.push_back({ir::Op::Return, 0, instance_reg, 0, 0});
      if (shape == 3) {
        probe.code.push_back({ir::Op::LoadException, 4, 0, 0, 0});
        probe.code.push_back({ir::Op::Return, 0, 4, 0, 0});
      }
    }
  }
  const uint32_t repeated_slot_probe_id = static_cast<uint32_t>(mutable_module->functions.size());
  mutable_module->functions.emplace_back();
  auto& repeated_slot = mutable_module->functions.back();
  repeated_slot.name = "constructor_repeated_slot_probe";
  repeated_slot.params = {"cls"}; repeated_slot.locals = {"cls"};
  repeated_slot.register_count = 4; repeated_slot.constants = {Value::int64(1), Value::int64(2)};
  repeated_slot.call_args = {{1, 2}};
  repeated_slot.code = {{ir::Op::LoadLocal, 0, 0, 0, 0},
      {ir::Op::LoadConst, 1, 0, 0, 0}, {ir::Op::LoadConst, 2, 1, 0, 0},
      {ir::Op::Call, 3, 0, 0, 0}, {ir::Op::Return, 0, 3, 0, 0}};
  std::shared_ptr<const ir::Module> module = mutable_module;
  std::ostringstream output;
  Runtime runtime(output);
  PublicationReset reset{state};
  Interpreter interpreter(runtime);
  auto setup = interpreter.run_module(*module, globals, module);
  expect_true(result, setup.errors.empty(), "canonical constructor setup uses actual DLL");
  if (!setup.errors.empty()) return;
  std::string error;
  Value same, returned, sorted;
  if (!module_get_attr(globals, "same_frame", same, error) ||
      !module_get_attr(globals, "returned_driver", returned, error) ||
      !module_get_attr(globals, "sorted_driver", sorted, error)) {
    expect_true(result, false, "canonical proof obtains its actual driver functions"); return;
  }
  CounterScope counters;
  auto check_driver = [&](const Value& driver, const char* name,
      int64_t expected, uint64_t expected_stores) {
    Value klass, answer;
    if (!module_get_attr(globals, name, klass, error)) {
      expect_true(result, false, std::string("missing canonical proof class ") + name); return;
    }
    const auto before = stores();
    error.clear();
    const bool ok = runtime_call_callable(runtime, driver, &klass, 1, answer, error);
    const auto actual_stores = stores() - before;
    expect_true(result, ok && answer.tag == ValueTag::Int64 && answer.as.i64 == expected &&
        actual_stores == expected_stores,
        std::string("actual DLL constructor stores for ") + name + ": expected=" +
        std::to_string(expected_stores) + ", actual=" + std::to_string(actual_stores) +
        ", error=" + error);
  };
  // Zero StoreAttr dispatches is the admission proof, independently of the
  // correct final sum. Returned wrappers and native sorted are cold per entry.
  check_driver(same, "OwnSlotBox", 496, 0);
  check_driver(returned, "OwnSlotBox", 496, 0);
  {
    Value answer;
    const auto before = stores();
    error.clear();
    const bool ok = runtime_call_callable(runtime, sorted, nullptr, 0, answer, error);
    expect_true(result, ok && answer.tag == ValueTag::Int64 && answer.as.i64 == 496 &&
        stores() == before, "native sorted callback enters actual zero-store canonical constructors");
  }
  for (const char* name : {"OwnSlotAlias", "OwnSlotDuplicate", "InheritedSlotBox", "NewSlotBox"})
    check_driver(same, name, 496, 32);
  check_driver(same, "SetterSlotBox", 816, 32);
  check_driver(same, "PropertySlotBox", 3696, 32);
  check_driver(same, "MetaSlotBox", 1136, 32);
  {
    Value klass;
    if (!module_get_attr(globals, "OwnSlotBox", klass, error)) {
      expect_true(result, false, "native constructor negative obtains live class"); return;
    }
    auto* class_object = value_as_class(klass);
    class_object->native_type_constructor = native_constructor;
    class_object->native_type_constructor_version = class_object->version;
    check_driver(same, "OwnSlotBox", 28800, 0);
    class_object->native_type_constructor = nullptr;
    class_object->native_type_constructor_version = 0;
  }
  {
    if (!module_get_attr(globals, "PublicationSlotBox", state->klass, error) ||
        !module_get_attr(globals, "replacement_init", state->replacement, error)) {
      expect_true(result, false, "publication proof obtains live class and replacement"); return;
    }
    Value function = Value::function(publication_id, {}, globals, module);
    CallArgsView arguments;
    arguments.leading = &state->klass; arguments.leading_count = 1;
    const auto before = stores();
    auto answer = interpreter.run_function_value(value_as_function(function), arguments);
    Value member;
    const bool member_ok = object_get_attr(answer.value, "obj", member, error);
    expect_true(result, answer.errors.empty() && member_ok && member.tag == ValueTag::Int64 &&
        member.as.i64 == 102 && state->cleanup == 1 && state->mutation_ok &&
        state->observed == std::vector<int64_t>{1, 102} && stores() == before + 1,
        "old-output cleanup invalidates the actual warmed site without stale-plan/code publication");
  }
  {
    Value klass;
    if (!module_get_attr(globals, "OverwrittenArgumentBox", klass, error)) {
      expect_true(result, false, "repeated slot proof obtains live class"); return;
    }
    Value function = Value::function(repeated_slot_probe_id, {}, globals, module);
    CallArgsView arguments; arguments.leading = &klass; arguments.leading_count = 1;
    const auto before_stores = stores();
    auto answer = interpreter.run_function_value(value_as_function(function), arguments);
    Value member;
    expect_true(result, answer.errors.empty() && object_get_attr(answer.value, "obj", member, error) &&
        member.tag == ValueTag::Int64 && member.as.i64 == 2 && stores() == before_stores + 2,
        "duplicate physical slot stores keep both initializer dispatches instead of unsafe promotion");
  }
  // A linear owning argument may retire only after the constructor has its
  // own copy. Alias and legacy branch/handler guards use the real entry too.
  for (const char* name : {"OwnSlotBox", "PushedArgumentBox"}) {
    Value klass;
    if (!module_get_attr(globals, name, klass, error)) {
      expect_true(result, false, "argument proof obtains live class"); return;
    }
    for (unsigned shape = 0; shape != argument_probe_ids.size(); ++shape) {
      const auto before_cleanup = argument_state->cleanup;
      const auto before_inspections = argument_state->inspections;
      Value function = Value::function(argument_probe_ids[shape], {}, globals, module);
      CallArgsView arguments; arguments.leading = &klass; arguments.leading_count = 1;
      {
        auto answer = interpreter.run_function_value(value_as_function(function), arguments);
        const auto& actual_ir = module->functions[argument_probe_ids[shape]];
        auto metadata = std::atomic_load_explicit(&actual_ir.execution_metadata, std::memory_order_acquire);
        bool value_ok = answer.value.tag == ValueTag::Bool && answer.value.as.b;
        if (shape >= 2) {
          Value field;
          value_ok = object_get_attr(answer.value, "obj", field, error) &&
              value_as_native_function(field) != nullptr && argument_state->cleanup == before_cleanup;
        }
        expect_true(result, answer.errors.empty() && value_ok && metadata != nullptr &&
            metadata->linear_constructor_argument_liveness == (shape < 2) &&
            (shape >= 2 || argument_state->inspections == before_inspections + 1),
            std::string("DLL constructor argument liveness/alias/control guard: ") + name +
            "/" + std::to_string(shape));
      }
      expect_true(result, argument_state->cleanup == before_cleanup + 1,
          "constructor argument has exactly one cleanup after its last actual owner");
    }
  }
  check_class_canonical_slot_plan_persistence(result);
}

inline void check_class_canonical_slot_plan_persistence(CaseResult& result) {
  using namespace ordinary_canonical_slot_constructor_cases;
  auto parsed = parse_source(R"PY(
class PersistenceBox:
    __slots__ = ('obj', 'other')
    def __init__(self, obj):
        self.obj = obj
def persistence_wrapper(cls, value):
    return cls(value)
def persistence_key(value):
    return PersistenceBox(value).obj
def other_initializer(self, obj):
    self.other = obj
)PY");
  expect_true(result, parsed.errors.empty(), "class-plan public proof parses");
  if (!parsed.errors.empty()) return;
  auto lowered = lower_to_ir(parsed.module);
  expect_true(result, lowered.errors.empty(), "class-plan public proof lowers");
  if (!lowered.errors.empty()) return;
  Value globals = Value::module("class_constructor_plan_audit");
  auto module = std::make_shared<ir::Module>(std::move(lowered.module));
  std::ostringstream output;
  Runtime runtime(output);
  Interpreter interpreter(runtime);
  auto setup = interpreter.run_module(*module, globals, module);
  expect_true(result, setup.errors.empty(), "class-plan setup executes actual DLL");
  if (!setup.errors.empty()) return;
  Value klass, wrapper, key, initializer, replacement;
  std::string error;
  const bool found = module_get_attr(globals, "PersistenceBox", klass, error) &&
      module_get_attr(globals, "persistence_wrapper", wrapper, error) &&
      module_get_attr(globals, "persistence_key", key, error) &&
      module_get_attr(globals, "other_initializer", replacement, error) &&
      object_get_attr(klass, "__init__", initializer, error);
  expect_true(result, found, "class-plan proof obtains live public callables");
  if (!found) return;
  auto* class_object = value_as_class(klass);
  auto* function_object = value_as_function(initializer);
  expect_true(result, class_object != nullptr && function_object != nullptr &&
      class_object->canonical_slot_constructor_cache == nullptr,
      "class plan is absent before the first actual constructor call");
  if (class_object == nullptr || function_object == nullptr) return;
  CounterScope counters;
  const Value wrapper_args[] = {klass, Value::int64(3)};
  auto invoke = [&](const char* member, int64_t expected) {
    Value answer, field;
    error.clear();
    const bool okay = runtime_call_callable(runtime, wrapper, wrapper_args, 2, answer, error) &&
        object_get_attr(answer, member, field, error);
    const bool matched = okay && field.tag == ValueTag::Int64 && field.as.i64 == expected;
    value_set_invalid(field);
    value_set_invalid(answer);
    return matched;
  };
  const auto initial_stores = stores();
  expect_true(result, invoke("obj", 3) && stores() == initial_stores,
      "first actual DLL ordinary constructor publishes the zero-StoreAttr own-slot plan");
  const auto* node = class_object->canonical_slot_constructor_cache.get();
  expect_true(result, node != nullptr && node->class_version == class_object->version &&
      node->initializer == function_object &&
      node->initializer_code_version == function_object->code_version && node->slots.size() == 1,
      "successful cold selection publishes the current class/init physical plan");
  if (node == nullptr) return;
  const auto version = node->class_version;
  const auto class_refs = klass.as.obj->refcnt.load();
  const auto initializer_refs = initializer.as.obj->refcnt.load();
  bool all_keys = true;
  const auto before_keys = stores();
  for (int64_t value = 0; value < 32; ++value) {
    // Each public native-to-Python call creates/returns its own Interpreter.
    // A same-frame loop would not prove persistence across these entries.
    Value argument = Value::int64(value), answer;
    error.clear();
    all_keys &= runtime_call_callable(runtime, key, &argument, 1, answer, error) &&
        answer.tag == ValueTag::Int64 && answer.as.i64 == value &&
        class_object->canonical_slot_constructor_cache.get() == node &&
        class_object->canonical_slot_constructor_cache->class_version == version;
  }
  expect_true(result, all_keys && stores() == before_keys &&
      klass.as.obj->refcnt.load() == class_refs &&
      initializer.as.obj->refcnt.load() == initializer_refs,
      "same ownerless class plan survives 32 returned native callback entries without class/init roots");

  const auto before_class_version = class_object->version;
  error.clear();
  const bool class_changed = object_set_attr(klass, "_plan_marker", Value::int64(1), error);
  expect_true(result, class_changed && class_object->version != before_class_version && invoke("obj", 3) &&
      class_object->canonical_slot_constructor_cache != nullptr &&
      class_object->canonical_slot_constructor_cache->class_version == class_object->version,
      "class mutation deopts stale generation and rebuilds after valid cold selection");

  auto* metaclass = value_as_class(class_object->metaclass);
  expect_true(result, metaclass != nullptr, "class-plan proof has a live metaclass");
  if (metaclass == nullptr) return;
  const auto old_metaclass_version = metaclass->version;
  // This exported SDK layout-invalidation boundary retags the metaclass without
  // changing user behavior or installing a callback. Custom metaclass semantics
  // remain covered by the unchanged canonical constructor fixture/CPP cases.
  class_forget_slot_declarations(metaclass);
  expect_true(result, metaclass->version != old_metaclass_version && invoke("obj", 3) &&
      class_object->canonical_slot_constructor_cache != nullptr &&
      class_object->canonical_slot_constructor_cache->metaclass_version == metaclass->version,
      "metaclass generation invalidation cannot reuse the stale weak class plan");

  Value code;
  error.clear();
  const auto old_code_version = function_object->code_version;
  const bool code_changed = object_get_attr(replacement, "__code__", code, error) &&
      object_set_attr(initializer, "__code__", code, error);
  const bool other_set = code_changed && invoke("other", 3);
  const auto* rebuilt = class_object->canonical_slot_constructor_cache.get();
  const auto other_index = class_object->instance_slot_indices.find("other");
  expect_true(result, other_set && function_object->code_version != old_code_version &&
      rebuilt != nullptr && rebuilt->initializer == function_object &&
      rebuilt->initializer_code_version == function_object->code_version &&
      other_index != class_object->instance_slot_indices.end() && rebuilt->slots.size() == 1 &&
      rebuilt->slots[0].first == other_index->second,
      "initializer code replacement changes the actual physical store before rebuilding its guard");
}

} // namespace xlang3::test
