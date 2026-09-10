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
#include "xlang3/builtins.h"
#include "xlang3/builtin_methods.h"

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <climits>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kPicklerNativeType = "_pickle.Pickler";
constexpr const char* kUnpicklerNativeType = "_pickle.Unpickler";
constexpr unsigned char kPickleHighestProtocol = 5;

struct PicklerState {
  Value file;
  int protocol = kPickleHighestProtocol;
};

struct UnpicklerState {
  Value file;
};

void raise_pickle_module_error(Runtime& runtime, const char* class_name, const std::string& message) {
  Value pickle_module;
  Value exception_class;
  std::string ignored;
  if (mapping_get_item(
          runtime.module_registry_dict(), Value::string("_pickle"), pickle_module, ignored) &&
      module_get_attr(pickle_module, class_name, exception_class, ignored) &&
      value_as_class(exception_class) != nullptr) {
    runtime.set_pending_exception(runtime.make_exception_from_class(std::move(exception_class), message));
    return;
  }
  runtime.raise_class_error(class_name, message);
}

void pickler_cleanup(void* data) {
  delete static_cast<PicklerState*>(data);
}

void unpickler_cleanup(void* data) {
  delete static_cast<UnpicklerState*>(data);
}

bool picklebuffer_init(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool marshal_function(Runtime& runtime, const char* name, Value& out, std::string& error) {
  Value marshal_module;
  if (!mapping_get_item(runtime.module_registry_dict(), Value::string("marshal"), marshal_module, error)) {
    error = "marshal module is not registered";
    return false;
  }
  return module_get_attr(marshal_module, name, out, error);
}

bool get_bytes_view(const Value& value, std::string_view& out, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_view(*bytes);
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    out = memoryview_object_view(*view);
    return true;
  }
  error = "pickle.loads() expected bytes-like object";
  return false;
}

void append_u32(std::string& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
  }
}

void append_u64_be(std::string& out, uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
  }
}

void append_pickle_string(std::string& out, std::string_view text) {
  if (text.size() <= 0xffu) {
    out.push_back(static_cast<char>(0x8c)); // SHORT_BINUNICODE
    out.push_back(static_cast<char>(text.size()));
  } else {
    out.push_back('X'); // BINUNICODE
    append_u32(out, static_cast<uint32_t>(text.size()));
  }
  out.append(text.data(), text.size());
}

void append_pickle_bytes(std::string& out, std::string_view text) {
  if (text.size() <= 0xffu) {
    out.push_back('C'); // SHORT_BINBYTES
    out.push_back(static_cast<char>(text.size()));
  } else {
    out.push_back('B'); // BINBYTES
    append_u32(out, static_cast<uint32_t>(text.size()));
  }
  out.append(text.data(), text.size());
}

bool pickle_write_global(Runtime& runtime, const Value& callable, std::string& out, std::string& error) {
  Value module_value;
  Value name_value;
  if (!attribute_get(callable, "__module__", module_value, error) ||
      !attribute_get(callable, "__qualname__", name_value, error)) {
    error = "pickle reducer is not a global callable: " + value_to_repr(callable);
    return false;
  }
  auto* module = value_as_string(module_value);
  auto* name = value_as_string(name_value);
  if (module == nullptr || name == nullptr) {
    error = "pickle reducer global name must be a string";
    return false;
  }
  std::string module_name = string_object_to_string(*module);
  std::string qualified_name = string_object_to_string(*name);
  if (module_name.find('\n') != std::string::npos || qualified_name.find('\n') != std::string::npos) {
    error = "pickle reducer global name contains a newline";
    return false;
  }
  Value resolved;
  const ClassObject* callable_class = value_as_class(callable);
  if (callable_class != nullptr && value_as_module(callable_class->globals_module) != nullptr) {
    value_assign_fast(resolved, callable_class->globals_module);
  } else if (!runtime.import_module(module_name, resolved, error)) {
    error = "pickle global module '" + module_name + "' cannot be imported";
    return false;
  }
  size_t start = 0;
  while (start <= qualified_name.size()) {
    const size_t dot = qualified_name.find('.', start);
    const std::string component = qualified_name.substr(
        start, dot == std::string::npos ? std::string::npos : dot - start);
    Value next;
    if (!attribute_get(resolved, component, next, error)) {
      error = "pickle global '" + module_name + "." + qualified_name + "' cannot be resolved";
      return false;
    }
    resolved = std::move(next);
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  if (!value_is(resolved, callable)) {
    error = "pickle global '" + module_name + "." + qualified_name + "' is not the same object";
    return false;
  }
  out.push_back('c'); // GLOBAL
  out.append(module_name);
  out.push_back('\n');
  out.append(qualified_name);
  out.push_back('\n');
  return true;
}

bool pickle_write_value(
    Runtime& runtime,
    const Value& value,
    int protocol,
    std::string& out,
    std::string& error,
    const Value* dispatch_table = nullptr) {
  switch (value.tag) {
    case ValueTag::None:
      out.push_back('N');
      return true;
    case ValueTag::Bool:
      out.push_back(static_cast<char>(value.as.b ? 0x88 : 0x89)); // NEWTRUE/NEWFALSE
      return true;
    case ValueTag::Int64:
      if (value.as.i64 >= 0 && value.as.i64 <= 0xff) {
        out.push_back('K'); // BININT1
        out.push_back(static_cast<char>(value.as.i64));
      } else if (value.as.i64 >= 0 && value.as.i64 <= 0xffff) {
        out.push_back('M'); // BININT2
        out.push_back(static_cast<char>(value.as.i64 & 0xff));
        out.push_back(static_cast<char>((value.as.i64 >> 8) & 0xff));
      } else if (value.as.i64 >= INT32_MIN && value.as.i64 <= INT32_MAX) {
        out.push_back('J'); // BININT
        append_u32(out, static_cast<uint32_t>(value.as.i64));
      } else {
        out.push_back(static_cast<char>(0x8a)); // LONG1
        uint64_t raw = static_cast<uint64_t>(value.as.i64);
        uint8_t bytes[8];
        size_t count = 8;
        for (size_t i = 0; i < 8; ++i) {
          bytes[i] = static_cast<uint8_t>((raw >> (i * 8)) & 0xffu);
        }
        while (count > 1 &&
               ((bytes[count - 1] == 0x00 && (bytes[count - 2] & 0x80u) == 0) ||
                (bytes[count - 1] == 0xff && (bytes[count - 2] & 0x80u) != 0))) {
          --count;
        }
        out.push_back(static_cast<char>(count));
        out.append(reinterpret_cast<const char*>(bytes), count);
      }
      return true;
    case ValueTag::Double: {
      out.push_back('G'); // BINFLOAT
      uint64_t bits = 0;
      std::memcpy(&bits, &value.as.f64, sizeof(bits));
      append_u64_be(out, bits);
      return true;
    }
    case ValueTag::Invalid:
      error = "cannot pickle invalid value";
      return false;
    case ValueTag::Object:
      break;
  }

  if (auto* text = value_as_string(value)) {
    append_pickle_string(out, string_object_view(*text));
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    append_pickle_bytes(out, bytes_object_view(*bytes));
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    append_pickle_bytes(out, bytearray->value);
    return true;
  }
  if (auto* list = value_as_list(value)) {
    out.push_back(']'); // EMPTY_LIST
    if (!list->items.empty()) {
      out.push_back('('); // MARK
      for (const auto& item : list->items) {
        if (!pickle_write_value(runtime, item, protocol, out, error, dispatch_table)) {
          return false;
        }
      }
      out.push_back('e'); // APPENDS
    }
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    if (tuple->items.empty()) {
      out.push_back(')'); // EMPTY_TUPLE
      return true;
    }
    out.push_back('('); // MARK
    for (const auto& item : tuple->items) {
      if (!pickle_write_value(runtime, item, protocol, out, error, dispatch_table)) {
        return false;
      }
    }
    out.push_back('t'); // TUPLE
    return true;
  }
  if (auto* dict = value_as_dict(value)) {
    out.push_back('}'); // EMPTY_DICT
    if (!dict->entries.empty()) {
      out.push_back('('); // MARK
      for (const auto& entry : dict->entries) {
        if (!pickle_write_value(runtime, entry.first, protocol, out, error, dispatch_table) ||
            !pickle_write_value(runtime, entry.second, protocol, out, error, dispatch_table)) {
          return false;
        }
      }
      out.push_back('u'); // SETITEMS
    }
    return true;
  }
  if (auto* set = value_as_set(value)) {
    out.push_back(static_cast<char>(0x8f)); // EMPTY_SET
    if (!set->items.empty()) {
      out.push_back('('); // MARK
      for (const auto& item : set->items) {
        if (!pickle_write_value(runtime, item, protocol, out, error, dispatch_table)) {
          return false;
        }
      }
      out.push_back(static_cast<char>(0x90)); // ADDITEMS
    }
    return true;
  }

  if (value_as_class(value) != nullptr || value_as_function(value) != nullptr ||
      value_as_native_function(value) != nullptr) {
    return pickle_write_global(runtime, value, out, error);
  }

  if (auto* bound = value_as_bound_method(value)) {
    const Value* getattr_function = runtime.find_builtin("getattr");
    Value method_name;
    if (getattr_function == nullptr ||
        !attribute_get(bound->function, "__name__", method_name, error) ||
        value_as_string(method_name) == nullptr) {
      error = "cannot pickle bound method";
      return false;
    }
    if (!pickle_write_global(runtime, *getattr_function, out, error) ||
        !pickle_write_value(
            runtime, Value::tuple({bound->self, std::move(method_name)}), protocol, out, error, dispatch_table)) {
      return false;
    }
    out.push_back('R'); // REDUCE
    return true;
  }

  Value reduced;
  bool used_dispatch = false;
  if (dispatch_table != nullptr && mapping_is_mapping(*dispatch_table)) {
    Value object_type;
    Value reducer;
    std::string dispatch_error;
    if (runtime_type_of_value(runtime, value, object_type) &&
        mapping_get_item(*dispatch_table, object_type, reducer, dispatch_error)) {
      if (!runtime_call_callable(runtime, reducer, &value, 1, reduced, error)) {
        return false;
      }
      used_dispatch = true;
    }
  }
  if (!used_dispatch) {
    Value reduce_method;
    std::string reduce_error;
    if (attribute_get(value, "__reduce_ex__", reduce_method, reduce_error)) {
      Value protocol_value = Value::int64(protocol);
      if (!runtime_call_callable(runtime, reduce_method, &protocol_value, 1, reduced, error)) {
        return false;
      }
    } else {
      reduce_error.clear();
      if (!attribute_get(value, "__reduce__", reduce_method, reduce_error)) {
        Value getstate;
        std::string getstate_error;
        if (attribute_get(value, "__getstate__", getstate, getstate_error)) {
          Value ignored_state;
          if (!runtime_call_callable(runtime, getstate, nullptr, 0, ignored_state, error)) {
            return false;
          }
        }
        error = "cannot pickle this object yet";
        return false;
      }
      if (!runtime_call_callable(runtime, reduce_method, nullptr, 0, reduced, error)) {
        return false;
      }
    }
  }
  auto* reduced_tuple = value_as_tuple(reduced);
  if (reduced_tuple == nullptr || reduced_tuple->items.size() < 2 || reduced_tuple->items.size() > 5 ||
      value_as_tuple(reduced_tuple->items[1]) == nullptr) {
    error = "__reduce__ must return a tuple with two through five items";
    return false;
  }
  if (!pickle_write_global(runtime, reduced_tuple->items[0], out, error)) {
    error += " while reducing " + value_to_repr(value);
    return false;
  }
  if (!pickle_write_value(runtime, reduced_tuple->items[1], protocol, out, error, dispatch_table)) {
    return false;
  }
  out.push_back('R'); // REDUCE

  if (reduced_tuple->items.size() >= 4 && reduced_tuple->items[3].tag != ValueTag::None) {
    std::vector<Value> list_items;
    if (!runtime_collect_iterable(runtime, reduced_tuple->items[3], list_items, error)) {
      return false;
    }
    if (!list_items.empty()) {
      out.push_back('('); // MARK
      for (const auto& item : list_items) {
        if (!pickle_write_value(runtime, item, protocol, out, error, dispatch_table)) {
          return false;
        }
      }
      out.push_back('e'); // APPENDS
    }
  }

  if (reduced_tuple->items.size() >= 5 && reduced_tuple->items[4].tag != ValueTag::None) {
    std::vector<Value> dict_items;
    if (!runtime_collect_iterable(runtime, reduced_tuple->items[4], dict_items, error)) {
      return false;
    }
    if (!dict_items.empty()) {
      out.push_back('('); // MARK
      for (const auto& item : dict_items) {
        const auto* pair = value_as_tuple(item);
        if (pair == nullptr || pair->items.size() != 2) {
          error = "dict items iterator must return 2-tuples";
          return false;
        }
        if (!pickle_write_value(runtime, pair->items[0], protocol, out, error, dispatch_table) ||
            !pickle_write_value(runtime, pair->items[1], protocol, out, error, dispatch_table)) {
          return false;
        }
      }
      out.push_back('u'); // SETITEMS
    }
  }

  if (reduced_tuple->items.size() >= 3 && reduced_tuple->items[2].tag != ValueTag::None) {
    if (!pickle_write_value(runtime, reduced_tuple->items[2], protocol, out, error, dispatch_table)) {
      return false;
    }
    out.push_back('b'); // BUILD
  }
  return true;
}

bool pickle_apply_attribute_state(Value& instance, const Value& state, std::string& error) {
  const auto* mapping = value_as_dict(state);
  if (mapping == nullptr) {
    error = "pickle object state is not a dictionary";
    return false;
  }
  for (const auto& entry : mapping->entries) {
    const auto* name = value_as_string(entry.first);
    if (name == nullptr) {
      error = "pickle object state contains a non-string attribute name";
      return false;
    }
    if (!attribute_set(instance, string_object_to_string(*name), entry.second, error)) {
      return false;
    }
  }
  return true;
}

bool pickle_apply_state(Runtime& runtime, Value& instance, const Value& state, std::string& error) {
  Value setstate;
  std::string attr_error;
  if (attribute_get(instance, "__setstate__", setstate, attr_error)) {
    Value ignored;
    return runtime_call_callable(runtime, setstate, &state, 1, ignored, error);
  }

  if (const auto* pair = value_as_tuple(state); pair != nullptr && pair->items.size() == 2) {
    if (pair->items[0].tag != ValueTag::None &&
        !pickle_apply_attribute_state(instance, pair->items[0], error)) {
      return false;
    }
    if (pair->items[1].tag != ValueTag::None &&
        !pickle_apply_attribute_state(instance, pair->items[1], error)) {
      return false;
    }
    return true;
  }
  return pickle_apply_attribute_state(instance, state, error);
}

struct PickleReader {
  std::string_view data;
  size_t pos = 0;
  std::vector<Value> stack;
  std::vector<size_t> marks;

  bool read_byte(unsigned char& out) {
    if (pos >= data.size()) {
      return false;
    }
    out = static_cast<unsigned char>(data[pos++]);
    return true;
  }

  bool read_u32(uint32_t& out) {
    if (data.size() - pos < 4) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 4; ++i) {
      out |= static_cast<uint32_t>(static_cast<unsigned char>(data[pos++])) << (i * 8);
    }
    return true;
  }

  bool read_u64_be(uint64_t& out) {
    if (data.size() - pos < 8) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out = (out << 8u) | static_cast<unsigned char>(data[pos++]);
    }
    return true;
  }

  bool read_u64(uint64_t& out) {
    if (data.size() - pos < 8) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<uint64_t>(static_cast<unsigned char>(data[pos++])) << (i * 8);
    }
    return true;
  }

  bool read_bytes(size_t size, std::string_view& out) {
    if (data.size() - pos < size) {
      return false;
    }
    out = data.substr(pos, size);
    pos += size;
    return true;
  }

  bool read_line(std::string_view& out) {
    size_t end = data.find('\n', pos);
    if (end == std::string_view::npos) {
      return false;
    }
    out = data.substr(pos, end - pos);
    pos = end + 1;
    return true;
  }
};

enum class PickleScanResult {
  Incomplete,
  Complete,
  Invalid,
};

PickleScanResult scan_pickle_record(std::string_view data, std::string& error) {
  size_t pos = 0;
  auto skip = [&](size_t count) -> bool {
    if (count > data.size() - pos) {
      return false;
    }
    pos += count;
    return true;
  };
  auto read_u32_size = [&](size_t& size) -> bool {
    if (data.size() - pos < 4) {
      return false;
    }
    size = 0;
    for (int shift = 0; shift < 32; shift += 8) {
      size |= static_cast<size_t>(static_cast<unsigned char>(data[pos++])) << shift;
    }
    return true;
  };

  while (pos < data.size()) {
    const unsigned char opcode = static_cast<unsigned char>(data[pos++]);
    switch (opcode) {
      case '.':
        return PickleScanResult::Complete;
      case 0x80: // PROTO
      case 'K':  // BININT1
        if (!skip(1)) return PickleScanResult::Incomplete;
        break;
      case 'M': // BININT2
        if (!skip(2)) return PickleScanResult::Incomplete;
        break;
      case 'J': // BININT
        if (!skip(4)) return PickleScanResult::Incomplete;
        break;
      case 'G': // BINFLOAT
      case 0x95: // FRAME
        if (!skip(8)) return PickleScanResult::Incomplete;
        break;
      case 0x8a: { // LONG1
        if (pos == data.size()) return PickleScanResult::Incomplete;
        const size_t size = static_cast<unsigned char>(data[pos++]);
        if (!skip(size)) return PickleScanResult::Incomplete;
        break;
      }
      case 0x8c: // SHORT_BINUNICODE
      case 'C': { // SHORT_BINBYTES
        if (pos == data.size()) return PickleScanResult::Incomplete;
        const size_t size = static_cast<unsigned char>(data[pos++]);
        if (!skip(size)) return PickleScanResult::Incomplete;
        break;
      }
      case 'X': // BINUNICODE
      case 'B': { // BINBYTES
        size_t size = 0;
        if (!read_u32_size(size) || !skip(size)) return PickleScanResult::Incomplete;
        break;
      }
      case 'c': { // GLOBAL
        for (int line = 0; line < 2; ++line) {
          const size_t newline = data.find('\n', pos);
          if (newline == std::string_view::npos) return PickleScanResult::Incomplete;
          pos = newline + 1;
        }
        break;
      }
      case 'N': // NONE
      case 0x88: // NEWTRUE
      case 0x89: // NEWFALSE
      case 0x94: // MEMOIZE
      case 0x93: // STACK_GLOBAL
      case 'R': // REDUCE
      case 'b': // BUILD
      case ']': // EMPTY_LIST
      case '}': // EMPTY_DICT
      case ')': // EMPTY_TUPLE
      case 0x8f: // EMPTY_SET
      case '(': // MARK
      case 'e': // APPENDS
      case 't': // TUPLE
      case 'u': // SETITEMS
      case 0x90: // ADDITEMS
        break;
      default:
        error = "unsupported pickle opcode in stream";
        return PickleScanResult::Invalid;
    }
  }
  return PickleScanResult::Incomplete;
}

bool pickle_read_value(Runtime& runtime, std::string_view payload, Value& out, std::string& error) {
  PickleReader reader{payload};
  while (reader.pos < reader.data.size()) {
    unsigned char opcode = 0;
    if (!reader.read_byte(opcode)) {
      error = "truncated pickle data";
      return false;
    }
    switch (opcode) {
      case 0x80: { // PROTO
        unsigned char protocol = 0;
        if (!reader.read_byte(protocol) || protocol > kPickleHighestProtocol) {
          error = "unsupported pickle protocol";
          return false;
        }
        break;
      }
      case 0x94: // MEMOIZE
        break;
      case 0x95: { // FRAME
        uint64_t frame_size = 0;
        if (!reader.read_u64(frame_size)) {
          error = "truncated pickle frame";
          return false;
        }
        break;
      }
      case 'N':
        reader.stack.push_back(Value::none());
        break;
      case 0x88:
        reader.stack.push_back(Value::boolean(true));
        break;
      case 0x89:
        reader.stack.push_back(Value::boolean(false));
        break;
      case 'K': {
        unsigned char value = 0;
        if (!reader.read_byte(value)) {
          error = "truncated pickle int";
          return false;
        }
        reader.stack.push_back(Value::int64(value));
        break;
      }
      case 'M': {
        unsigned char lo = 0;
        unsigned char hi = 0;
        if (!reader.read_byte(lo) || !reader.read_byte(hi)) {
          error = "truncated pickle int";
          return false;
        }
        reader.stack.push_back(Value::int64(static_cast<int64_t>(lo | (hi << 8u))));
        break;
      }
      case 'J': {
        uint32_t raw = 0;
        if (!reader.read_u32(raw)) {
          error = "truncated pickle int";
          return false;
        }
        reader.stack.push_back(Value::int64(static_cast<int32_t>(raw)));
        break;
      }
      case 0x8a: { // LONG1
        unsigned char size = 0;
        if (!reader.read_byte(size) || size == 0 || reader.data.size() - reader.pos < size) {
          error = "bad pickle long";
          return false;
        }
        uint64_t raw = 0;
        const bool negative = (static_cast<unsigned char>(reader.data[reader.pos + size - 1]) & 0x80u) != 0;
        for (size_t i = 0; i < size && i < 8; ++i) {
          raw |= static_cast<uint64_t>(static_cast<unsigned char>(reader.data[reader.pos + i])) << (i * 8);
        }
        reader.pos += size;
        if (negative && size < 8) {
          raw |= (~uint64_t{0}) << (size * 8);
        }
        reader.stack.push_back(Value::int64(static_cast<int64_t>(raw)));
        break;
      }
      case 'G': {
        uint64_t bits = 0;
        if (!reader.read_u64_be(bits)) {
          error = "truncated pickle float";
          return false;
        }
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        reader.stack.push_back(Value::number(value));
        break;
      }
      case 0x8c:
      case 'C': {
        unsigned char size = 0;
        std::string_view bytes;
        if (!reader.read_byte(size) || !reader.read_bytes(size, bytes)) {
          error = "truncated pickle bytes";
          return false;
        }
        reader.stack.push_back(opcode == 'C' ? Value::bytes(std::string(bytes)) : Value::string(std::string(bytes)));
        break;
      }
      case 'X':
      case 'B': {
        uint32_t size = 0;
        std::string_view bytes;
        if (!reader.read_u32(size) || !reader.read_bytes(size, bytes)) {
          error = "truncated pickle bytes";
          return false;
        }
        reader.stack.push_back(opcode == 'B' ? Value::bytes(std::string(bytes)) : Value::string(std::string(bytes)));
        break;
      }
      case 'c': { // GLOBAL
        std::string_view module_name;
        std::string_view qualified_name;
        if (!reader.read_line(module_name) || !reader.read_line(qualified_name)) {
          error = "truncated pickle global";
          return false;
        }
        Value global;
        if (!runtime.import_module(std::string(module_name), global, error)) {
          return false;
        }
        size_t start = 0;
        while (start <= qualified_name.size()) {
          size_t dot = qualified_name.find('.', start);
          std::string component(qualified_name.substr(
              start,
              dot == std::string_view::npos ? qualified_name.size() - start : dot - start));
          Value next;
          if (!attribute_get(global, component, next, error)) {
            return false;
          }
          global = next;
          if (dot == std::string_view::npos) {
            break;
          }
          start = dot + 1;
        }
        reader.stack.push_back(global);
        break;
      }
      case 0x93: { // STACK_GLOBAL
        if (reader.stack.size() < 2) {
          error = "pickle STACK_GLOBAL needs module and name";
          return false;
        }
        Value qualified_name_value = reader.stack.back();
        reader.stack.pop_back();
        Value module_name_value = reader.stack.back();
        reader.stack.pop_back();
        auto* module_name_string = value_as_string(module_name_value);
        auto* qualified_name_string = value_as_string(qualified_name_value);
        if (module_name_string == nullptr || qualified_name_string == nullptr) {
          error = "STACK_GLOBAL requires str";
          return false;
        }
        const std::string module_name = string_object_to_string(*module_name_string);
        const std::string qualified_name = string_object_to_string(*qualified_name_string);
        Value global;
        if (!runtime.import_module(module_name, global, error)) {
          return false;
        }
        size_t start = 0;
        while (start <= qualified_name.size()) {
          const size_t dot = qualified_name.find('.', start);
          const std::string component = qualified_name.substr(
              start,
              dot == std::string::npos ? qualified_name.size() - start : dot - start);
          Value next;
          if (!attribute_get(global, component, next, error)) {
            return false;
          }
          global = std::move(next);
          if (dot == std::string::npos) {
            break;
          }
          start = dot + 1;
        }
        reader.stack.push_back(std::move(global));
        break;
      }
      case 'R': { // REDUCE
        if (reader.stack.size() < 2) {
          error = "pickle REDUCE needs a callable and arguments";
          return false;
        }
        Value arguments = reader.stack.back();
        reader.stack.pop_back();
        Value callable = reader.stack.back();
        reader.stack.pop_back();
        auto* tuple = value_as_tuple(arguments);
        if (tuple == nullptr) {
          error = "pickle REDUCE arguments are not a tuple";
          return false;
        }
        Value reduced;
        if (!runtime_call_callable(
                runtime,
                callable,
                tuple->items.empty() ? nullptr : tuple->items.begin(),
                static_cast<uint32_t>(tuple->items.size()),
                reduced,
                error)) {
          return false;
        }
        reader.stack.push_back(reduced);
        break;
      }
      case 'b': { // BUILD
        if (reader.stack.size() < 2) {
          error = "pickle BUILD needs an instance and state";
          return false;
        }
        Value state = reader.stack.back();
        reader.stack.pop_back();
        Value& instance = reader.stack.back();
        if (!pickle_apply_state(runtime, instance, state, error)) {
          return false;
        }
        break;
      }
      case ']':
        reader.stack.push_back(Value::list({}));
        break;
      case '}':
        reader.stack.push_back(Value::dict({}));
        break;
      case ')':
        reader.stack.push_back(Value::tuple({}));
        break;
      case 0x8f:
        reader.stack.push_back(Value::set({}));
        break;
      case '(':
        reader.marks.push_back(reader.stack.size());
        break;
      case 'e': {
        if (reader.marks.empty() || reader.marks.back() == 0 || reader.marks.back() > reader.stack.size()) {
          error = "bad pickle mark";
          return false;
        }
        size_t mark = reader.marks.back();
        reader.marks.pop_back();
        Value list_value = reader.stack[mark - 1];
        auto* list = value_as_list(list_value);
        if (list == nullptr) {
          error = "pickle APPENDS target is not list";
          return false;
        }
        for (size_t i = mark; i < reader.stack.size(); ++i) {
          list->items.push_back(reader.stack[i]);
        }
        reader.stack.resize(mark);
        reader.stack[mark - 1] = list_value;
        break;
      }
      case 't': {
        if (reader.marks.empty() || reader.marks.back() > reader.stack.size()) {
          error = "bad pickle mark";
          return false;
        }
        size_t mark = reader.marks.back();
        reader.marks.pop_back();
        std::vector<Value> items;
        for (size_t i = mark; i < reader.stack.size(); ++i) {
          items.push_back(reader.stack[i]);
        }
        reader.stack.resize(mark);
        reader.stack.push_back(Value::tuple(std::move(items)));
        break;
      }
      case 'u': {
        if (reader.marks.empty() || reader.marks.back() == 0 || reader.marks.back() > reader.stack.size()) {
          error = "bad pickle mark";
          return false;
        }
        size_t mark = reader.marks.back();
        reader.marks.pop_back();
        if (((reader.stack.size() - mark) % 2) != 0) {
          error = "pickle SETITEMS needs key/value pairs";
          return false;
        }
        Value dict_value = reader.stack[mark - 1];
        if (value_as_dict(dict_value) == nullptr) {
          error = "pickle SETITEMS target is not dict";
          return false;
        }
        for (size_t i = mark; i < reader.stack.size(); i += 2) {
          if (!mapping_set_item(dict_value, reader.stack[i], reader.stack[i + 1], error)) {
            return false;
          }
        }
        reader.stack.resize(mark);
        reader.stack[mark - 1] = dict_value;
        break;
      }
      case 0x90: {
        if (reader.marks.empty() || reader.marks.back() == 0 || reader.marks.back() > reader.stack.size()) {
          error = "bad pickle mark";
          return false;
        }
        size_t mark = reader.marks.back();
        reader.marks.pop_back();
        std::vector<Value> items;
        for (size_t i = mark; i < reader.stack.size(); ++i) {
          items.push_back(reader.stack[i]);
        }
        reader.stack.resize(mark);
        reader.stack[mark - 1] = Value::set(std::move(items));
        break;
      }
      case '.':
        if (reader.stack.empty()) {
          error = "empty pickle stack";
          return false;
        }
        out = reader.stack.back();
        return true;
      default:
        error = "unsupported pickle opcode";
        raise_pickle_module_error(runtime, "UnpicklingError", error);
        return false;
    }
  }
  error = "pickle data missing STOP";
  return false;
}

bool pickle_dumps(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "pickle.dumps() expected object and optional protocol";
    return false;
  }
  int protocol = kPickleHighestProtocol;
  if (argc == 2 && args[1].tag == ValueTag::Int64) {
    protocol = static_cast<int>(args[1].as.i64);
  }
  if (protocol < 0) {
    protocol = kPickleHighestProtocol;
  }
  if (protocol > kPickleHighestProtocol) {
    error = "pickle protocol not supported";
    return false;
  }
  std::string payload;
  payload.push_back(static_cast<char>(0x80)); // PROTO
  payload.push_back(static_cast<char>(protocol));
  if (!pickle_write_value(runtime, args[0], protocol, payload, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      runtime.set_pending_exception(std::move(pending));
    } else {
      raise_pickle_module_error(runtime, "PicklingError", error);
    }
    return false;
  }
  payload.push_back('.');
  out = Value::bytes(std::move(payload));
  return true;
}

bool pickle_dumps_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    error = "pickle.dumps() expected object and optional protocol";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> positional(args, args + argc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (kwargs[i].value == nullptr) {
      error = "pickle.dumps() received an invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (name == "protocol") {
      if (positional.size() == 2) {
        error = "pickle.dumps() got multiple values for argument 'protocol'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      positional.push_back(*kwargs[i].value);
    } else if (name != "fix_imports" && name != "buffer_callback") {
      error = "pickle.dumps() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return pickle_dumps(runtime, positional.data(), static_cast<uint32_t>(positional.size()), out, error, nullptr);
}

bool pickle_loads(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pickle.loads() expected data";
    return false;
  }
  std::string_view payload;
  if (!get_bytes_view(args[0], payload, error)) {
    return false;
  }
  if (payload.size() >= 4 && payload.substr(0, 4) == "X3P1") {
    Value loads;
    if (!marshal_function(runtime, "loads", loads, error)) {
      return false;
    }
    Value marshaled = Value::bytes(std::string(payload.substr(4)));
    return runtime_call_callable(runtime, loads, &marshaled, 1, out, error);
  }
  return pickle_read_value(runtime, payload, out, error);
}

bool pickle_dump(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "pickle.dump() expected object, file, and optional protocol";
    return false;
  }
  Value data;
  if (!pickle_dumps(runtime, args, argc == 3 ? 2 : 1, data, error, nullptr)) {
    return false;
  }
  Value write;
  if (!attribute_get(args[1], "write", write, error)) {
    return false;
  }
  Value ignored;
  if (!runtime_call_callable(runtime, write, &data, 1, ignored, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool pickle_dump_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    error = "pickle.dump() expected object, file, and optional protocol";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> positional(args, args + argc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (kwargs[i].value == nullptr) {
      error = "pickle.dump() received an invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (name == "protocol") {
      if (positional.size() == 3) {
        error = "pickle.dump() got multiple values for argument 'protocol'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      positional.push_back(*kwargs[i].value);
    } else if (name != "fix_imports" && name != "buffer_callback") {
      error = "pickle.dump() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return pickle_dump(runtime, positional.data(), static_cast<uint32_t>(positional.size()), out, error, nullptr);
}

bool pickle_load(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pickle.load() expected file";
    return false;
  }
  Value read;
  if (!attribute_get(args[0], "read", read, error)) {
    return false;
  }
  std::string payload;
  for (;;) {
    Value read_size = Value::int64(1);
    Value chunk;
    if (!runtime_call_callable(runtime, read, &read_size, 1, chunk, error)) {
      return false;
    }
    std::string_view chunk_view;
    if (!get_bytes_view(chunk, chunk_view, error)) {
      return false;
    }
    if (chunk_view.empty()) {
      break;
    }
    payload.append(chunk_view.data(), chunk_view.size());
    const PickleScanResult scan = scan_pickle_record(payload, error);
    if (scan == PickleScanResult::Invalid) {
      raise_pickle_module_error(runtime, "UnpicklingError", error);
      return false;
    }
    if (scan == PickleScanResult::Complete) {
      Value data = Value::bytes(std::move(payload));
      return pickle_loads(runtime, &data, 1, out, error, nullptr);
    }
  }
  Value data = Value::bytes(std::move(payload));
  return pickle_loads(runtime, &data, 1, out, error, nullptr);
}

bool pickler_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "Pickler() expected file and optional protocol";
    return false;
  }
  auto* state = new PicklerState();
  state->file = args[1];
  if (argc == 3 && args[2].tag == ValueTag::Int64) {
    state->protocol = static_cast<int>(args[2].as.i64);
  }
  if (!instance_set_native_data(args[0], kPicklerNativeType, state, pickler_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool pickler_dump(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Pickler.dump() expected object";
    return false;
  }
  auto* state = static_cast<PicklerState*>(instance_get_native_data(args[0], kPicklerNativeType));
  if (state == nullptr) {
    error = "invalid Pickler object";
    return false;
  }
  Value dispatch_table;
  const Value* dispatch_table_ptr = nullptr;
  std::string dispatch_error;
  if (object_get_attr(args[0], "dispatch_table", dispatch_table, dispatch_error) &&
      mapping_is_mapping(dispatch_table)) {
    dispatch_table_ptr = &dispatch_table;
  }
  std::string payload;
  payload.push_back(static_cast<char>(0x80)); // PROTO
  payload.push_back(static_cast<char>(state->protocol));
  if (!pickle_write_value(runtime, args[1], state->protocol, payload, error, dispatch_table_ptr)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      runtime.set_pending_exception(std::move(pending));
    } else {
      raise_pickle_module_error(runtime, "PicklingError", error);
    }
    return false;
  }
  payload.push_back('.');
  Value data = Value::bytes(std::move(payload));
  Value write;
  if (!attribute_get(state->file, "write", write, error)) {
    return false;
  }
  Value ignored;
  if (!runtime_call_callable(runtime, write, &data, 1, ignored, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool unpickler_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Unpickler() expected file";
    return false;
  }
  auto* state = new UnpicklerState();
  state->file = args[1];
  if (!instance_set_native_data(args[0], kUnpicklerNativeType, state, unpickler_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool unpickler_load(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Unpickler.load() expected no arguments";
    return false;
  }
  auto* state = static_cast<UnpicklerState*>(instance_get_native_data(args[0], kUnpicklerNativeType));
  if (state == nullptr) {
    error = "invalid Unpickler object";
    return false;
  }
  return pickle_load(runtime, &state->file, 1, out, error, nullptr);
}

Value make_pickler_class(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string(name)});
  attrs.push_back({"__qualname__", Value::string("Pickler")});
  attrs.push_back({
      "__text_signature__",
      Value::string("(file, protocol=None, fix_imports=True, buffer_callback=None)")});
  attrs.push_back({"__init__", runtime.make_native_function(std::string(name) + ".Pickler.__init__", pickler_init)});
  Value dump = runtime.make_native_function(std::string(name) + ".Pickler.dump", pickler_dump);
  builtin_method_set_text_signature(dump, "($self, obj, /)");
  attrs.push_back({"dump", std::move(dump)});
  const Value* object_class = runtime.find_builtin("object");
  return Value::class_object(
      "Pickler",
      std::move(attrs),
      object_class != nullptr ? *object_class : Value::invalid());
}

Value make_unpickler_class(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string(name)});
  attrs.push_back({"__qualname__", Value::string("Unpickler")});
  attrs.push_back({"__init__", runtime.make_native_function(std::string(name) + ".Unpickler.__init__", unpickler_init)});
  attrs.push_back({"load", runtime.make_native_function(std::string(name) + ".Unpickler.load", unpickler_load)});
  const Value* object_class = runtime.find_builtin("object");
  return Value::class_object(
      "Unpickler",
      std::move(attrs),
      object_class != nullptr ? *object_class : Value::invalid());
}

Value make_pickle_module(Runtime& runtime, const char* name) {
  Value exception_base = runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::invalid();
  Value pickle_error = Value::class_object("PickleError", {}, exception_base);
  Value pickling_error = Value::class_object("PicklingError", {}, pickle_error);
  Value unpickling_error = Value::class_object("UnpicklingError", {}, pickle_error);

  std::vector<std::pair<std::string, Value>> buffer_attrs;
  buffer_attrs.push_back({"__init__", runtime.make_native_function(std::string(name) + ".PickleBuffer.__init__", picklebuffer_init)});

  NativeModuleBuilder builder(runtime, name);
  builder.value("HIGHEST_PROTOCOL", Value::int64(kPickleHighestProtocol))
      .value("DEFAULT_PROTOCOL", Value::int64(5))
      .value("PickleError", pickle_error)
      .value("PicklingError", pickling_error)
      .value("UnpicklingError", unpickling_error)
      .value("Pickler", make_pickler_class(runtime, name))
      .value("Unpickler", make_unpickler_class(runtime, name))
      .value("PickleBuffer", Value::class_object("PickleBuffer", std::move(buffer_attrs)))
      .function("dump", pickle_dump, nullptr, false, pickle_dump_kw)
      .function("dumps", pickle_dumps, nullptr, false, pickle_dumps_kw)
      .function("load", pickle_load)
      .function("loads", pickle_loads);
  return builder.finish();
}

} // namespace

void register_pickle_module(Runtime& runtime) {
  runtime.register_module("_pickle", make_pickle_module(runtime, "_pickle"));
}

} // namespace xlang3
