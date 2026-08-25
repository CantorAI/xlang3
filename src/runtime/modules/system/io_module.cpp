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

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <algorithm>

namespace xlang3 {

namespace {

struct MemoryStreamState {
  std::string buffer;
  size_t cursor = 0;
  bool binary = false;
  bool closed = false;
};

void memory_stream_cleanup(void* data) {
  delete static_cast<MemoryStreamState*>(data);
}

MemoryStreamState* memory_stream_state(const Value& self, const char* type, std::string& error) {
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(self, type));
  if (state == nullptr) {
    error = "invalid memory stream object";
    return nullptr;
  }
  if (state->closed) {
    error = "I/O operation on closed file";
    return nullptr;
  }
  return state;
}

bool string_value(const Value& value, std::string& out) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  return false;
}

bool bytes_value(const Value& value, std::string& out) {
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  return false;
}

Value memory_stream_result(const MemoryStreamState& state, std::string data) {
  return state.binary ? Value::bytes(std::move(data)) : Value::string(std::move(data));
}

bool memory_stream_init(const char* type, bool binary, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (argc > 2) {
    error = "memory stream constructor expected optional initial value";
    return false;
  }
  auto* state = new MemoryStreamState();
  state->binary = binary;
  if (argc == 2) {
    bool ok = binary ? bytes_value(args[1], state->buffer) : string_value(args[1], state->buffer);
    if (!ok) {
      delete state;
      error = binary ? "BytesIO initial value must be bytes-like" : "StringIO initial value must be str";
      return false;
    }
  }
  if (!instance_set_native_data(args[0], type, state, memory_stream_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool string_io_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return memory_stream_init("_io.StringIO", false, args, argc, out, error);
}

bool bytes_io_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return memory_stream_init("_io.BytesIO", true, args, argc, out, error);
}

bool stream_read(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream read() expected optional size";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  size_t size = state->buffer.size() - std::min(state->cursor, state->buffer.size());
  if (argc == 2 && args[1].tag == ValueTag::Int64 && args[1].as.i64 >= 0) {
    size = std::min<size_t>(size, static_cast<size_t>(args[1].as.i64));
  }
  const size_t start = std::min(state->cursor, state->buffer.size());
  out = memory_stream_result(*state, state->buffer.substr(start, size));
  state->cursor = start + size;
  return true;
}

bool stream_readline(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream readline() expected optional size";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  size_t limit = state->buffer.size();
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "memory stream readline size must be int";
      return false;
    }
    if (args[1].as.i64 >= 0) {
      limit = std::min(state->buffer.size(), state->cursor + static_cast<size_t>(args[1].as.i64));
    }
  }
  const size_t start = std::min(state->cursor, state->buffer.size());
  size_t end = start;
  while (end < limit && end < state->buffer.size()) {
    ++end;
    if (state->buffer[end - 1] == '\n') {
      break;
    }
  }
  out = memory_stream_result(*state, state->buffer.substr(start, end - start));
  state->cursor = end;
  return true;
}

bool stream_readlines(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream readlines() expected optional hint";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> lines;
  while (state->cursor < state->buffer.size()) {
    const size_t start = std::min(state->cursor, state->buffer.size());
    size_t end = start;
    while (end < state->buffer.size()) {
      ++end;
      if (state->buffer[end - 1] == '\n') {
        break;
      }
    }
    lines.push_back(memory_stream_result(*state, state->buffer.substr(start, end - start)));
    state->cursor = end;
  }
  out = Value::list(std::move(lines));
  return true;
}

bool stream_write(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "memory stream write() expected data";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  std::string data;
  const bool ok = state->binary ? bytes_value(args[1], data) : string_value(args[1], data);
  if (!ok) {
    error = state->binary ? "BytesIO.write() argument must be bytes-like" : "StringIO.write() argument must be str";
    return false;
  }
  if (state->cursor > state->buffer.size()) {
    state->cursor = state->buffer.size();
  }
  if (state->cursor + data.size() > state->buffer.size()) {
    state->buffer.resize(state->cursor + data.size(), '\0');
  }
  std::copy(data.begin(), data.end(), state->buffer.begin() + static_cast<std::ptrdiff_t>(state->cursor));
  state->cursor += data.size();
  value_set_int64(out, static_cast<int64_t>(data.size()));
  return true;
}

bool stream_writelines(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "memory stream writelines() expected iterable";
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    Value write_args[2] = {args[0], item};
    Value ignored;
    if (!stream_write(runtime, write_args, 2, ignored, error, user_data)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool stream_getvalue(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream getvalue() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  out = memory_stream_result(*state, state->buffer);
  return true;
}

bool stream_seek(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 2 || argc > 3 || args[1].tag != ValueTag::Int64) {
    error = "memory stream seek() expected offset and optional whence";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  const int64_t whence = argc == 3 && args[2].tag == ValueTag::Int64 ? args[2].as.i64 : 0;
  int64_t base = 0;
  if (whence == 1) {
    base = static_cast<int64_t>(state->cursor);
  } else if (whence == 2) {
    base = static_cast<int64_t>(state->buffer.size());
  } else if (whence != 0) {
    error = "invalid whence";
    return false;
  }
  int64_t next = base + args[1].as.i64;
  if (next < 0) {
    next = 0;
  }
  state->cursor = static_cast<size_t>(next);
  value_set_int64(out, static_cast<int64_t>(state->cursor));
  return true;
}

bool stream_tell(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream tell() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(state->cursor));
  return true;
}

bool stream_close(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream close() expected no arguments";
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], static_cast<const char*>(user_data)));
  if (state == nullptr) {
    error = "invalid memory stream object";
    return false;
  }
  state->closed = true;
  value_set_none(out);
  return true;
}

bool stream_closed(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream closed() expected no arguments";
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], static_cast<const char*>(user_data)));
  if (state == nullptr) {
    error = "invalid memory stream object";
    return false;
  }
  value_set_bool(out, state->closed);
  return true;
}

bool stream_capability(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream capability method expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  value_set_bool(out, true);
  return true;
}

bool stream_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream __enter__() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  if (memory_stream_state(args[0], type, error) == nullptr) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool stream_exit(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 4) {
    error = "memory stream __exit__() expected exc details";
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], static_cast<const char*>(user_data)));
  if (state == nullptr) {
    error = "invalid memory stream object";
    return false;
  }
  state->closed = true;
  value_set_bool(out, false);
  return true;
}

bool io_open_code(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "io.open_code() expected path";
    return false;
  }
  const Value* open_value = runtime.find_builtin("open");
  auto* open_fn = open_value == nullptr ? nullptr : value_as_native_function(*open_value);
  if (open_fn == nullptr || open_fn->callback == nullptr) {
    error = "builtin open is not available";
    return false;
  }
  Value open_args[2] = {args[0], Value::string("rb")};
  return open_fn->callback(runtime, open_args, 2, out, error, open_fn->user_data);
}

Value make_memory_stream_class(Runtime& runtime, const char* name, const char* type, NativeFunctionCallback init) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function(std::string("_io.") + name + ".__init__", init)});
  attrs.push_back({"__enter__", runtime.make_native_function(std::string("_io.") + name + ".__enter__", stream_enter, const_cast<char*>(type))});
  attrs.push_back({"__exit__", runtime.make_native_function(std::string("_io.") + name + ".__exit__", stream_exit, const_cast<char*>(type))});
  attrs.push_back({"read", runtime.make_native_function(std::string("_io.") + name + ".read", stream_read, const_cast<char*>(type))});
  attrs.push_back({"readline", runtime.make_native_function(std::string("_io.") + name + ".readline", stream_readline, const_cast<char*>(type))});
  attrs.push_back({"readlines", runtime.make_native_function(std::string("_io.") + name + ".readlines", stream_readlines, const_cast<char*>(type))});
  attrs.push_back({"write", runtime.make_native_function(std::string("_io.") + name + ".write", stream_write, const_cast<char*>(type))});
  attrs.push_back({"writelines", runtime.make_native_function(std::string("_io.") + name + ".writelines", stream_writelines, const_cast<char*>(type))});
  attrs.push_back({"getvalue", runtime.make_native_function(std::string("_io.") + name + ".getvalue", stream_getvalue, const_cast<char*>(type))});
  attrs.push_back({"seek", runtime.make_native_function(std::string("_io.") + name + ".seek", stream_seek, const_cast<char*>(type))});
  attrs.push_back({"tell", runtime.make_native_function(std::string("_io.") + name + ".tell", stream_tell, const_cast<char*>(type))});
  attrs.push_back({"close", runtime.make_native_function(std::string("_io.") + name + ".close", stream_close, const_cast<char*>(type))});
  attrs.push_back({"closed", runtime.make_native_function(std::string("_io.") + name + ".closed", stream_closed, const_cast<char*>(type))});
  attrs.push_back({"readable", runtime.make_native_function(std::string("_io.") + name + ".readable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"writable", runtime.make_native_function(std::string("_io.") + name + ".writable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"seekable", runtime.make_native_function(std::string("_io.") + name + ".seekable", stream_capability, const_cast<char*>(type))});
  return Value::class_object(name, std::move(attrs));
}

void add_io_exports(NativeModuleBuilder& builder, Runtime& runtime, const Value& string_io, const Value& bytes_io) {
  if (const Value* open = runtime.find_builtin("open")) {
    builder.value("open", *open);
  }
  Value io_base = Value::class_object("IOBase", {});
  Value text_io_base = Value::class_object("TextIOBase", {});
  Value buffered_io_base = Value::class_object("BufferedIOBase", {});
  builder.value("IOBase", io_base)
      .value("TextIOBase", text_io_base)
      .value("BufferedIOBase", buffered_io_base)
      .value("StringIO", string_io)
      .value("BytesIO", bytes_io)
      .value("open_code", runtime.make_native_function("io.open_code", io_open_code))
      .value("DEFAULT_BUFFER_SIZE", Value::int64(8192));
}

} // namespace

void register_io_module(Runtime& runtime) {
  Value string_io = make_memory_stream_class(runtime, "StringIO", "_io.StringIO", string_io_init);
  Value bytes_io = make_memory_stream_class(runtime, "BytesIO", "_io.BytesIO", bytes_io_init);

  NativeModuleBuilder low_level(runtime, "_io");
  add_io_exports(low_level, runtime, string_io, bytes_io);
  runtime.register_module("_io", low_level.finish());

  NativeModuleBuilder high_level(runtime, "io");
  add_io_exports(high_level, runtime, std::move(string_io), std::move(bytes_io));
  runtime.register_module("io", high_level.finish());
}

} // namespace xlang3
