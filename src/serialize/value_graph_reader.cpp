/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "value_graph_internal.h"
#include "xlang3/expression.h"
#include "runtime_lock.h"
#include <algorithm>

namespace xlang3::serialize::graph {
namespace {
class Reader {
public:
  Reader(Runtime& runtime, XLangStream& stream) : runtime(runtime), io(stream) {}
  ~Reader() { if (!committed) for (size_t i = 0; i < owned.size(); ++i) if (owned[i]) clear_edges(values[i]); }
  Value run() {
    IO::require(io.number<uint32_t>() == magic && io.number<uint32_t>() == version, "unsupported value graph format");
    auto module_count = io.count();
    IO::require(module_count <= max_nodes, "too many code modules");
    for (uint32_t i = 0; i < module_count; ++i) {
      auto hash = io.number<uint64_t>();
      auto bytes = io.text();
      IO::require(hash == ir::source_hash64(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()), "serialized IR checksum mismatch");
      auto module = std::make_shared<ir::Module>();
      std::string error;
      if (!ir::decode_module(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), 0, *module, error)) throw std::runtime_error(error);
      modules.push_back(std::move(module));
    }
    auto count = io.count();
    IO::require(count <= max_nodes, "too many graph objects");
    IO::require(io.stream.CanRead(static_cast<int64_t>(count) * 21 + 9), "truncated graph object table");
    records.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      Record record;
      record.kind = static_cast<Kind>(io.number<uint8_t>());
      auto n = io.count();
      IO::require(io.stream.CanRead(static_cast<int64_t>(n) * 8), "truncated graph numbers");
      for (uint32_t j = 0; j < n; ++j) record.numbers.push_back(io.number<uint64_t>());
      n = io.count();
      IO::require(io.stream.CanRead(static_cast<int64_t>(n) * 8), "truncated graph names");
      for (uint32_t j = 0; j < n; ++j) record.names.push_back(io.text());
      record.payload = io.text();
      n = io.count();
      IO::require(io.stream.CanRead(static_cast<int64_t>(n) * 9), "truncated graph references");
      record.refs.reserve(n);
      for (uint32_t j = 0; j < n; ++j) record.refs.push_back(reference(count));
      records.push_back(std::move(record));
    }
    auto root = reference(count);
    values.resize(count);
    owned.resize(count, true);
    for (size_t i = 0; i < count; ++i) allocate(i);
    for (size_t i = 0; i < count; ++i) fill(i);
    validate();
    for (size_t i = 0; i < count; ++i) {
      auto& r = records[i];
      if (r.kind != Kind::NativeInstance) continue;
      auto* codec = runtime.find_native_codec(r.names.back());
      IO::require(codec && codec->version == r.numbers[1], "native serializer is missing or has an incompatible version");
      auto state = resolve(r.refs.back());
      std::string error;
      if (!codec->decode(values[i], state, error)) throw std::runtime_error(error);
      IO::require(value_as_instance(values[i])->native_type == codec->native_type,
          "native serializer did not restore the registered native type");
    }
    Value result = resolve(root);
    std::vector<Value> objects;
    for (size_t i = 0; i < count; ++i) if (owned[i]) objects.push_back(values[i]);
    runtime.retain_serialized_objects(std::move(objects));
    committed = true;
    return result;
  }
private:
  Runtime& runtime;
  IO io;
  bool committed = false;
  std::vector<Record> records;
  std::vector<Value> values;
  std::vector<bool> owned;
  std::vector<std::shared_ptr<const ir::Module>> modules;

  Reference reference(uint32_t count) {
    Reference ref{io.number<uint8_t>(), io.number<uint64_t>()};
    IO::require(ref.tag <= static_cast<uint8_t>(ValueTag::Object), "invalid graph value tag");
    if (ref.tag == static_cast<uint8_t>(ValueTag::Object)) IO::require(ref.bits < count, "invalid graph object id");
    if (ref.tag == static_cast<uint8_t>(ValueTag::Bool)) IO::require(ref.bits <= 1, "invalid graph boolean");
    return ref;
  }
  Value resolve(Reference ref) {
    switch (static_cast<ValueTag>(ref.tag)) {
      case ValueTag::Invalid: return Value::invalid();
      case ValueTag::None: return Value::none();
      case ValueTag::Bool: return Value::boolean(ref.bits != 0);
      case ValueTag::Int64: { int64_t v; std::memcpy(&v, &ref.bits, 8); return Value::int64(v); }
      case ValueTag::Double: { double v; std::memcpy(&v, &ref.bits, 8); return Value::number(v); }
      case ValueTag::Object: return values[static_cast<size_t>(ref.bits)];
    }
    throw std::runtime_error("invalid graph reference");
  }
  static void shape(const Record& r, size_t numbers, size_t names, size_t refs) {
    IO::require(r.numbers.size() == numbers && r.names.size() == names && r.refs.size() == refs, "invalid graph record shape");
  }
  void allocate(size_t i) {
    auto& r = records[i];
    auto& v = values[i];
    if (r.kind != Kind::String && r.kind != Kind::Bytes && r.kind != Kind::ByteArray && r.kind != Kind::Expression)
      IO::require(r.payload.empty(), "unexpected graph payload");
    switch (r.kind) {
      case Kind::String: shape(r, 0, 0, 0); v = Value::string(std::move(r.payload)); break;
      case Kind::Bytes: shape(r, 0, 0, 0); v = Value::bytes(std::move(r.payload)); break;
      case Kind::ByteArray: shape(r, 0, 0, 0); v = Value::bytearray(std::move(r.payload)); break;
      case Kind::List: shape(r, 0, 0, r.refs.size()); v = Value::list({}); break;
      case Kind::Tuple: shape(r, 0, 0, r.refs.size()); v = Value::tuple_reserved(r.refs.size()); break;
      case Kind::Dict: shape(r, 0, 0, r.refs.size()); v = Value::dict({}); break;
      case Kind::Cell: shape(r, 0, 0, 1); v = Value::cell(Value::invalid()); break;
      case Kind::Function: v = Value::function(0, {}); break;
      case Kind::Globals: IO::require(!r.names.empty(), "missing globals name"); v = Value::module(r.names[0]); break;
      case Kind::Class: IO::require(!r.names.empty(), "missing class name"); v = Value::class_object(r.names[0], {}); break;
      case Kind::Instance: case Kind::NativeInstance: v = Value::instance(Value::invalid()); break;
      case Kind::BoundMethod: shape(r, 0, 0, 2); v = Value::bound_method({}, {}); break;
      case Kind::StaticMethod: shape(r, 0, 0, 2); v = Value::static_method({}); break;
      case Kind::ClassMethod: shape(r, 0, 0, 2); v = Value::class_method({}); break;
      case Kind::Property: shape(r, 1, 0, 5); v = Value::property({}, {}, {}, {}); break;
      case Kind::Slot: shape(r, 1, 2, 1); IO::require(r.numbers[0] <= UINT32_MAX, "invalid slot index"); v = slot_descriptor(r.names[0], r.names[1], static_cast<uint32_t>(r.numbers[0])); break;
      case Kind::Module: case Kind::Symbol: {
        // Imported objects belong to the runtime, including on lookup failure.
        owned[i] = false;
        if (r.kind == Kind::Module) shape(r, 0, 1, 0);
        else IO::require(r.numbers.size() == 1 && r.refs.empty() && r.names.size() >= 2 && r.names.size() <= 3, "invalid native symbol path");
        if (r.kind == Kind::Symbol && r.names[0].empty()) {
          auto* symbol = runtime.find_native_symbol(r.names[1]);
          IO::require(symbol != nullptr && r.numbers[0] == static_cast<uint64_t>(ObjectKind::NativeFunction), "native runtime symbol is unavailable");
          v = *symbol;
          owned[i] = false;
          break;
        }
        std::string error;
        if (!runtime.import_module(r.names[0], v, error)) throw std::runtime_error(error);
        if (r.kind == Kind::Symbol) {
          Value symbol;
          if (!module_get_attr(v, r.names[1], symbol, error)) throw std::runtime_error(error);
          v = std::move(symbol);
          if (r.names.size() == 3) {
            if (!object_lookup_class_attr(v, r.names[2], symbol, error)) throw std::runtime_error(error);
            v = std::move(symbol);
          }
          IO::require(v.tag == ValueTag::Object && static_cast<uint64_t>(v.as.obj->kind) == r.numbers[0], "native symbol kind mismatch");
        }
        owned[i] = false;
        break;
      }
      case Kind::Expression: {
        shape(r, 0, 0, 0);
        std::string error;
        if (!decode_expression(r.payload, v, error)) throw std::runtime_error(error);
        break;
      }
      default: throw std::runtime_error("unsupported graph object kind");
    }
  }
  void fill(size_t i) {
    auto& r = records[i];
    auto& v = values[i];
    auto ref = [&](size_t n) { IO::require(n < r.refs.size(), "missing graph field"); return resolve(r.refs[n]); };
    switch (r.kind) {
      case Kind::List: for (auto x : r.refs) value_as_list(v)->items.push_back(resolve(x)); break;
      case Kind::Tuple: for (auto x : r.refs) value_as_tuple(v)->items.push_back(resolve(x)); break;
      case Kind::Dict: {
        IO::require(r.refs.size() % 2 == 0, "invalid dictionary graph");
        for (size_t j = 0; j < r.refs.size(); j += 2) value_as_dict(v)->entries.emplace_back(ref(j), ref(j + 1));
        break;
      }
      case Kind::Cell: value_as_cell(v)->value = ref(0); break;
      case Kind::Globals: {
        IO::require(r.names.size() == r.refs.size() + 1, "invalid globals graph");
        std::string error;
        for (size_t j = 0; j < r.refs.size(); ++j) if (!module_set_attr(v, r.names[j + 1], ref(j), error)) throw std::runtime_error(error);
        break;
      }
      case Kind::Function: {
        IO::require(r.numbers.size() == 6 && !r.names.empty(), "invalid function metadata");
        auto& n = r.numbers;
        for (size_t j = 2; j < 6; ++j) IO::require(n[j] <= max_fields, "invalid function field count");
        IO::require(n[1] < modules.size() && n[0] < modules[n[1]]->functions.size(), "invalid function IR reference");
        IO::require(r.refs.size() == 4 + n[2] + n[3] + n[4] + n[5] && r.names.size() >= 1 + n[5], "invalid function fields");
        auto* f = value_as_function(v);
        f->function_id = static_cast<uint32_t>(n[0]); f->module = modules[n[1]]; f->qualname = r.names[0];
        f->globals_module = ref(0); f->annotations = ref(1); f->doc = ref(2); f->attrs_dict = ref(3);
        size_t cursor = 4;
        for (size_t j = 0; j < n[2]; ++j) f->closure.push_back(ref(cursor++));
        for (size_t j = 0; j < n[3]; ++j) f->defaults.push_back(ref(cursor++));
        for (size_t j = 0; j < n[4]; ++j) f->positional_defaults.push_back(ref(cursor++));
        for (size_t j = 0; j < n[5]; ++j) f->kwdefaults.emplace_back(r.names[j + 1], ref(cursor++));
        f->type_params.assign(r.names.begin() + 1 + n[5], r.names.end());
        IO::require(f->closure.size() == f->module->functions[f->function_id].free_vars.size(), "function closure size mismatch");
        for (auto& cell : f->closure) IO::require(value_as_cell(cell) != nullptr, "function closure is not a cell");
        break;
      }
      case Kind::Class: {
        IO::require(r.numbers.size() == 4, "invalid class metadata");
        auto& n = r.numbers;
        for (size_t j = 0; j < 3; ++j) IO::require(n[j] <= max_fields, "invalid class field count");
        IO::require(r.names.size() >= 1 + n[1] + n[2] && r.refs.size() == 2 + n[0] + r.names.size() - 1 - n[1] - n[2], "invalid class fields");
        auto* c = value_as_class(v);
        c->base = ref(0); c->metaclass = ref(1); c->attrs.clear();
        for (size_t j = 0; j < n[0]; ++j) c->bases.push_back(ref(2 + j));
        c->instance_slot_names.assign(r.names.begin() + 1, r.names.begin() + 1 + n[1]);
        for (size_t j = 0; j < n[1]; ++j) c->instance_slot_indices.emplace(c->instance_slot_names[j], static_cast<uint32_t>(j));
        c->definition_attr_order.assign(r.names.begin() + 1 + n[1], r.names.begin() + 1 + n[1] + n[2]);
        for (size_t j = 1 + n[1] + n[2], k = 2 + n[0]; j < r.names.size(); ++j, ++k) c->attrs.emplace(r.names[j], ref(k));
        auto flags = n[3]; IO::require(flags <= 511, "invalid class flags");
        c->has_explicit_bases = flags & 1; c->has_descriptors = flags & 2; c->has_getattribute_hook = flags & 4;
        c->has_getattr_hook = flags & 8; c->has_setattr_hook = flags & 16; c->has_delattr_hook = flags & 32;
        c->restrict_instance_attrs = flags & 64; c->allow_instance_dict = flags & 128; c->allow_weakref = flags & 256;
        break;
      }
      case Kind::Instance: case Kind::NativeInstance: {
        const bool native = r.kind == Kind::NativeInstance;
        IO::require(r.numbers.size() == (native ? 2 : 1) && (!native || !r.names.empty()) && r.numbers[0] <= max_fields && r.refs.size() == 3 + r.numbers[0] + r.names.size(), "invalid instance graph");
        auto* instance = value_as_instance(v);
        instance->klass = ref(0); instance->mapping_storage = ref(1); instance->sequence_storage = ref(2);
        const auto slot_count = static_cast<uint32_t>(r.numbers[0]);
        if (slot_count > 8) instance->overflow_slots.resize(slot_count);
        instance->slot_count = slot_count;
        for (uint32_t j = 0; j < instance->slot_count; ++j) instance_slot_at(instance, j) = ref(3 + j);
        for (size_t j = 0; j < r.names.size() - (native ? 1 : 0); ++j) instance->attrs.emplace_back(r.names[j], ref(3 + instance->slot_count + j));
        break;
      }
      case Kind::BoundMethod: { auto* m = value_as_bound_method(v); m->self = ref(0); m->function = ref(1); break; }
      case Kind::StaticMethod: { auto* m = value_as_static_method(v); m->function = ref(0); m->attrs_dict = ref(1); break; }
      case Kind::ClassMethod: { auto* m = value_as_class_method(v); m->function = ref(0); m->attrs_dict = ref(1); break; }
      case Kind::Slot: value_as_slot_descriptor(v)->owner_class = ref(0); break;
      case Kind::Property: {
        auto* p = value_as_property(v);
        p->fget = ref(0); p->fset = ref(1); p->fdel = ref(2); p->doc = ref(3); p->name = ref(4);
        auto flags = r.numbers[0]; IO::require(flags <= 15, "invalid property flags");
        p->is_abstract = flags & 1; p->doc_from_getter = flags & 2; p->has_name = flags & 4; p->name_from_getter = flags & 8;
        break;
      }
      default: break;
    }
  }
  void validate() {
    std::unordered_map<ClassObject*, unsigned> marks;
    std::function<void(ClassObject*, unsigned)> visit = [&](ClassObject* klass, unsigned depth) {
      if (!klass || marks[klass] == 2) return;
      IO::require(depth < 256 && marks[klass] != 1, "cyclic or excessively deep class hierarchy");
      marks[klass] = 1;
      if (klass->base.tag != ValueTag::Invalid && klass->base.tag != ValueTag::None) {
        auto* parent = value_as_class(klass->base);
        IO::require(parent != nullptr, "class base is not a class");
        visit(parent, depth + 1);
      }
      for (auto& base : klass->bases) {
        auto* parent = value_as_class(base);
        IO::require(parent != nullptr, "class base is not a class");
        visit(parent, depth + 1);
      }
      marks[klass] = 2;
    };
    for (size_t i = 0; i < values.size(); ++i) {
      if (!owned[i]) continue;
      if (auto* klass = value_as_class(values[i])) visit(klass, 0);
      if (auto* obj = value_as_instance(values[i])) {
        auto* klass = value_as_class(obj->klass);
        IO::require(klass && obj->slot_count == klass->instance_slot_names.size(), "instance class layout mismatch");
      }
      if (auto* slot = value_as_slot_descriptor(values[i])) {
        auto* klass = value_as_class(slot->owner_class);
        IO::require(klass && slot->index < klass->instance_slot_names.size(), "invalid slot descriptor");
      }
    }
  }
};
}
}
namespace xlang3::serialize {
bool read_value_graph(Runtime& runtime, XLangStream& stream, Value& value, std::string& error) {
  try {
    XlangRuntimeExecutionGuard guard;
    while (runtime.collect_serialized_objects()) {}
    graph::Reader reader(runtime, stream);
    value = reader.run();
    return true;
  } catch (const std::exception& e) { error = e.what(); return false; }
}
}
