/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "value_graph_internal.h"
#include "xlang3/expression.h"
#include "runtime_lock.h"
#include <algorithm>

namespace xlang3::serialize::graph {
namespace {
struct Node {
  Value value;
  Kind kind{};
  std::vector<Value> refs;
  std::vector<std::string> names;
  std::vector<uint64_t> numbers;
  std::string owned_payload;
  std::string_view payload;
};
class Writer {
public:
  Writer(Runtime& runtime, XLangStream& stream) : runtime(runtime), io(stream) {}
  void run(const Value& root) {
    add(root);
    for (size_t i = 0; i < pending.size(); ++i) {
      auto id = pending[i];
      Node node;
      node.value = nodes[id].value;
      describe(node);
      for (const auto& ref : node.refs) add(ref);
      nodes[id] = std::move(node);
    }
    io.put_number<uint32_t>(magic);
    io.put_number<uint32_t>(version);
    io.count(modules.size());
    for (const auto& module : modules) {
      ir::EncodedModule encoded;
      std::string error;
      if (!ir::encode_module(*module, 0, encoded, error)) throw std::runtime_error(error);
      io.put_number<uint64_t>(ir::source_hash64(encoded.bytes.data(), encoded.bytes.size()));
      io.text(std::string_view(reinterpret_cast<const char*>(encoded.bytes.data()), encoded.bytes.size()));
    }
    io.count(nodes.size());
    for (const auto& node : nodes) {
      io.put_number<uint8_t>(static_cast<uint8_t>(node.kind));
      io.count(node.numbers.size());
      for (auto number : node.numbers) io.put_number<uint64_t>(number);
      io.count(node.names.size());
      for (const auto& name : node.names) io.text(name);
      io.text(node.owned_payload.empty() ? node.payload : std::string_view(node.owned_payload));
      io.count(node.refs.size());
      for (const auto& ref : node.refs) reference(ref);
    }
    reference(root);
  }
private:
  Runtime& runtime;
  IO io;
  std::vector<Node> nodes;
  std::vector<uint32_t> pending;
  std::unordered_map<Object*, uint32_t> ids;
  std::unordered_set<Object*> globals;
  std::unordered_map<Object*, std::unordered_set<std::string>> global_names;
  std::unordered_set<Object*> dynamic_globals;
  std::unordered_map<Object*, std::vector<std::string>> symbols;
  bool symbols_indexed = false;
  std::vector<Value> native_symbols;
  std::vector<std::shared_ptr<const ir::Module>> modules;
  std::unordered_map<const ir::Module*, uint32_t> module_ids;

  void add(const Value& value) {
    if (value.tag != ValueTag::Object || !value.as.obj || ids.count(value.as.obj)) return;
    IO::require(nodes.size() < max_nodes, "value graph has too many objects");
    auto id = static_cast<uint32_t>(nodes.size());
    ids.emplace(value.as.obj, id);
    Node node;
    node.value = value;
    nodes.push_back(std::move(node));
    pending.push_back(id);
  }
  void reference(const Value& value) {
    io.put_number<uint8_t>(static_cast<uint8_t>(value.tag));
    uint64_t bits = 0;
    switch (value.tag) {
      case ValueTag::Invalid: case ValueTag::None: break;
      case ValueTag::Bool: bits = value.as.b ? 1 : 0; break;
      case ValueTag::Int64: std::memcpy(&bits, &value.as.i64, 8); break;
      case ValueTag::Double: std::memcpy(&bits, &value.as.f64, 8); break;
      case ValueTag::Object:
        IO::require(value.as.obj != nullptr, "null graph object");
        bits = ids.at(value.as.obj); break;
      default: throw std::runtime_error("unsupported value tag in graph");
    }
    io.put_number<uint64_t>(bits);
  }
  void index_symbols() {
    Value builtins;
    std::string error;
    IO::require(runtime.import_module("builtins", builtins, error), "cannot resolve builtins");
    Value machinery;
    IO::require(runtime.import_module("importlib.machinery", machinery, error), "cannot resolve loader types");
    auto* registry = value_as_dict(runtime.module_registry_dict());
    if (!registry) return;
    for (const auto& entry : registry->entries) {
      auto* module = value_as_module(entry.second);
      if (!module) continue;
      bool script = false;
      for (const auto& value : module->slots) if (value_as_function(value)) script = true;
      Value file;
      if (module_get_attr(entry.second, "__file__", file, error)) {
        if (auto* text = value_as_string(file)) {
          auto path = string_object_view(*text);
          script = script || (path.size() >= 3 && path.substr(path.size() - 3) == ".py");
        }
      }
      if (script) continue;
      for (const auto& item : module->name_to_slot) {
        if (item.second >= module->slots.size()) continue;
        const auto& value = module->slots[item.second];
        if (value_as_native_function(value) || value_as_class(value)) {
          symbols.emplace(value.as.obj, std::vector<std::string>{module->name, item.first});
          if (value_as_native_function(value)) native_symbols.push_back(value);
          if (auto* klass = value_as_class(value)) {
            for (const auto& attr : klass->attrs) {
              if (value_as_native_function(attr.second)) {
                symbols.emplace(attr.second.as.obj, std::vector<std::string>{module->name, item.first, attr.first});
                native_symbols.push_back(attr.second);
              }
            }
          }
        }
      }
    }
  }
  void capture_globals(const FunctionObject& function) {
    auto* owner = function.globals_module.as.obj;
    if (!value_as_module(function.globals_module)) return;
    bool changed = globals.insert(owner).second;
    auto& names = global_names[owner];
    std::unordered_set<uint32_t> visited;
    std::vector<uint32_t> functions{function.function_id};
    while (!functions.empty()) {
      const auto id = functions.back(); functions.pop_back();
      if (!visited.insert(id).second) continue;
      IO::require(id < function.module->functions.size(), "invalid nested function ID");
      const auto& code = function.module->functions[id];
      for (const auto& instruction : code.code) {
        const std::string* name = nullptr;
        if (instruction.op == ir::Op::LoadGlobal) {
          IO::require(instruction.a < code.names.size(), "invalid global name");
          name = &code.names[instruction.a];
        } else if (instruction.op == ir::Op::LoadModuleSlot || instruction.op == ir::Op::CallModuleMethod) {
          IO::require(instruction.a < function.module->global_slots.size(), "invalid global slot");
          name = &function.module->global_slots[instruction.a];
        } else if (instruction.op == ir::Op::MakeFunction) functions.push_back(instruction.a);
        if (name) {
          changed = names.insert(*name).second || changed;
          if (*name == "globals" || *name == "eval" || *name == "exec")
            changed = dynamic_globals.insert(owner).second || changed;
        }
      }
    }
    if (changed) {
      auto old = ids.find(owner);
      if (old != ids.end()) pending.push_back(old->second);
    }
  }
  void describe(Node& node) {
    const auto& value = node.value;
    if (auto* native = value_as_native_function(value)) {
      const auto* registered = runtime.find_native_symbol(native->name);
      auto* other = registered ? value_as_native_function(*registered) : nullptr;
      if (other && !native->user_data && native->callback == other->callback) {
        node.kind = Kind::Symbol;
        node.names = {"", native->name};
        node.numbers = {static_cast<uint64_t>(ObjectKind::NativeFunction)};
        return;
      }
    }
    if (!symbols_indexed && (value_as_native_function(value) || value_as_class(value))) {
      index_symbols();
      symbols_indexed = true;
    }
    auto symbol = symbols.find(value.as.obj);
    if (symbol == symbols.end()) {
      if (auto* native = value_as_native_function(value)) {
        for (const auto& candidate : native_symbols) {
          auto* other = value_as_native_function(candidate);
          if (native->name == other->name && native->callback == other->callback && native->user_data == other->user_data) {
            symbol = symbols.find(candidate.as.obj);
            break;
          }
        }
      }
    }
    if (symbol != symbols.end()) {
      node.kind = Kind::Symbol;
      node.names = symbol->second;
      node.numbers = {static_cast<uint64_t>(value.as.obj->kind)};
      return;
    }
    if (auto* text = value_as_string(value)) {
      node.kind = Kind::String; node.payload = string_object_view(*text);
    } else if (auto* bytes = value_as_bytes(value)) {
      node.kind = Kind::Bytes; node.payload = bytes_object_view(*bytes);
    } else if (auto* bytes = value_as_bytearray(value)) {
      node.kind = Kind::ByteArray; node.payload = std::string_view(bytes->value.data(), bytes->value.size());
    } else if (auto* view = value_as_memoryview(value)) {
      IO::require(!view->released, "cannot serialize a released memoryview");
      const auto storage = memoryview_object_view(*view);
      IO::require(storage.data() != nullptr, "invalid memoryview bounds");
      node.kind = Kind::Bytes; node.payload = storage;
    } else if (auto* list = value_as_list(value)) {
      node.kind = Kind::List; node.refs = list->items;
    } else if (auto* tuple = value_as_tuple(value)) {
      node.kind = Kind::Tuple; node.refs.assign(tuple->items.begin(), tuple->items.end());
    } else if (auto* dict = value_as_dict(value)) {
      node.kind = Kind::Dict;
      for (const auto& entry : dict->entries) { node.refs.push_back(entry.first); node.refs.push_back(entry.second); }
    } else if (auto* cell = value_as_cell(value)) {
      node.kind = Kind::Cell; node.refs = {cell->value};
    } else if (auto* function = value_as_function(value)) {
      IO::require(function->module && function->function_id < function->module->functions.size(), "function has no executable IR");
      node.kind = Kind::Function;
      auto [it, added] = module_ids.emplace(function->module.get(), static_cast<uint32_t>(modules.size()));
      if (added) modules.push_back(function->module);
      node.numbers = {function->function_id, it->second, function->closure.size(), function->defaults.size(), function->positional_defaults.size(), function->kwdefaults.size()};
      node.names = {function->qualname};
      for (const auto& item : function->kwdefaults) node.names.push_back(item.first);
      node.names.insert(node.names.end(), function->type_params.begin(), function->type_params.end());
      node.refs = {function->globals_module, function->annotations, function->doc, function->attrs_dict};
      node.refs.insert(node.refs.end(), function->closure.begin(), function->closure.end());
      node.refs.insert(node.refs.end(), function->defaults.begin(), function->defaults.end());
      node.refs.insert(node.refs.end(), function->positional_defaults.begin(), function->positional_defaults.end());
      for (const auto& item : function->kwdefaults) node.refs.push_back(item.second);
      capture_globals(*function);
    } else if (auto* module = value_as_module(value)) {
      node.names = {module->name};
      if (!globals.count(value.as.obj)) { node.kind = Kind::Module; return; }
      node.kind = Kind::Globals;
      std::vector<std::pair<uint32_t, std::string>> ordered;
      for (const auto& entry : module->name_to_slot) ordered.emplace_back(entry.second, entry.first);
      std::sort(ordered.begin(), ordered.end());
      for (const auto& entry : ordered) {
        IO::require(entry.first < module->slots.size(), "invalid globals slot");
        node.names.push_back(entry.second);
        // Keep slot numbering stable without copying unrelated module state.
        node.refs.push_back(dynamic_globals.count(value.as.obj) || global_names[value.as.obj].count(entry.second)
            ? module->slots[entry.first] : Value{});
      }
    } else if (auto* klass = value_as_class(value)) {
      node.kind = Kind::Class;
      node.names = {klass->name};
      node.names.insert(node.names.end(), klass->instance_slot_names.begin(), klass->instance_slot_names.end());
      node.names.insert(node.names.end(), klass->definition_attr_order.begin(), klass->definition_attr_order.end());
      uint64_t flags = static_cast<uint64_t>(klass->has_explicit_bases) | (klass->has_descriptors << 1) | (klass->has_getattribute_hook << 2) |
          (klass->has_getattr_hook << 3) | (klass->has_setattr_hook << 4) | (klass->has_delattr_hook << 5) |
          (klass->restrict_instance_attrs << 6) | (klass->allow_instance_dict << 7) | (klass->allow_weakref << 8);
      node.numbers = {klass->bases.size(), klass->instance_slot_names.size(), klass->definition_attr_order.size(), flags};
      node.refs = {klass->base, klass->metaclass};
      node.refs.insert(node.refs.end(), klass->bases.begin(), klass->bases.end());
      for (const auto& entry : klass->attrs) { node.names.push_back(entry.first); node.refs.push_back(entry.second); }
    } else if (auto* instance = value_as_instance(value)) {
      node.kind = Kind::Instance;
      node.numbers = {instance->slot_count};
      node.refs = {instance->klass, instance->mapping_storage, instance->sequence_storage};
      for (uint32_t i = 0; i < instance->slot_count; ++i) node.refs.push_back(instance_slot_at(instance, i));
      for (const auto& entry : instance->attrs) { node.names.push_back(entry.first); node.refs.push_back(entry.second); }
      if (instance->native_data || instance->native_get_attr || instance->native_set_attr || instance->native_delete_attr) {
        auto* codec = runtime.find_native_codec(instance->native_type, true);
        IO::require(codec != nullptr, "native instance requires a registered serializer");
        Value state;
        std::string error;
        if (!codec->encode(value, state, error)) throw std::runtime_error(error);
        node.kind = Kind::NativeInstance;
        node.names.push_back(codec->type_id);
        node.numbers.push_back(codec->version);
        node.refs.push_back(std::move(state));
      }
    } else if (auto* method = value_as_bound_method(value)) {
      node.kind = Kind::BoundMethod; node.refs = {method->self, method->function};
    } else if (auto* method = value_as_static_method(value)) {
      node.kind = Kind::StaticMethod; node.refs = {method->function, method->attrs_dict};
    } else if (auto* method = value_as_class_method(value)) {
      node.kind = Kind::ClassMethod; node.refs = {method->function, method->attrs_dict};
    } else if (auto* property = value_as_property(value)) {
      node.kind = Kind::Property;
      node.refs = {property->fget, property->fset, property->fdel, property->doc, property->name};
      node.numbers = {static_cast<uint64_t>(property->is_abstract) | (property->doc_from_getter << 1) | (property->has_name << 2) | (property->name_from_getter << 3)};
    } else if (auto* slot = value_as_slot_descriptor(value)) {
      node.kind = Kind::Slot; node.refs = {slot->owner_class};
      node.names = {slot->owner_name, slot->name}; node.numbers = {slot->index};
    } else if (value.as.obj->kind == ObjectKind::Expression) {
      node.kind = Kind::Expression;
      std::string error;
      if (!encode_expression(value, node.owned_payload, error)) throw std::runtime_error(error);
    } else {
      auto* native = value_as_native_function(value);
      throw std::runtime_error("unsupported object in value graph: kind=" + std::to_string(static_cast<uint32_t>(value.as.obj->kind)) +
          (native ? ", native=" + native->name : std::string()));
    }
  }
};
}
}

namespace xlang3::serialize {
bool write_value_graph(Runtime& runtime, XLangStream& stream, const Value& value, std::string& error) {
  try { XlangRuntimeExecutionGuard guard; graph::Writer(runtime, stream).run(value); return true; }
  catch (const std::exception& e) { error = e.what(); return false; }
}
}
