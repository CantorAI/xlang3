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

bool pickle_write_value(const Value& value, std::string& out, std::string& error) {
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
        if (!pickle_write_value(item, out, error)) {
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
      if (!pickle_write_value(item, out, error)) {
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
        if (!pickle_write_value(entry.first, out, error) || !pickle_write_value(entry.second, out, error)) {
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
        if (!pickle_write_value(item, out, error)) {
          return false;
        }
      }
      out.push_back(static_cast<char>(0x90)); // ADDITEMS
    }
    return true;
  }

  error = "cannot pickle this object yet";
  return false;
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
};

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
        runtime.raise_class_error("UnpicklingError", error);
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
  if (!pickle_write_value(args[0], payload, error)) {
    runtime.raise_class_error("PicklingError", error);
    return false;
  }
  payload.push_back('.');
  out = Value::bytes(std::move(payload));
  return true;
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

bool pickle_load(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pickle.load() expected file";
    return false;
  }
  Value read;
  if (!attribute_get(args[0], "read", read, error)) {
    return false;
  }
  Value data;
  if (!runtime_call_callable(runtime, read, nullptr, 0, data, error)) {
    return false;
  }
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
  Value dump_args[] = {args[1], state->file, Value::int64(state->protocol)};
  return pickle_dump(runtime, dump_args, 3, out, error, nullptr);
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
  attrs.push_back({"__init__", runtime.make_native_function(std::string(name) + ".Pickler.__init__", pickler_init)});
  attrs.push_back({"dump", runtime.make_native_function(std::string(name) + ".Pickler.dump", pickler_dump)});
  return Value::class_object("Pickler", std::move(attrs));
}

Value make_unpickler_class(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function(std::string(name) + ".Unpickler.__init__", unpickler_init)});
  attrs.push_back({"load", runtime.make_native_function(std::string(name) + ".Unpickler.load", unpickler_load)});
  return Value::class_object("Unpickler", std::move(attrs));
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
      .function("dump", pickle_dump)
      .function("dumps", pickle_dumps)
      .function("load", pickle_load)
      .function("loads", pickle_loads);
  return builder.finish();
}

} // namespace

void register_pickle_module(Runtime& runtime) {
  runtime.register_module("_pickle", make_pickle_module(runtime, "_pickle"));
  runtime.register_module("pickle", make_pickle_module(runtime, "pickle"));
}

} // namespace xlang3
