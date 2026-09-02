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
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "source_encoding.h"

#include <algorithm>

namespace xlang3 {

namespace {

struct MemoryStreamState {
  std::string buffer;
  Value wrapped_buffer;
  std::string encoding = "utf-8";
  size_t cursor = 0;
  bool binary = false;
  bool closed = false;
  bool wraps_buffer = false;
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

bool memory_stream_init(
    const char* type,
    bool binary,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
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
  for (uint32_t i = 0; i < kwargc; ++i) {
    const char* name = kwargs[i].name;
    const Value* value = kwargs[i].value;
    if (name == nullptr || value == nullptr) {
      delete state;
      error = "memory stream constructor received invalid keyword";
      return false;
    }
    const std::string_view keyword(name);
    if ((!binary && keyword == "initial_value") || (binary && keyword == "initial_bytes")) {
      bool ok = binary ? bytes_value(*value, state->buffer) : string_value(*value, state->buffer);
      if (!ok) {
        delete state;
        error = binary ? "BytesIO initial_bytes must be bytes-like" : "StringIO initial_value must be str";
        return false;
      }
    } else if (!binary && keyword == "newline") {
      if (value->tag != ValueTag::None && value_as_string(*value) == nullptr) {
        delete state;
        error = "StringIO newline must be str or None";
        return false;
      }
    } else {
      delete state;
      error = std::string(binary ? "BytesIO" : "StringIO") + " got an unexpected keyword argument '" + std::string(keyword) + "'";
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
  return memory_stream_init("_io.StringIO", false, args, argc, nullptr, 0, out, error);
}

bool bytes_io_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return memory_stream_init("_io.BytesIO", true, args, argc, nullptr, 0, out, error);
}

bool string_io_init_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return memory_stream_init("_io.StringIO", false, args, argc, kwargs, kwargc, out, error);
}

bool bytes_io_init_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return memory_stream_init("_io.BytesIO", true, args, argc, kwargs, kwargc, out, error);
}

std::string text_io_encoding_from_args(
    const Value* args,
    uint32_t argc,
    uint32_t encoding_index,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc) {
  if (argc > encoding_index) {
    if (auto* encoding = value_as_string(args[encoding_index])) {
      return string_object_to_string(*encoding);
    }
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string_view(kwargs[i].name) == "encoding" && kwargs[i].value != nullptr) {
      if (auto* encoding = value_as_string(*kwargs[i].value)) {
        return string_object_to_string(*encoding);
      }
    }
  }
  return "utf-8";
}

bool text_io_wrapper_load_buffer(
    Runtime& runtime,
    const Value& self,
    const Value& buffer,
    std::string encoding,
    Value& out,
    std::string& error) {
  auto* state = new MemoryStreamState();
  state->binary = false;
  state->wraps_buffer = true;
  state->wrapped_buffer = buffer;
  state->encoding = std::move(encoding);
  if (!instance_set_native_data(self, "_io.TextIOWrapper", state, memory_stream_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool text_io_wrapper_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "_io.TextIOWrapper() missing required buffer argument";
    return false;
  }
  return text_io_wrapper_load_buffer(runtime, args[0], args[1], text_io_encoding_from_args(args, argc, 2, nullptr, 0), out, error);
}

bool text_io_wrapper_new(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "_io.TextIOWrapper() missing required buffer argument";
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "_io.TextIOWrapper.__new__ first argument must be a class";
    return false;
  }
  out = Value::instance(args[0]);
  Value ignored;
  return text_io_wrapper_load_buffer(runtime, out, args[1], text_io_encoding_from_args(args, argc, 2, nullptr, 0), ignored, error);
}

bool text_io_wrapper_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 2) {
    error = "_io.TextIOWrapper() missing required buffer argument";
    return false;
  }
  return text_io_wrapper_load_buffer(runtime, args[0], args[1], text_io_encoding_from_args(args, argc, 2, kwargs, kwargc), out, error);
}

bool text_io_wrapper_new_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 2) {
    error = "_io.TextIOWrapper() missing required buffer argument";
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "_io.TextIOWrapper.__new__ first argument must be a class";
    return false;
  }
  out = Value::instance(args[0]);
  Value ignored;
  return text_io_wrapper_load_buffer(runtime, out, args[1], text_io_encoding_from_args(args, argc, 2, kwargs, kwargc), ignored, error);
}

bool buffered_stream_load_buffer(const Value& self, const Value& buffer, const char* type, Value& out, std::string& error) {
  auto* state = new MemoryStreamState();
  state->binary = true;
  state->wraps_buffer = true;
  state->wrapped_buffer = buffer;
  if (!instance_set_native_data(self, type, state, memory_stream_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool buffered_reader_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "_io.BufferedReader() expected raw stream and optional buffer size";
    return false;
  }
  return buffered_stream_load_buffer(args[0], args[1], "_io.BufferedReader", out, error);
}

bool buffered_writer_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "_io.BufferedWriter() expected raw stream and optional buffer size";
    return false;
  }
  return buffered_stream_load_buffer(args[0], args[1], "_io.BufferedWriter", out, error);
}

bool buffered_random_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "_io.BufferedRandom() expected raw stream and optional buffer size";
    return false;
  }
  return buffered_stream_load_buffer(args[0], args[1], "_io.BufferedRandom", out, error);
}

bool buffered_rw_pair_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 4) {
    error = "_io.BufferedRWPair() expected reader, writer, and optional buffer size";
    return false;
  }
  return buffered_stream_load_buffer(args[0], args[1], "_io.BufferedRWPair", out, error);
}

bool decode_text_io_data(const Value& data, const std::string& encoding, Value& out, std::string& error) {
  if (auto* string = value_as_string(data)) {
    out = Value::string(string_object_to_string(*string));
    return true;
  }
  std::string bytes;
  if (!bytes_value(data, bytes)) {
    error = "_io.TextIOWrapper buffer read() must return bytes or str";
    return false;
  }
  PythonSourceText decoded;
  if (!decode_python_source_bytes_as(bytes, encoding, decoded, error)) {
    return false;
  }
  out = Value::string(std::move(decoded.text));
  return true;
}

bool stream_read(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream read() expected optional size";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  if (state->wraps_buffer) {
    Value read_method;
    std::string read_error;
    if (!attribute_get(state->wrapped_buffer, "read", read_method, read_error)) {
      Value readinto_method;
      if (!attribute_get(state->wrapped_buffer, "readinto", readinto_method, error)) {
        error = read_error.empty() ? error : read_error;
        return false;
      }
      const int64_t requested_size = argc == 2 && args[1].tag == ValueTag::Int64 ? args[1].as.i64 : -1;
      const size_t chunk_size = requested_size >= 0 ? static_cast<size_t>(requested_size) : 8192;
      std::string collected;
      for (;;) {
        Value buffer = Value::bytearray(std::string(chunk_size, '\0'));
        Value readinto_result;
        if (!runtime_call_callable(runtime, readinto_method, &buffer, 1, readinto_result, error)) {
          return false;
        }
        if (readinto_result.tag == ValueTag::None) {
          value_set_none(out);
          return true;
        }
        if (readinto_result.tag != ValueTag::Int64) {
          error = "_io.BufferedReader readinto() returned non-int";
          return false;
        }
        const size_t count = readinto_result.as.i64 <= 0 ? 0 : static_cast<size_t>(readinto_result.as.i64);
        auto* array = value_as_bytearray(buffer);
        if (array == nullptr) {
          error = "_io.BufferedReader internal buffer is invalid";
          return false;
        }
        if (count > array->value.size()) {
          error = "_io.BufferedReader readinto() returned an invalid byte count";
          return false;
        }
        if (requested_size >= 0) {
          out = Value::bytes(array->value.substr(0, count));
          return true;
        }
        if (count == 0) {
          out = Value::bytes(std::move(collected));
          return true;
        }
        collected.append(array->value.data(), count);
      }
    }
    Value data;
    const Value* read_args = argc == 2 ? &args[1] : nullptr;
    const uint32_t read_argc = argc == 2 ? 1 : 0;
    if (!runtime_call_callable(runtime, read_method, read_args, read_argc, data, error)) {
      return false;
    }
    if (state->binary) {
      value_assign_fast(out, data);
      return true;
    }
    return decode_text_io_data(data, state->encoding, out, error);
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

bool stream_readline(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream readline() expected optional size";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  if (state->wraps_buffer) {
    Value read_method;
    std::string readline_error;
    if (state->binary) {
      Value readinto_method;
      if (attribute_get(state->wrapped_buffer, "readinto", readinto_method, readline_error)) {
        const int64_t limit = argc == 2 && args[1].tag == ValueTag::Int64 ? args[1].as.i64 : -1;
        std::string line;
        while (limit < 0 || static_cast<int64_t>(line.size()) < limit) {
          Value buffer = Value::bytearray(std::string(1, '\0'));
          Value readinto_result;
          if (!runtime_call_callable(runtime, readinto_method, &buffer, 1, readinto_result, error)) {
            return false;
          }
          if (readinto_result.tag == ValueTag::None) {
            value_set_none(out);
            return true;
          }
          if (readinto_result.tag != ValueTag::Int64) {
            error = "_io.BufferedReader readinto() returned non-int";
            return false;
          }
          if (readinto_result.as.i64 <= 0) {
            out = Value::bytes(std::move(line));
            return true;
          }
          auto* array = value_as_bytearray(buffer);
          if (array == nullptr || array->value.empty()) {
            error = "_io.BufferedReader internal buffer is invalid";
            return false;
          }
          line.push_back(array->value[0]);
          if (array->value[0] == '\n') {
            out = Value::bytes(std::move(line));
            return true;
          }
        }
        out = Value::bytes(std::move(line));
        return true;
      }
    }
    if (!attribute_get(state->wrapped_buffer, "readline", read_method, readline_error)) {
      error = readline_error;
      return false;
    }
    Value data;
    const Value* read_args = argc == 2 ? &args[1] : nullptr;
    const uint32_t read_argc = argc == 2 ? 1 : 0;
    if (!runtime_call_callable(runtime, read_method, read_args, read_argc, data, error)) {
      return false;
    }
    if (state->binary) {
      value_assign_fast(out, data);
      return true;
    }
    return decode_text_io_data(data, state->encoding, out, error);
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

bool stream_readlines(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream readlines() expected optional hint";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  if (state->wraps_buffer) {
    std::vector<Value> lines;
    for (;;) {
      Value line;
      if (!stream_readline(runtime, args, 1, line, error, user_data)) {
        return false;
      }
      bool empty = false;
      if (auto* text = value_as_string(line)) {
        empty = string_object_view(*text).empty();
      } else if (auto* bytes = value_as_bytes(line)) {
        empty = bytes_object_view(*bytes).empty();
      }
      if (empty) {
        break;
      }
      lines.push_back(std::move(line));
    }
    out = Value::list(std::move(lines));
    return true;
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

bool stream_write(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "memory stream write() expected data";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  if (state->wraps_buffer) {
    std::string data;
    if (!string_value(args[1], data)) {
      error = "TextIOWrapper.write() argument must be str";
      return false;
    }
    Value write_method;
    if (!attribute_get(state->wrapped_buffer, "write", write_method, error)) {
      return false;
    }
    Value bytes_arg = Value::bytes(data);
    Value ignored;
    if (!runtime_call_callable(runtime, write_method, &bytes_arg, 1, ignored, error)) {
      return false;
    }
    value_set_int64(out, static_cast<int64_t>(utf8_codepoint_count(data)));
    return true;
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

bool stream_truncate(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream truncate() expected optional size";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  size_t size = state->cursor;
  if (argc == 2 && args[1].tag != ValueTag::None) {
    if (args[1].tag != ValueTag::Int64 || args[1].as.i64 < 0) {
      error = "memory stream truncate size must be a non-negative int";
      return false;
    }
    size = static_cast<size_t>(args[1].as.i64);
  }
  state->buffer.resize(size, '\0');
  value_set_int64(out, static_cast<int64_t>(size));
  return true;
}

bool stream_close(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream close() expected no arguments";
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], static_cast<const char*>(user_data)));
  if (state == nullptr) {
    error = "invalid memory stream object";
    return false;
  }
  if (state->wraps_buffer && !state->closed) {
    Value close_method;
    std::string ignored;
    if (attribute_get(state->wrapped_buffer, "close", close_method, ignored)) {
      Value close_result;
      if (!runtime_call_callable(runtime, close_method, nullptr, 0, close_result, error)) {
        return false;
      }
    }
  }
  state->closed = true;
  value_set_none(out);
  return true;
}

bool stream_flush(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream flush() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  if (state->wraps_buffer) {
    Value flush_method;
    std::string ignored;
    if (attribute_get(state->wrapped_buffer, "flush", flush_method, ignored)) {
      Value flush_result;
      if (!runtime_call_callable(runtime, flush_method, nullptr, 0, flush_result, error)) {
        return false;
      }
    }
  }
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

Value make_memory_stream_class(
    Runtime& runtime,
    const char* name,
    const char* type,
    NativeFunctionCallback init,
    NativeKeywordFunctionCallback init_kw = nullptr) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function(std::string("_io.") + name + ".__init__", init, nullptr, nullptr, nullptr, false, init_kw)});
  if (std::string_view(name) == "TextIOWrapper") {
    attrs.push_back({"__new__", runtime.make_native_function("_io.TextIOWrapper.__new__", text_io_wrapper_new, nullptr, nullptr, nullptr, false, text_io_wrapper_new_kw)});
  }
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
  attrs.push_back({"truncate", runtime.make_native_function(std::string("_io.") + name + ".truncate", stream_truncate, const_cast<char*>(type))});
  attrs.push_back({"close", runtime.make_native_function(std::string("_io.") + name + ".close", stream_close, const_cast<char*>(type))});
  attrs.push_back({"flush", runtime.make_native_function(std::string("_io.") + name + ".flush", stream_flush, const_cast<char*>(type))});
  attrs.push_back({"closed", runtime.make_native_function(std::string("_io.") + name + ".closed", stream_closed, const_cast<char*>(type))});
  attrs.push_back({"readable", runtime.make_native_function(std::string("_io.") + name + ".readable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"writable", runtime.make_native_function(std::string("_io.") + name + ".writable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"seekable", runtime.make_native_function(std::string("_io.") + name + ".seekable", stream_capability, const_cast<char*>(type))});
  return Value::class_object(name, std::move(attrs));
}

Value make_buffered_stream_class(Runtime& runtime, const char* name, const char* type, NativeFunctionCallback init) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function(std::string("_io.") + name + ".__init__", init)});
  attrs.push_back({"__enter__", runtime.make_native_function(std::string("_io.") + name + ".__enter__", stream_enter, const_cast<char*>(type))});
  attrs.push_back({"__exit__", runtime.make_native_function(std::string("_io.") + name + ".__exit__", stream_exit, const_cast<char*>(type))});
  attrs.push_back({"read", runtime.make_native_function(std::string("_io.") + name + ".read", stream_read, const_cast<char*>(type))});
  attrs.push_back({"readline", runtime.make_native_function(std::string("_io.") + name + ".readline", stream_readline, const_cast<char*>(type))});
  attrs.push_back({"readlines", runtime.make_native_function(std::string("_io.") + name + ".readlines", stream_readlines, const_cast<char*>(type))});
  attrs.push_back({"write", runtime.make_native_function(std::string("_io.") + name + ".write", stream_write, const_cast<char*>(type))});
  attrs.push_back({"writelines", runtime.make_native_function(std::string("_io.") + name + ".writelines", stream_writelines, const_cast<char*>(type))});
  attrs.push_back({"flush", runtime.make_native_function(std::string("_io.") + name + ".flush", stream_flush, const_cast<char*>(type))});
  attrs.push_back({"truncate", runtime.make_native_function(std::string("_io.") + name + ".truncate", stream_truncate, const_cast<char*>(type))});
  attrs.push_back({"close", runtime.make_native_function(std::string("_io.") + name + ".close", stream_close, const_cast<char*>(type))});
  attrs.push_back({"closed", runtime.make_native_function(std::string("_io.") + name + ".closed", stream_closed, const_cast<char*>(type))});
  attrs.push_back({"readable", runtime.make_native_function(std::string("_io.") + name + ".readable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"writable", runtime.make_native_function(std::string("_io.") + name + ".writable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"seekable", runtime.make_native_function(std::string("_io.") + name + ".seekable", stream_capability, const_cast<char*>(type))});
  return Value::class_object(name, std::move(attrs));
}

Value make_unsupported_operation_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("io")});
  attrs.push_back({"__qualname__", Value::string("UnsupportedOperation")});
  Value klass = Value::class_object("UnsupportedOperation", std::move(attrs));
  std::string ignored;
  if (const Value* os_error = runtime.find_builtin("OSError")) {
    class_set_base(klass, *os_error, ignored);
  }
  if (const Value* value_error = runtime.find_builtin("ValueError")) {
    class_set_base(klass, *value_error, ignored);
  }
  return klass;
}

bool io_text_encoding(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "_io.text_encoding() expected one or two arguments";
    return false;
  }
  if (args[0].tag != ValueTag::None) {
    value_assign_fast(out, args[0]);
  } else {
    out = Value::string("locale");
  }
  return true;
}

bool io_base_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_io._IOBase.__enter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool io_base_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "_io._IOBase.__exit__() expected exc details";
    return false;
  }
  Value close_method;
  if (!object_get_attr(args[0], "close", close_method, error)) {
    return false;
  }
  Value ignored;
  if (!runtime_call_callable(runtime, close_method, nullptr, 0, ignored, error)) {
    return false;
  }
  value_set_bool(out, false);
  return true;
}

bool io_base_init(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_io._IOBase.__init__() expected no arguments";
    return false;
  }
  value_set_none(out);
  return true;
}

bool io_base_close(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_io._IOBase.close() expected no arguments";
    return false;
  }
  Value self = args[0];
  if (!object_set_attr(self, "__xlang3_io_closed", Value::boolean(true), error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool io_base_closed_get(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_io._IOBase.closed getter expected self";
    return false;
  }
  Value state_value;
  std::string ignored;
  if (object_get_attr(args[0], "__xlang3_io_closed", state_value, ignored)) {
    value_set_bool(out, value_truthy(state_value));
    return true;
  }
  value_set_bool(out, false);
  return true;
}

bool io_base_check_closed(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_io._IOBase._checkClosed() expected no arguments";
    return false;
  }
  Value closed;
  if (!io_base_closed_get(runtime, args, argc, closed, error, nullptr)) {
    return false;
  }
  if (value_truthy(closed)) {
    error = "I/O operation on closed file";
    return false;
  }
  value_set_none(out);
  return true;
}

bool io_base_check_capability(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = std::string("_io._IOBase.") + static_cast<const char*>(user_data) + "() expected no arguments";
    return false;
  }
  const std::string_view check_name(static_cast<const char*>(user_data));
  const char* capability_name = check_name == "_checkReadable" ? "readable" : "writable";
  Value capability;
  if (!object_get_attr(args[0], capability_name, capability, error)) {
    return false;
  }
  Value allowed;
  if (!runtime_call_callable(runtime, capability, nullptr, 0, allowed, error)) {
    return false;
  }
  if (!value_truthy(allowed)) {
    error = check_name == "_checkReadable" ? "File or stream is not readable" : "File or stream is not writable";
    return false;
  }
  value_set_none(out);
  return true;
}

bool io_base_false_method(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = std::string("_io._IOBase.") + static_cast<const char*>(user_data) + "() expected no arguments";
    return false;
  }
  value_set_bool(out, false);
  return true;
}

bool io_base_readline(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "_io._IOBase.readline() expected optional limit";
    return false;
  }
  int64_t limit = -1;
  if (argc == 2 && args[1].tag == ValueTag::Int64) {
    limit = args[1].as.i64;
  }
  Value read_method;
  if (!object_get_attr(args[0], "read", read_method, error)) {
    return false;
  }
  std::string bytes;
  while (limit < 0 || static_cast<int64_t>(bytes.size()) < limit) {
    Value read_arg = Value::int64(1);
    Value chunk;
    if (!runtime_call_callable(runtime, read_method, &read_arg, 1, chunk, error)) {
      return false;
    }
    std::string_view view;
    if (auto* chunk_bytes = value_as_bytes(chunk)) {
      view = bytes_object_view(*chunk_bytes);
    } else if (auto* chunk_text = value_as_string(chunk)) {
      view = string_object_view(*chunk_text);
    } else {
      error = "_io._IOBase.readline() read() returned non-bytes";
      return false;
    }
    if (view.empty()) {
      break;
    }
    bytes.append(view.data(), view.size());
    if (view.find('\n') != std::string_view::npos) {
      break;
    }
  }
  out = Value::bytes(std::move(bytes));
  return true;
}

bool io_base_readlines(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "_io._IOBase.readlines() expected optional hint";
    return false;
  }
  Value readline_method;
  if (!object_get_attr(args[0], "readline", readline_method, error)) {
    return false;
  }
  std::vector<Value> lines;
  for (;;) {
    Value line;
    if (!runtime_call_callable(runtime, readline_method, nullptr, 0, line, error)) {
      return false;
    }
    bool empty = false;
    if (auto* bytes = value_as_bytes(line)) {
      empty = bytes_object_view(*bytes).empty();
    } else if (auto* text = value_as_string(line)) {
      empty = string_object_view(*text).empty();
    }
    if (empty) {
      break;
    }
    lines.push_back(std::move(line));
  }
  out = Value::list(std::move(lines));
  return true;
}

void add_io_exports(NativeModuleBuilder& builder, Runtime& runtime, const Value& string_io, const Value& bytes_io) {
  if (const Value* open = runtime.find_builtin("open")) {
    builder.value("open", *open);
  }
  Value closed_getter = runtime.make_native_function("_io._IOBase.closed.get", io_base_closed_get);
  const std::vector<std::pair<std::string, Value>> base_attrs = {
      {"__doc__", Value::none()},
      {"__init__", runtime.make_native_function("_io._IOBase.__init__", io_base_init)},
      {"__enter__", runtime.make_native_function("_io._IOBase.__enter__", io_base_enter)},
      {"__exit__", runtime.make_native_function("_io._IOBase.__exit__", io_base_exit)},
      {"close", runtime.make_native_function("_io._IOBase.close", io_base_close)},
      {"closed", Value::property(std::move(closed_getter), Value::none(), Value::none(), Value::none())},
      {"_checkClosed", runtime.make_native_function("_io._IOBase._checkClosed", io_base_check_closed)},
      {"_checkReadable", runtime.make_native_function("_io._IOBase._checkReadable", io_base_check_capability, const_cast<char*>("_checkReadable"))},
      {"_checkWritable", runtime.make_native_function("_io._IOBase._checkWritable", io_base_check_capability, const_cast<char*>("_checkWritable"))},
      {"readable", runtime.make_native_function("_io._IOBase.readable", io_base_false_method, const_cast<char*>("readable"))},
      {"writable", runtime.make_native_function("_io._IOBase.writable", io_base_false_method, const_cast<char*>("writable"))},
      {"seekable", runtime.make_native_function("_io._IOBase.seekable", io_base_false_method, const_cast<char*>("seekable"))},
      {"readline", runtime.make_native_function("_io._IOBase.readline", io_base_readline)},
      {"readlines", runtime.make_native_function("_io._IOBase.readlines", io_base_readlines)},
  };
  Value io_base = Value::class_object("_IOBase", base_attrs);
  Value raw_io_base = Value::class_object("_RawIOBase", base_attrs, io_base);
  Value text_io_base = Value::class_object("_TextIOBase", base_attrs);
  Value buffered_io_base = Value::class_object("_BufferedIOBase", base_attrs, io_base);
  Value file_io = Value::class_object("FileIO", {}, raw_io_base);
  Value buffered_reader = make_buffered_stream_class(runtime, "BufferedReader", "_io.BufferedReader", buffered_reader_init);
  Value buffered_writer = make_buffered_stream_class(runtime, "BufferedWriter", "_io.BufferedWriter", buffered_writer_init);
  Value buffered_random = make_buffered_stream_class(runtime, "BufferedRandom", "_io.BufferedRandom", buffered_random_init);
  Value buffered_rw_pair = make_buffered_stream_class(runtime, "BufferedRWPair", "_io.BufferedRWPair", buffered_rw_pair_init);
  Value text_io_wrapper = make_memory_stream_class(runtime, "TextIOWrapper", "_io.TextIOWrapper", text_io_wrapper_init, text_io_wrapper_init_kw);
  std::string ignored;
  class_set_base(buffered_reader, buffered_io_base, ignored);
  class_set_base(buffered_writer, buffered_io_base, ignored);
  class_set_base(buffered_random, buffered_io_base, ignored);
  class_set_base(buffered_rw_pair, buffered_io_base, ignored);
  class_set_base(text_io_wrapper, text_io_base, ignored);
  Value incremental_newline_decoder = Value::class_object("IncrementalNewlineDecoder", {});
  builder.value("_IOBase", io_base)
      .value("_RawIOBase", raw_io_base)
      .value("_TextIOBase", text_io_base)
      .value("_BufferedIOBase", buffered_io_base)
      .value("IOBase", io_base)
      .value("RawIOBase", raw_io_base)
      .value("TextIOBase", text_io_base)
      .value("BufferedIOBase", buffered_io_base)
      .value("FileIO", file_io)
      .value("BufferedReader", buffered_reader)
      .value("BufferedWriter", buffered_writer)
      .value("BufferedRandom", buffered_random)
      .value("BufferedRWPair", buffered_rw_pair)
      .value("TextIOWrapper", text_io_wrapper)
      .value("IncrementalNewlineDecoder", incremental_newline_decoder)
      .value("StringIO", string_io)
      .value("BytesIO", bytes_io)
      .value("open_code", runtime.make_native_function("io.open_code", io_open_code))
      .value("text_encoding", runtime.make_native_function("_io.text_encoding", io_text_encoding))
      .value("DEFAULT_BUFFER_SIZE", Value::int64(131072));
}

} // namespace

void register_io_module(Runtime& runtime) {
  Value string_io = make_memory_stream_class(runtime, "StringIO", "_io.StringIO", string_io_init, string_io_init_kw);
  Value bytes_io = make_memory_stream_class(runtime, "BytesIO", "_io.BytesIO", bytes_io_init, bytes_io_init_kw);
  Value unsupported_operation = make_unsupported_operation_class(runtime);

  NativeModuleBuilder low_level(runtime, "_io");
  add_io_exports(low_level, runtime, string_io, bytes_io);
  low_level.value("UnsupportedOperation", unsupported_operation);
  if (const Value* blocking_io_error = runtime.find_builtin("BlockingIOError")) {
    low_level.value("BlockingIOError", *blocking_io_error);
  }
  runtime.register_module("_io", low_level.finish());
}

} // namespace xlang3
