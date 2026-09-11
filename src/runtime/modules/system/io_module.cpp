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
  std::string errors = "strict";
  std::string newline;
  bool newline_is_none = true;
  uint8_t seen_newlines = 0;
  size_t cursor = 0;
  bool binary = false;
  bool closed = false;
  bool wraps_buffer = false;
  bool line_buffering = false;
  bool write_through = false;
  Value exported_buffer = Value::invalid();
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

void memory_stream_sync_exported_buffer(MemoryStreamState& state) {
  if (auto* exported = value_as_bytearray(state.exported_buffer)) {
    state.buffer = exported->value;
  }
}

void memory_stream_update_exported_buffer(MemoryStreamState& state) {
  if (auto* exported = value_as_bytearray(state.exported_buffer)) {
    exported->value = state.buffer;
  }
}

bool memory_stream_has_active_export(const MemoryStreamState& state) {
  const auto* exported = value_as_bytearray(state.exported_buffer);
  return exported != nullptr && exported->buffer_exports > 0;
}

bool memory_stream_export_allowed(Runtime& runtime, const MemoryStreamState& state, std::string& error) {
  if (!memory_stream_has_active_export(state)) return true;
  error = "Existing exports of data: object cannot be re-sized";
  runtime.raise_class_error("BufferError", error);
  return false;
}

bool string_value(const Value& value, std::string& out) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  return false;
}

void note_stringio_newlines(MemoryStreamState& state, std::string_view text) {
  if (state.binary || (!state.newline_is_none && !state.newline.empty())) return;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') {
        state.seen_newlines |= 4;
        ++i;
      } else {
        state.seen_newlines |= 1;
      }
    } else if (text[i] == '\n') {
      state.seen_newlines |= 2;
    }
  }
}

std::string translate_stringio_newlines(MemoryStreamState& state, std::string text) {
  note_stringio_newlines(state, text);
  if (state.binary || (!state.newline_is_none && state.newline.empty())) return text;
  std::string translated;
  translated.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (state.newline_is_none && text[i] == '\r') {
      translated.push_back('\n');
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
    } else if (!state.newline_is_none && text[i] == '\n') {
      translated += state.newline;
    } else {
      translated.push_back(text[i]);
    }
  }
  return translated;
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
  if (auto* view = value_as_memoryview(value)) {
    if (view->released) {
      return false;
    }
    const auto bytes = memoryview_object_view(*view);
    if (bytes.data() != nullptr) {
      out.assign(bytes.data(), bytes.size());
      return true;
    }
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
      state->newline_is_none = value->tag == ValueTag::None;
      state->newline.clear();
      if (!state->newline_is_none) {
        state->newline = string_object_to_string(*value_as_string(*value));
      }
    } else {
      delete state;
      error = std::string(binary ? "BytesIO" : "StringIO") + " got an unexpected keyword argument '" + std::string(keyword) + "'";
      return false;
    }
  }
  if (!binary) state->buffer = translate_stringio_newlines(*state, std::move(state->buffer));
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

std::string text_io_option_from_args(
    const Value* args,
    uint32_t argc,
    uint32_t encoding_index,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    std::string_view name = "encoding",
    const char* fallback = "utf-8") {
  if (argc > encoding_index) {
    if (auto* encoding = value_as_string(args[encoding_index])) {
      return string_object_to_string(*encoding);
    }
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string_view(kwargs[i].name) == name && kwargs[i].value != nullptr) {
      if (auto* encoding = value_as_string(*kwargs[i].value)) {
        return string_object_to_string(*encoding);
      }
    }
  }
  return fallback;
}

void text_io_newline_from_args(
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    std::string& newline,
    bool& newline_is_none) {
  const Value* value = argc > 4 ? &args[4] : nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string_view(kwargs[i].name) == "newline") {
      value = kwargs[i].value;
      break;
    }
  }
  newline_is_none = value == nullptr || value->tag == ValueTag::None;
  newline.clear();
  if (!newline_is_none) {
    if (auto* string = value_as_string(*value)) {
      newline = string_object_to_string(*string);
    }
  }
}

bool text_io_flag_from_args(
    const Value* args,
    uint32_t argc,
    uint32_t positional_index,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    std::string_view name) {
  const Value* value = argc > positional_index ? &args[positional_index] : nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string_view(kwargs[i].name) == name) {
      value = kwargs[i].value;
      break;
    }
  }
  return value != nullptr && value_truthy(*value);
}

bool text_io_wrapper_load_buffer(
    Runtime& runtime,
    const Value& self,
    const Value& buffer,
    std::string encoding,
    std::string errors,
    std::string newline,
    bool newline_is_none,
    bool line_buffering,
    bool write_through,
    Value& out,
    std::string& error) {
  auto* state = new MemoryStreamState();
  state->binary = false;
  state->wraps_buffer = true;
  state->wrapped_buffer = buffer;
  state->encoding = std::move(encoding);
  state->errors = std::move(errors);
  state->newline = std::move(newline);
  state->newline_is_none = newline_is_none;
  state->line_buffering = line_buffering;
  state->write_through = write_through;
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
  std::string newline;
  bool newline_is_none = true;
  text_io_newline_from_args(args, argc, nullptr, 0, newline, newline_is_none);
  return text_io_wrapper_load_buffer(runtime, args[0], args[1], text_io_option_from_args(args, argc, 2, nullptr, 0), text_io_option_from_args(args, argc, 3, nullptr, 0, "errors", "strict"), std::move(newline), newline_is_none, text_io_flag_from_args(args, argc, 5, nullptr, 0, "line_buffering"), text_io_flag_from_args(args, argc, 6, nullptr, 0, "write_through"), out, error);
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
  std::string newline;
  bool newline_is_none = true;
  text_io_newline_from_args(args, argc, nullptr, 0, newline, newline_is_none);
  return text_io_wrapper_load_buffer(runtime, out, args[1], text_io_option_from_args(args, argc, 2, nullptr, 0), text_io_option_from_args(args, argc, 3, nullptr, 0, "errors", "strict"), std::move(newline), newline_is_none, text_io_flag_from_args(args, argc, 5, nullptr, 0, "line_buffering"), text_io_flag_from_args(args, argc, 6, nullptr, 0, "write_through"), ignored, error);
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
  std::string newline;
  bool newline_is_none = true;
  text_io_newline_from_args(args, argc, kwargs, kwargc, newline, newline_is_none);
  return text_io_wrapper_load_buffer(runtime, args[0], args[1], text_io_option_from_args(args, argc, 2, kwargs, kwargc), text_io_option_from_args(args, argc, 3, kwargs, kwargc, "errors", "strict"), std::move(newline), newline_is_none, text_io_flag_from_args(args, argc, 5, kwargs, kwargc, "line_buffering"), text_io_flag_from_args(args, argc, 6, kwargs, kwargc, "write_through"), out, error);
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
  std::string newline;
  bool newline_is_none = true;
  text_io_newline_from_args(args, argc, kwargs, kwargc, newline, newline_is_none);
  return text_io_wrapper_load_buffer(runtime, out, args[1], text_io_option_from_args(args, argc, 2, kwargs, kwargc), text_io_option_from_args(args, argc, 3, kwargs, kwargc, "errors", "strict"), std::move(newline), newline_is_none, text_io_flag_from_args(args, argc, 5, kwargs, kwargc, "line_buffering"), text_io_flag_from_args(args, argc, 6, kwargs, kwargc, "write_through"), ignored, error);
}

void io_set_instance_attr(const Value& self, const std::string& name, const Value& value) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr) return;
  for (auto& attr : instance->attrs) {
    if (attr.first == name) {
      value_assign_fast(attr.second, value);
      return;
    }
  }
  instance->attrs.push_back({name, value});
}

bool buffered_stream_load_buffer(Runtime& runtime, const Value& self, const Value& buffer, const char* type, Value& out, std::string& error) {
  auto* state = new MemoryStreamState();
  state->binary = true;
  state->wraps_buffer = true;
  state->wrapped_buffer = buffer;
  if (!instance_set_native_data(self, type, state, memory_stream_cleanup, error)) {
    delete state;
    return false;
  }
  Value mode;
  std::string ignored;
  if (!object_get_attr(buffer, "_mode", mode, ignored)) {
    attribute_get(buffer, "mode", mode, ignored);
  }
  if (value_as_string(mode) != nullptr) {
    io_set_instance_attr(self, "mode", mode);
  }
  Value name;
  if (object_get_attr(buffer, "_sock", name, ignored)) {
    Value fileno;
    if (attribute_get(name, "fileno", fileno, ignored)) {
      Value descriptor;
      if (runtime_call_callable(runtime, fileno, nullptr, 0, descriptor, ignored)) {
        io_set_instance_attr(self, "name", descriptor);
      }
    }
  } else if (attribute_get(buffer, "name", name, ignored) && value_as_property(name) == nullptr) {
    io_set_instance_attr(self, "name", name);
  }
  value_set_none(out);
  return true;
}

bool buffered_reader_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "_io.BufferedReader() expected raw stream and optional buffer size";
    return false;
  }
  return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedReader", out, error);
}

bool buffered_writer_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "_io.BufferedWriter() expected raw stream and optional buffer size";
    return false;
  }
  return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedWriter", out, error);
}

bool buffered_random_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "_io.BufferedRandom() expected raw stream and optional buffer size";
    return false;
  }
  return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedRandom", out, error);
}

bool buffered_rw_pair_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 4) {
    error = "_io.BufferedRWPair() expected reader, writer, and optional buffer size";
    return false;
  }
  return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedRWPair", out, error);
}

bool decode_text_io_data(
    Runtime& runtime,
    const Value& data,
    MemoryStreamState& state,
    Value& out,
    std::string& error) {
  const auto normalize_newlines = [&](std::string text) {
    if (!state.newline_is_none) {
      return text;
    }
    std::string normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\r') {
        if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        normalized.push_back('\n');
      } else {
        normalized.push_back(text[i]);
      }
    }
    return normalized;
  };
  if (auto* string = value_as_string(data)) {
    std::string text = string_object_to_string(*string);
    note_stringio_newlines(state, text);
    out = Value::string(normalize_newlines(std::move(text)));
    return true;
  }
  std::string bytes;
  if (!bytes_value(data, bytes)) {
    error = "_io.TextIOWrapper buffer read() must return bytes or str";
    return false;
  }
  Value encoded = Value::bytes(std::move(bytes));
  Value decode;
  if (!attribute_get(encoded, "decode", decode, error)) {
    return false;
  }
  Value decode_args[] = {Value::string(state.encoding), Value::string(state.errors)};
  Value decoded;
  if (!runtime_call_callable(runtime, decode, decode_args, 2, decoded, error)) {
    return false;
  }
  auto* string = value_as_string(decoded);
  if (string == nullptr) {
    error = "_io.TextIOWrapper decoder must return str";
    return false;
  }
  std::string text = string_object_to_string(*string);
  note_stringio_newlines(state, text);
  out = Value::string(normalize_newlines(std::move(text)));
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
    if (error == "I/O operation on closed file") {
      runtime.raise_class_error("ValueError", error);
    }
    return false;
  }
  memory_stream_sync_exported_buffer(*state);
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
    return decode_text_io_data(runtime, data, *state, out, error);
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

bool stream_readinto(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "BytesIO.readinto() expected one buffer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = memory_stream_state(args[0], static_cast<const char*>(user_data), error);
  if (state == nullptr) return false;
  memory_stream_sync_exported_buffer(*state);
  char* destination = nullptr;
  size_t capacity = 0;
  if (auto* bytearray = value_as_bytearray(args[1])) {
    destination = bytearray->value.data();
    capacity = bytearray->value.size();
  } else if (auto* view = value_as_memoryview(args[1]); view != nullptr && !view->readonly) {
    destination = memoryview_object_writable_data(*view);
    capacity = view->size;
  }
  if (destination == nullptr && capacity != 0) {
    error = "readinto() argument must be read-write bytes-like object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (state->wraps_buffer) {
    Value read_args[] = {args[0], Value::int64(static_cast<int64_t>(capacity))};
    Value data;
    if (!stream_read(runtime, read_args, 2, data, error, user_data)) {
      return false;
    }
    std::string bytes;
    if (!bytes_value(data, bytes)) {
      error = "readinto() source did not return bytes";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (bytes.size() > capacity) {
      error = "readinto() source returned too many bytes";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    if (!bytes.empty()) std::memcpy(destination, bytes.data(), bytes.size());
    out = Value::int64(static_cast<int64_t>(bytes.size()));
    return true;
  }
  const size_t available = state->cursor >= state->buffer.size() ? 0 : state->buffer.size() - state->cursor;
  const size_t count = std::min(capacity, available);
  if (count != 0) std::memcpy(destination, state->buffer.data() + state->cursor, count);
  state->cursor += count;
  out = Value::int64(static_cast<int64_t>(count));
  return true;
}

bool buffered_stream_peek(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "BufferedReader.peek() expected optional size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = memory_stream_state(args[0], static_cast<const char*>(user_data), error);
  if (state == nullptr || !state->wraps_buffer) return false;
  int64_t size = 8192;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "BufferedReader.peek() size must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (args[1].as.i64 > 0) size = args[1].as.i64;
  }
  Value tell;
  Value seek;
  if (!attribute_get(state->wrapped_buffer, "tell", tell, error) ||
      !attribute_get(state->wrapped_buffer, "seek", seek, error)) return false;
  Value position;
  if (!runtime_call_callable(runtime, tell, nullptr, 0, position, error)) return false;
  Value read;
  if (!attribute_get(state->wrapped_buffer, "read", read, error)) return false;
  Value count = Value::int64(size);
  if (!runtime_call_callable(runtime, read, &count, 1, out, error)) return false;
  Value restore[] = {position};
  Value ignored;
  return runtime_call_callable(runtime, seek, restore, 1, ignored, error);
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
  memory_stream_sync_exported_buffer(*state);
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
    return decode_text_io_data(runtime, data, *state, out, error);
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
    bool ends_line = state->buffer[end - 1] == '\n';
    if (!state->binary && !state->newline_is_none) {
      if (state->newline.empty()) {
        ends_line = state->buffer[end - 1] == '\r' || state->buffer[end - 1] == '\n';
        if (state->buffer[end - 1] == '\r' && end < limit && end < state->buffer.size() &&
            state->buffer[end] == '\n') {
          ++end;
        }
      } else {
        const size_t newline_size = state->newline.size();
        ends_line = end >= newline_size &&
            state->buffer.compare(end - newline_size, newline_size, state->newline) == 0;
      }
    }
    if (ends_line) {
      break;
    }
  }
  out = memory_stream_result(*state, state->buffer.substr(start, end - start));
  state->cursor = end;
  return true;
}

bool stream_iter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "memory stream __iter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool stream_next(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream __next__() expected no arguments";
    return false;
  }
  if (!stream_readline(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  bool empty = false;
  if (auto* text = value_as_string(out)) {
    empty = string_object_view(*text).empty();
  } else if (auto* bytes = value_as_bytes(out)) {
    empty = bytes_object_view(*bytes).empty();
  }
  if (empty) {
    error = "StopIteration";
    runtime.raise_class_error("StopIteration", "");
    return false;
  }
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
  if (state->wraps_buffer && !state->binary) {
    std::string data;
    if (!string_value(args[1], data)) {
      error = "TextIOWrapper.write() argument must be str";
      return false;
    }
    const int64_t written = static_cast<int64_t>(utf8_codepoint_count(data));
    const bool flush_line = state->line_buffering &&
        (data.find('\n') != std::string::npos || data.find('\r') != std::string::npos);
    std::string translated;
    const std::string replacement = state->newline_is_none
#if defined(_WIN32)
        ? "\r\n"
#else
        ? "\n"
#endif
        : state->newline;
    if (!replacement.empty() && replacement != "\n") {
      translated.reserve(data.size());
      for (char ch : data) {
        if (ch == '\n') {
          translated += replacement;
        } else {
          translated.push_back(ch);
        }
      }
      data = std::move(translated);
    }
    Value text = Value::string(std::move(data));
    Value encode;
    if (!attribute_get(text, "encode", encode, error)) {
      return false;
    }
    Value encode_args[] = {Value::string(state->encoding), Value::string(state->errors)};
    Value bytes_arg;
    if (!runtime_call_callable(runtime, encode, encode_args, 2, bytes_arg, error)) {
      return false;
    }
    Value write_method;
    if (!attribute_get(state->wrapped_buffer, "write", write_method, error)) {
      return false;
    }
    Value ignored;
    if (!runtime_call_callable(runtime, write_method, &bytes_arg, 1, ignored, error)) {
      return false;
    }
    if (flush_line) {
      Value flush_method;
      if (attribute_get(state->wrapped_buffer, "flush", flush_method, error)) {
        Value flush_result;
        if (!runtime_call_callable(runtime, flush_method, nullptr, 0, flush_result, error)) {
          return false;
        }
      }
    }
    value_set_int64(out, written);
    return true;
  }
  if (state->wraps_buffer) {
    Value write_method;
    if (!attribute_get(state->wrapped_buffer, "write", write_method, error)) {
      return false;
    }
    return runtime_call_callable(runtime, write_method, args + 1, 1, out, error);
  }
  std::string data;
  const bool ok = state->binary ? bytes_value(args[1], data) : string_value(args[1], data);
  if (!ok) {
    error = state->binary ? "BytesIO.write() argument must be bytes-like" : "StringIO.write() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const size_t input_size = data.size();
  if (state->binary && !memory_stream_export_allowed(runtime, *state, error)) {
    return false;
  }
  if (!state->binary) {
    data = translate_stringio_newlines(*state, std::move(data));
  }
  if (state->cursor > state->buffer.size()) {
    state->cursor = state->buffer.size();
  }
  if (state->cursor + data.size() > state->buffer.size()) {
    state->buffer.resize(state->cursor + data.size(), '\0');
  }
  std::copy(data.begin(), data.end(), state->buffer.begin() + static_cast<std::ptrdiff_t>(state->cursor));
  state->cursor += data.size();
  memory_stream_update_exported_buffer(*state);
  value_set_int64(out, static_cast<int64_t>(state->binary ? data.size() : input_size));
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
  memory_stream_sync_exported_buffer(*state);
  out = memory_stream_result(*state, state->buffer);
  return true;
}

bool stream_getbuffer(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "BytesIO.getbuffer() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr || !state->binary) {
    if (error.empty()) error = "getbuffer() requires a binary memory stream";
    return false;
  }
  memory_stream_sync_exported_buffer(*state);
  if (state->exported_buffer.tag == ValueTag::Invalid) {
    state->exported_buffer = Value::bytearray(state->buffer);
  }
  out = Value::memoryview(state->exported_buffer, 0, state->buffer.size(), false);
  return true;
}

bool stream_seek(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 2 || argc > 3 || args[1].tag != ValueTag::Int64) {
    error = "memory stream seek() expected offset and optional whence";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  memory_stream_sync_exported_buffer(*state);
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

bool stream_detach(Runtime& runtime, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  if (argc != 1) {
    error = "detach() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  error = "detach";
  Value module;
  Value unsupported;
  std::string ignored;
  if (runtime.import_module("_io", module, ignored) &&
      module_get_attr(module, "UnsupportedOperation", unsupported, ignored) &&
      value_as_class(unsupported) != nullptr) {
    runtime.set_pending_exception(runtime.make_exception_from_class(std::move(unsupported), error));
    return false;
  }
  runtime.raise_class_error("OSError", error);
  return false;
}

bool text_io_wrapper_detach(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "detach() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], "_io.TextIOWrapper"));
  if (state == nullptr || !state->wraps_buffer || state->closed) {
    error = "underlying buffer has been detached";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  value_assign_fast(out, state->wrapped_buffer);
  value_set_invalid(state->wrapped_buffer);
  state->wraps_buffer = false;
  state->closed = true;
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

bool stream_truncate(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream truncate() expected optional size";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  memory_stream_sync_exported_buffer(*state);
  size_t size = state->cursor;
  if (argc == 2 && args[1].tag != ValueTag::None) {
    if (args[1].tag != ValueTag::Int64 || args[1].as.i64 < 0) {
      error = "memory stream truncate size must be a non-negative int";
      return false;
    }
    size = static_cast<size_t>(args[1].as.i64);
  }
  if (state->binary && !memory_stream_export_allowed(runtime, *state, error)) {
    return false;
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
  if (state->binary && !memory_stream_export_allowed(runtime, *state, error)) {
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

bool string_io_newlines(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "StringIO.newlines expected no arguments";
    return false;
  }
  auto* state = memory_stream_state(args[0], static_cast<const char*>(user_data), error);
  if (state == nullptr) return false;
  if (!state->newline_is_none && !state->newline.empty()) {
    value_set_none(out);
    return true;
  }
  std::vector<Value> values;
  if ((state->seen_newlines & 1) != 0) values.push_back(Value::string("\r"));
  if ((state->seen_newlines & 2) != 0) values.push_back(Value::string("\n"));
  if ((state->seen_newlines & 4) != 0) values.push_back(Value::string("\r\n"));
  if (values.empty()) value_set_none(out);
  else if (values.size() == 1) value_assign_fast(out, values.front());
  else out = Value::tuple(std::move(values));
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

bool stream_isatty(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream isatty() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], type));
  if (state == nullptr) {
    error = "invalid memory stream object";
    return false;
  }
  if (state->closed) {
    error = "I/O operation on closed file";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  value_set_bool(out, false);
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

bool stream_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 4) {
    error = "memory stream __exit__() expected exc details";
    return false;
  }
  Value close_result;
  if (!stream_close(runtime, args, 1, close_result, error, user_data)) {
    return false;
  }
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

bool file_io_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  // FileIO is the unbuffered binary view of the runtime's descriptor-backed
  // FileObject.  Route construction through builtin open so path handling,
  // descriptors, closefd, and custom openers remain identical.
  if (argc < 2 || argc > 5) {
    error = "FileIO() expected file and at most mode, closefd, and opener";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value file = args[1];
  Value mode = argc >= 3 ? args[2] : Value::string("r");
  Value closefd = argc >= 4 ? args[3] : Value::boolean(true);
  Value opener = argc >= 5 ? args[4] : Value::none();
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "FileIO() received an invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const std::string key(kwargs[i].name);
    if (key == "mode") {
      if (argc >= 3) {
        error = "FileIO() got multiple values for argument 'mode'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      mode = *kwargs[i].value;
    } else if (key == "closefd") {
      if (argc >= 4) {
        error = "FileIO() got multiple values for argument 'closefd'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      closefd = *kwargs[i].value;
    } else if (key == "opener") {
      if (argc >= 5) {
        error = "FileIO() got multiple values for argument 'opener'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      opener = *kwargs[i].value;
    } else {
      error = "FileIO() got an unexpected keyword argument '" + key + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  std::string mode_text;
  if (!string_value(mode, mode_text)) {
    error = "FileIO() mode must be a string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (mode_text.find('t') != std::string::npos) {
    error = "FileIO() does not support text mode";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (mode_text.find('b') == std::string::npos) {
    mode_text.push_back('b');
  }
  const Value* open_value = runtime.find_builtin("open");
  auto* open_fn = open_value == nullptr ? nullptr : value_as_native_function(*open_value);
  if (open_fn == nullptr || open_fn->callback == nullptr) {
    error = "builtin open is not available";
    return false;
  }
  Value open_args[] = {
      file, Value::string(std::move(mode_text)), Value::int64(0), Value::none(),
      Value::none(), Value::none(), closefd, opener};
  return open_fn->callback(runtime, open_args, 8, out, error, open_fn->user_data);
}

bool file_io_new_positional(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  return file_io_new(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool file_io_readinto(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "FileIO.readinto() expected one buffer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  char* destination = nullptr;
  size_t capacity = 0;
  if (auto* bytearray = value_as_bytearray(args[1])) {
    destination = bytearray->value.data();
    capacity = bytearray->value.size();
  } else if (auto* view = value_as_memoryview(args[1])) {
    destination = memoryview_object_writable_data(*view);
    capacity = view->size;
  }
  if (destination == nullptr && capacity != 0) {
    error = "readinto() argument must be read-write bytes-like object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value read;
  if (!attribute_get(args[0], "read", read, error)) return false;
  Value size = Value::int64(static_cast<int64_t>(capacity));
  Value data;
  if (!runtime_call_callable(runtime, read, &size, 1, data, error)) return false;
  std::string bytes;
  if (!bytes_value(data, bytes)) {
    error = "FileIO.read() returned non-bytes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (bytes.size() > capacity) {
    error = "FileIO.read() returned too many bytes";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  if (!bytes.empty()) std::memcpy(destination, bytes.data(), bytes.size());
  value_set_int64(out, static_cast<int64_t>(bytes.size()));
  return true;
}

bool stream_fileno(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                   std::string& error, void* user_data) {
  if (argc != 1) {
    error = "fileno() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = memory_stream_state(args[0], static_cast<const char*>(user_data), error);
  if (!state) return false;
  Value method;
  if (!attribute_get(state->wrapped_buffer, "fileno", method, error)) return false;
  return runtime_call_callable(runtime, method, nullptr, 0, out, error);
}

bool buffered_wrapped_attr(Runtime&, const Value* args, uint32_t argc, Value& out,
                           std::string& error, void* user_data, const char* attr_name) {
  if (argc != 1) {
    error = std::string(attr_name) + " getter expected self";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], type));
  if (state == nullptr || !state->wraps_buffer) {
    error = "invalid buffered stream object";
    return false;
  }
  return attribute_get(state->wrapped_buffer, attr_name, out, error);
}

bool buffered_name_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                       std::string& error, void* user_data) {
  return buffered_wrapped_attr(runtime, args, argc, out, error, user_data, "name");
}

bool buffered_mode_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                       std::string& error, void* user_data) {
  return buffered_wrapped_attr(runtime, args, argc, out, error, user_data, "mode");
}

bool buffered_repr(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                   std::string& error, void* user_data) {
  if (argc != 1) {
    error = "buffered stream repr expected self";
    return false;
  }
  const char* type_name = static_cast<const char*>(user_data);
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], type_name));
  Value name;
  Value inherited_closed;
  std::string ignored;
  const bool is_closed = state != nullptr && (state->closed ||
      (object_get_attr(args[0], "__xlang3_io_closed", inherited_closed, ignored) &&
       value_truthy(inherited_closed)));
  if (is_closed) {
    name = Value::int64(-1);
  } else if (!object_get_attr(args[0], "name", name, error)) {
    name = Value::int64(-1);
    error.clear();
  }
  std::string name_text;
  if (auto* text = value_as_string(name)) {
    name_text = "'" + string_object_to_string(*text) + "'";
  } else {
    name_text = value_to_string(name);
  }
  std::string type = type_name;
  out = Value::string("<" + type + " name=" + name_text + ">");
  return true;
}

bool text_io_option_get(Runtime&, const Value* args, uint32_t argc, Value& out,
                        std::string& error, void* user_data) {
  auto* state = argc == 1 ? static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], "_io.TextIOWrapper")) : nullptr;
  if (state == nullptr) {
    error = "uninitialized TextIOWrapper";
    return false;
  }
  out = Value::string(std::string_view(static_cast<const char*>(user_data)) == "encoding"
                          ? state->encoding : state->errors);
  return true;
}

bool text_io_flag_get(Runtime&, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void* user_data) {
  auto* state = argc == 1 ? static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], "_io.TextIOWrapper")) : nullptr;
  if (state == nullptr) {
    error = "uninitialized TextIOWrapper";
    return false;
  }
  value_set_bool(out, std::string_view(static_cast<const char*>(user_data)) == "line_buffering"
                          ? state->line_buffering : state->write_through);
  return true;
}

bool text_io_buffer_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                        std::string& error, void*) {
  auto* state = argc == 1 ? static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], "_io.TextIOWrapper")) : nullptr;
  if (state == nullptr || state->closed || !state->wraps_buffer) {
    error = "underlying buffer has been detached";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  value_assign_fast(out, state->wrapped_buffer);
  return true;
}

bool text_io_name_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  auto* state = argc == 1 ? static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], "_io.TextIOWrapper")) : nullptr;
  if (state == nullptr || state->closed || !state->wraps_buffer) {
    error = "underlying buffer has been detached";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return attribute_get(state->wrapped_buffer, "name", out, error);
}

bool text_io_wrapper_reconfigure_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "TextIOWrapper.reconfigure() takes no positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], "_io.TextIOWrapper"));
  if (state == nullptr || state->closed || !state->wraps_buffer) {
    error = "I/O operation on closed file";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (kwargs[index].name == nullptr || kwargs[index].value == nullptr) {
      error = "invalid TextIOWrapper.reconfigure() keyword";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const std::string_view name(kwargs[index].name);
    const Value& value = *kwargs[index].value;
    if (name == "encoding" || name == "errors") {
      auto* text = value_as_string(value);
      if (text == nullptr) {
        error = std::string(name) + " must be str";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      if (name == "encoding") state->encoding = string_object_to_string(*text);
      else state->errors = string_object_to_string(*text);
    } else if (name == "newline") {
      if (value.tag == ValueTag::None) {
        state->newline.clear();
        state->newline_is_none = true;
      } else if (auto* text = value_as_string(value)) {
        state->newline = string_object_to_string(*text);
        state->newline_is_none = false;
      } else {
        error = "newline must be str or None";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    } else if (name == "line_buffering") {
      state->line_buffering = value_truthy(value);
    } else if (name == "write_through") {
      state->write_through = value_truthy(value);
    } else {
      error = "TextIOWrapper.reconfigure() got an unexpected keyword argument '" + std::string(name) + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  Value flush_method;
  std::string ignored;
  if (attribute_get(state->wrapped_buffer, "flush", flush_method, ignored)) {
    Value flush_result;
    if (!runtime_call_callable(runtime, flush_method, nullptr, 0, flush_result, error)) return false;
  }
  value_set_none(out);
  return true;
}

bool text_io_wrapper_reconfigure(Runtime& runtime, const Value* args, uint32_t argc,
                                 Value& out, std::string& error, void* user_data) {
  return text_io_wrapper_reconfigure_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
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
    attrs.push_back({"fileno", runtime.make_native_function("_io.TextIOWrapper.fileno", stream_fileno, const_cast<char*>(type))});
    for (const char* option : {"encoding", "errors"}) {
      Value getter = runtime.make_native_function(std::string("_io.TextIOWrapper.") + option,
          text_io_option_get, const_cast<char*>(option));
      attrs.push_back({option, Value::property(std::move(getter), Value::none(), Value::none(), Value::none())});
    }
    for (const char* option : {"line_buffering", "write_through"}) {
      Value getter = runtime.make_native_function(std::string("_io.TextIOWrapper.") + option,
          text_io_flag_get, const_cast<char*>(option));
      attrs.push_back({option, Value::property(std::move(getter), Value::none(), Value::none(), Value::none())});
    }
    attrs.push_back({"buffer", Value::property(
        runtime.make_native_function("_io.TextIOWrapper.buffer", text_io_buffer_get),
        Value::none(), Value::none(), Value::none())});
    attrs.push_back({"name", Value::property(
        runtime.make_native_function("_io.TextIOWrapper.name", text_io_name_get),
        Value::none(), Value::none(), Value::none())});
    attrs.push_back({"newlines", Value::property(
        runtime.make_native_function("_io.TextIOWrapper.newlines", string_io_newlines, const_cast<char*>(type)),
        Value::none(), Value::none(), Value::none())});
    attrs.push_back({"__new__", runtime.make_native_function("_io.TextIOWrapper.__new__", text_io_wrapper_new, nullptr, nullptr, nullptr, false, text_io_wrapper_new_kw)});
    attrs.push_back({"reconfigure", runtime.make_native_function(
        "_io.TextIOWrapper.reconfigure", text_io_wrapper_reconfigure,
        nullptr, nullptr, nullptr, false, text_io_wrapper_reconfigure_kw)});
  }
  attrs.push_back({"__enter__", runtime.make_native_function(std::string("_io.") + name + ".__enter__", stream_enter, const_cast<char*>(type))});
  attrs.push_back({"__exit__", runtime.make_native_function(std::string("_io.") + name + ".__exit__", stream_exit, const_cast<char*>(type))});
  attrs.push_back({"__iter__", runtime.make_native_function(std::string("_io.") + name + ".__iter__", stream_iter, const_cast<char*>(type))});
  attrs.push_back({"__next__", runtime.make_native_function(std::string("_io.") + name + ".__next__", stream_next, const_cast<char*>(type))});
  attrs.push_back({"read", runtime.make_native_function(std::string("_io.") + name + ".read", stream_read, const_cast<char*>(type))});
  attrs.push_back({"readline", runtime.make_native_function(std::string("_io.") + name + ".readline", stream_readline, const_cast<char*>(type))});
  attrs.push_back({"readlines", runtime.make_native_function(std::string("_io.") + name + ".readlines", stream_readlines, const_cast<char*>(type))});
  attrs.push_back({"write", runtime.make_native_function(std::string("_io.") + name + ".write", stream_write, const_cast<char*>(type))});
  attrs.push_back({"writelines", runtime.make_native_function(std::string("_io.") + name + ".writelines", stream_writelines, const_cast<char*>(type))});
  attrs.push_back({"getvalue", runtime.make_native_function(std::string("_io.") + name + ".getvalue", stream_getvalue, const_cast<char*>(type))});
  if (std::string_view(name) == "BytesIO") {
    attrs.push_back({"getbuffer", runtime.make_native_function("_io.BytesIO.getbuffer", stream_getbuffer, const_cast<char*>(type))});
    attrs.push_back({"readinto", runtime.make_native_function("_io.BytesIO.readinto", stream_readinto, const_cast<char*>(type))});
    attrs.push_back({"readinto1", runtime.make_native_function("_io.BytesIO.readinto1", stream_readinto, const_cast<char*>(type))});
  }
  if (std::string_view(name) == "StringIO") {
    attrs.push_back({"newlines", Value::property(
        runtime.make_native_function("_io.StringIO.newlines", string_io_newlines, const_cast<char*>(type)),
        Value::none(), Value::none(), Value::none())});
  }
  attrs.push_back({"seek", runtime.make_native_function(std::string("_io.") + name + ".seek", stream_seek, const_cast<char*>(type))});
  attrs.push_back({"detach", runtime.make_native_function(
      std::string("_io.") + name + ".detach",
      std::string_view(name) == "TextIOWrapper" ? text_io_wrapper_detach : stream_detach)});
  attrs.push_back({"tell", runtime.make_native_function(std::string("_io.") + name + ".tell", stream_tell, const_cast<char*>(type))});
  attrs.push_back({"truncate", runtime.make_native_function(std::string("_io.") + name + ".truncate", stream_truncate, const_cast<char*>(type))});
  attrs.push_back({"close", runtime.make_native_function(std::string("_io.") + name + ".close", stream_close, const_cast<char*>(type))});
  attrs.push_back({"flush", runtime.make_native_function(std::string("_io.") + name + ".flush", stream_flush, const_cast<char*>(type))});
  attrs.push_back({"closed", Value::property(runtime.make_native_function(std::string("_io.") + name + ".closed", stream_closed, const_cast<char*>(type)), Value::none(), Value::none(), Value::none())});
  attrs.push_back({"readable", runtime.make_native_function(std::string("_io.") + name + ".readable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"writable", runtime.make_native_function(std::string("_io.") + name + ".writable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"seekable", runtime.make_native_function(std::string("_io.") + name + ".seekable", stream_capability, const_cast<char*>(type))});
  attrs.push_back({"isatty", runtime.make_native_function(std::string("_io.") + name + ".isatty", stream_isatty, const_cast<char*>(type))});
  return Value::class_object(name, std::move(attrs));
}

Value make_buffered_stream_class(Runtime& runtime, const char* name, const char* type, NativeFunctionCallback init) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_io")});
  attrs.push_back({"__repr__", runtime.make_native_function(std::string("_io.") + name + ".__repr__", buffered_repr, const_cast<char*>(type))});
  attrs.push_back({"fileno", runtime.make_native_function(std::string("_io.") + name + ".fileno", stream_fileno, const_cast<char*>(type))});
  attrs.push_back({"__init__", runtime.make_native_function(std::string("_io.") + name + ".__init__", init)});
  attrs.push_back({"__enter__", runtime.make_native_function(std::string("_io.") + name + ".__enter__", stream_enter, const_cast<char*>(type))});
  attrs.push_back({"__exit__", runtime.make_native_function(std::string("_io.") + name + ".__exit__", stream_exit, const_cast<char*>(type))});
  attrs.push_back({"__iter__", runtime.make_native_function(std::string("_io.") + name + ".__iter__", stream_iter, const_cast<char*>(type))});
  attrs.push_back({"__next__", runtime.make_native_function(std::string("_io.") + name + ".__next__", stream_next, const_cast<char*>(type))});
  attrs.push_back({"read", runtime.make_native_function(std::string("_io.") + name + ".read", stream_read, const_cast<char*>(type))});
  if (std::string_view(name) == "BufferedReader" || std::string_view(name) == "BufferedRandom") {
    attrs.push_back({"read1", runtime.make_native_function(
        std::string("_io.") + name + ".read1", stream_read, const_cast<char*>(type))});
    attrs.push_back({"readinto", runtime.make_native_function(
        std::string("_io.") + name + ".readinto", stream_readinto, const_cast<char*>(type))});
    attrs.push_back({"readinto1", runtime.make_native_function(
        std::string("_io.") + name + ".readinto1", stream_readinto, const_cast<char*>(type))});
    attrs.push_back({"peek", runtime.make_native_function(
        std::string("_io.") + name + ".peek", buffered_stream_peek, const_cast<char*>(type))});
  }
  attrs.push_back({"readline", runtime.make_native_function(std::string("_io.") + name + ".readline", stream_readline, const_cast<char*>(type))});
  attrs.push_back({"readlines", runtime.make_native_function(std::string("_io.") + name + ".readlines", stream_readlines, const_cast<char*>(type))});
  attrs.push_back({"write", runtime.make_native_function(std::string("_io.") + name + ".write", stream_write, const_cast<char*>(type))});
  attrs.push_back({"writelines", runtime.make_native_function(std::string("_io.") + name + ".writelines", stream_writelines, const_cast<char*>(type))});
  attrs.push_back({"flush", runtime.make_native_function(std::string("_io.") + name + ".flush", stream_flush, const_cast<char*>(type))});
  attrs.push_back({"truncate", runtime.make_native_function(std::string("_io.") + name + ".truncate", stream_truncate, const_cast<char*>(type))});
  attrs.push_back({"close", runtime.make_native_function(std::string("_io.") + name + ".close", stream_close, const_cast<char*>(type))});
  attrs.push_back({"closed", Value::property(runtime.make_native_function(std::string("_io.") + name + ".closed", stream_closed, const_cast<char*>(type)), Value::none(), Value::none(), Value::none())});
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
      {"flush", runtime.make_native_function("_io._IOBase.flush", io_base_check_closed)},
      {"closed", Value::property(std::move(closed_getter), Value::none(), Value::none(), Value::none())},
      {"_checkClosed", runtime.make_native_function("_io._IOBase._checkClosed", io_base_check_closed)},
      {"_checkReadable", runtime.make_native_function("_io._IOBase._checkReadable", io_base_check_capability, const_cast<char*>("_checkReadable"))},
      {"_checkWritable", runtime.make_native_function("_io._IOBase._checkWritable", io_base_check_capability, const_cast<char*>("_checkWritable"))},
      {"readable", runtime.make_native_function("_io._IOBase.readable", io_base_false_method, const_cast<char*>("readable"))},
      {"writable", runtime.make_native_function("_io._IOBase.writable", io_base_false_method, const_cast<char*>("writable"))},
      {"seekable", runtime.make_native_function("_io._IOBase.seekable", io_base_false_method, const_cast<char*>("seekable"))},
      {"isatty", runtime.make_native_function("_io._IOBase.isatty", io_base_false_method, const_cast<char*>("isatty"))},
      {"readline", runtime.make_native_function("_io._IOBase.readline", io_base_readline)},
      {"readlines", runtime.make_native_function("_io._IOBase.readlines", io_base_readlines)},
  };
  Value io_base = Value::class_object("_IOBase", base_attrs);
  Value raw_io_base = Value::class_object("_RawIOBase", base_attrs, io_base);
  Value text_io_base = Value::class_object("_TextIOBase", base_attrs);
  Value buffered_io_base = Value::class_object("_BufferedIOBase", base_attrs, io_base);
  Value file_io = Value::class_object(
      "FileIO",
      {{"__module__", Value::string("_io")},
       {"__new__", runtime.make_native_function(
                       "_io.FileIO.__new__", file_io_new_positional, nullptr, nullptr,
                       nullptr, false, file_io_new)},
       {"readinto", runtime.make_native_function("_io.FileIO.readinto", file_io_readinto)}},
      raw_io_base);
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
