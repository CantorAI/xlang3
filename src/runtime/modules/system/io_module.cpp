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
#include <cctype>
#include <mutex>

namespace xlang3 {

namespace {

struct MemoryStreamState {
  std::recursive_mutex mutex;
  std::string buffer;
  Value wrapped_buffer;
  Value wrapped_writer;
  std::string encoding = "utf-8";
  std::string errors = "strict";
  std::string newline;
  bool newline_is_none = true;
  uint8_t seen_newlines = 0;
  size_t cursor = 0;
  size_t buffer_size = 8192;
  bool binary = false;
  bool closed = false;
  bool wraps_buffer = false;
  bool random_access = false;
  bool random_writing = false;
  bool line_buffering = false;
  bool write_through = false;
  bool text_pending_cr = false;
  std::string text_read_ahead;
  bool text_decoder_started = false;
  std::string text_decode_encoding;
  bool text_encoder_started = false;
  bool text_decoded_read = false;
  Value exported_buffer = Value::invalid();
};

bool text_contains_utf8_surrogate(std::string_view text) {
  for (size_t index = 0; index + 2 < text.size(); ++index) {
    const auto first = static_cast<unsigned char>(text[index]);
    const auto second = static_cast<unsigned char>(text[index + 1]);
    const auto third = static_cast<unsigned char>(text[index + 2]);
    if (first == 0xedu && second >= 0xa0u && second <= 0xbfu &&
        third >= 0x80u && third <= 0xbfu) return true;
  }
  return false;
}

const Value* buffered_raw_value(const Value& buffer) {
  for (const char* type : {"_io.BufferedReader", "_io.BufferedWriter",
                           "_io.BufferedRandom"}) {
    auto* state = static_cast<MemoryStreamState*>(
        instance_get_native_data(buffer, type));
    if (state != nullptr && state->wraps_buffer) return &state->wrapped_buffer;
  }
  return nullptr;
}

bool io_base_unsupported(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* user_data);
bool stream_flush(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* user_data);
bool buffered_random_prepare_read(
    Runtime& runtime, MemoryStreamState& state, std::string& error);
bool buffered_random_prepare_write(
    Runtime& runtime, MemoryStreamState& state, std::string& error);
bool text_io_flush_pending(
    Runtime& runtime, MemoryStreamState& state, std::string& error);
bool buffered_random_rewind_read(
    Runtime& runtime, MemoryStreamState& state, std::string& error);
bool buffered_stream_init_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,
    std::string& error, void* user_data);
bool text_io_wrapper_init_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,
    std::string& error, void* user_data);
bool io_open_alias(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* user_data);
bool io_open_alias_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,
    std::string& error, void* user_data);

constexpr const char* kIncrementalNewlineDecoderType = "_io.IncrementalNewlineDecoder";

struct IncrementalNewlineDecoderState {
  Value decoder;
  std::string errors = "strict";
  uint8_t seen_newlines = 0;
  bool translate = false;
  bool pending_cr = false;
};

void incremental_newline_decoder_cleanup(void* data) {
  delete static_cast<IncrementalNewlineDecoderState*>(data);
}

IncrementalNewlineDecoderState* incremental_newline_decoder_state(
    const Value& self, std::string& error) {
  auto* state = static_cast<IncrementalNewlineDecoderState*>(
      instance_get_native_data(self, kIncrementalNewlineDecoderType));
  if (state == nullptr) error = "invalid IncrementalNewlineDecoder object";
  return state;
}

bool incremental_newline_decoder_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 4) {
    error = "IncrementalNewlineDecoder() expected decoder, translate, and optional errors";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = new IncrementalNewlineDecoderState();
  value_assign_fast(state->decoder, args[1]);
  bool have_translate = argc >= 3;
  if (have_translate) state->translate = value_truthy(args[2]);
  if (argc == 4) {
    auto* text = value_as_string(args[3]);
    if (text == nullptr) {
      delete state;
      error = "IncrementalNewlineDecoder errors must be a string";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state->errors = string_object_to_string(*text);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name == "translate" && !have_translate && kwargs[i].value != nullptr) {
      state->translate = value_truthy(*kwargs[i].value);
      have_translate = true;
      continue;
    }
    if (name != "errors" || argc == 4 || kwargs[i].value == nullptr ||
        value_as_string(*kwargs[i].value) == nullptr) {
      delete state;
      error = "IncrementalNewlineDecoder() got an invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state->errors = string_object_to_string(*value_as_string(*kwargs[i].value));
  }
  if (!have_translate) {
    delete state;
    error = "IncrementalNewlineDecoder() missing required argument 'translate'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!instance_set_native_data(
          args[0], kIncrementalNewlineDecoderType, state,
          incremental_newline_decoder_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool incremental_newline_decoder_init(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* user_data) {
  return incremental_newline_decoder_init_kw(
      runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool incremental_newline_decoder_decode_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    error = "decode() expected input and optional final";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool final = argc == 3 && value_truthy(args[2]);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr ||
        std::string_view(kwargs[i].name) != "final" || argc == 3) {
      error = "decode() got an invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    final = value_truthy(*kwargs[i].value);
  }
  auto* state = incremental_newline_decoder_state(args[0], error);
  if (state == nullptr) return false;
  Value decoded;
  if (state->decoder.tag == ValueTag::None) {
    value_assign_fast(decoded, args[1]);
  } else {
    Value decode;
    if (!object_get_attr(state->decoder, "decode", decode, error)) return false;
    Value decode_args[2] = {args[1], Value::boolean(final)};
    if (!runtime_call_callable(runtime, decode, decode_args, 2, decoded, error)) return false;
  }
  auto* decoded_text = value_as_string(decoded);
  if (decoded_text == nullptr) {
    error = "decoder should return a string result";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string text = string_object_to_string(*decoded_text);
  if (state->pending_cr && (!text.empty() || final)) {
    text.insert(text.begin(), '\r');
    state->pending_cr = false;
  }
  if (!final && !text.empty() && text.back() == '\r') {
    text.pop_back();
    state->pending_cr = true;
  }
  size_t crlf = 0;
  size_t cr = 0;
  size_t lf = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') {
        ++crlf;
        ++i;
      } else {
        ++cr;
      }
    } else if (text[i] == '\n') {
      ++lf;
    }
  }
  if (lf != 0) state->seen_newlines |= 1;
  if (cr != 0) state->seen_newlines |= 2;
  if (crlf != 0) state->seen_newlines |= 4;
  if (state->translate && (cr != 0 || crlf != 0)) {
    std::string translated;
    translated.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\r') {
        translated.push_back('\n');
        if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
      } else {
        translated.push_back(text[i]);
      }
    }
    text = std::move(translated);
  }
  out = Value::string(std::move(text));
  return true;
}

bool incremental_newline_decoder_decode(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* user_data) {
  return incremental_newline_decoder_decode_kw(
      runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool incremental_newline_decoder_getstate(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  if (argc != 1) {
    error = "getstate() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = incremental_newline_decoder_state(args[0], error);
  if (state == nullptr) return false;
  Value buffer = Value::bytes("");
  int64_t flag = 0;
  if (state->decoder.tag != ValueTag::None) {
    Value method;
    Value decoder_state;
    if (!object_get_attr(state->decoder, "getstate", method, error) ||
        !runtime_call_callable(runtime, method, nullptr, 0, decoder_state, error)) return false;
    auto* tuple = value_as_tuple(decoder_state);
    if (tuple == nullptr || tuple->items.size() != 2 || tuple->items[1].tag != ValueTag::Int64) {
      error = "illegal decoder state";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_assign_fast(buffer, tuple->items[0]);
    flag = tuple->items[1].as.i64;
  }
  out = Value::tuple({std::move(buffer), Value::int64((flag << 1) | (state->pending_cr ? 1 : 0))});
  return true;
}

bool incremental_newline_decoder_setstate(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  auto* tuple = argc == 2 ? value_as_tuple(args[1]) : nullptr;
  if (tuple == nullptr || tuple->items.size() != 2 || tuple->items[1].tag != ValueTag::Int64) {
    error = "setstate() argument must be a two-item tuple";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = incremental_newline_decoder_state(args[0], error);
  if (state == nullptr) return false;
  const int64_t flag = tuple->items[1].as.i64;
  state->pending_cr = (flag & 1) != 0;
  if (state->decoder.tag != ValueTag::None) {
    Value method;
    if (!object_get_attr(state->decoder, "setstate", method, error)) return false;
    Value decoder_state = Value::tuple({tuple->items[0], Value::int64(flag >> 1)});
    if (!runtime_call_callable(runtime, method, &decoder_state, 1, out, error)) return false;
  }
  value_set_none(out);
  return true;
}

bool incremental_newline_decoder_reset(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  if (argc != 1) {
    error = "reset() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = incremental_newline_decoder_state(args[0], error);
  if (state == nullptr) return false;
  state->seen_newlines = 0;
  state->pending_cr = false;
  if (state->decoder.tag != ValueTag::None) {
    Value method;
    if (!object_get_attr(state->decoder, "reset", method, error) ||
        !runtime_call_callable(runtime, method, nullptr, 0, out, error)) return false;
  }
  value_set_none(out);
  return true;
}

bool incremental_newline_decoder_newlines(
    Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  if (argc != 1) {
    error = "newlines getter expected an IncrementalNewlineDecoder";
    return false;
  }
  auto* state = incremental_newline_decoder_state(args[0], error);
  if (state == nullptr) return false;
  std::vector<Value> values;
  if ((state->seen_newlines & 2) != 0) values.push_back(Value::string("\r"));
  if ((state->seen_newlines & 1) != 0) values.push_back(Value::string("\n"));
  if ((state->seen_newlines & 4) != 0) values.push_back(Value::string("\r\n"));
  if (values.empty()) value_set_none(out);
  else if (values.size() == 1) out = std::move(values[0]);
  else out = Value::tuple(std::move(values));
  return true;
}

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

bool raise_stream_state_error(Runtime& runtime, std::string& error) {
  if (error == "invalid memory stream object") error = "uninitialized buffered stream";
  runtime.raise_class_error("ValueError", error);
  return false;
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
  if (auto* instance = value_as_instance(value)) {
    for (const auto& attr : instance->attrs) {
      if (attr.first == "__xlang3_bytes_value__") {
        if (auto* bytearray = value_as_bytearray(attr.second)) {
          out = bytearray->value;
          return true;
        }
        break;
      }
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
  if (!binary) {
    state->newline = "\n";
    state->newline_is_none = false;
  }
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

bool memory_stream_reduce_ex(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 2) {
    error = "memory stream __reduce_ex__() expected a protocol";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) return false;
  Value klass;
  if (!runtime_type_of_value(runtime, args[0], klass)) return false;
  Value stream_state = state->binary
      ? Value::tuple({Value::bytes(state->buffer), Value::int64(static_cast<int64_t>(state->cursor)), Value::none()})
      : Value::tuple({Value::string(state->buffer), state->newline_is_none ? Value::none() : Value::string(state->newline),
                      Value::int64(static_cast<int64_t>(state->cursor)), Value::none()});
  out = Value::tuple({klass, Value::tuple({memory_stream_result(*state, state->buffer)}), std::move(stream_state)});
  return true;
}

bool memory_stream_getstate(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 1) {
    error = "memory stream __getstate__() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = memory_stream_state(args[0], static_cast<const char*>(user_data), error);
  if (state == nullptr) return false;
  out = state->binary
      ? Value::tuple({Value::bytes(state->buffer), Value::int64(static_cast<int64_t>(state->cursor)), Value::none()})
      : Value::tuple({Value::string(state->buffer), state->newline_is_none ? Value::none() : Value::string(state->newline),
                      Value::int64(static_cast<int64_t>(state->cursor)), Value::none()});
  return true;
}

bool memory_stream_setstate(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 2) {
    error = "memory stream __setstate__() expected one state argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  const auto* values = value_as_tuple(args[1]);
  if (state == nullptr || values == nullptr ||
      values->items.size() != (state->binary ? 3u : 4u) ||
      values->items[values->items.size() - 1].tag != ValueTag::None) {
    error = "invalid memory stream state";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string buffer;
  if (!(state->binary ? bytes_value(values->items[0], buffer) : string_value(values->items[0], buffer))) {
    error = "invalid memory stream contents";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  state->buffer = std::move(buffer);
  size_t position_index = 1;
  if (!state->binary) {
    const Value& newline_value = values->items[1];
    const auto* newline = value_as_string(newline_value);
    if (newline_value.tag != ValueTag::None && newline == nullptr) {
      error = "invalid StringIO state";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state->newline_is_none = newline_value.tag == ValueTag::None;
    state->newline = state->newline_is_none ? "" : string_object_to_string(*newline);
    position_index = 2;
  }
  int64_t position = 0;
  if (!value_int_like_to_i64(values->items[position_index], position) || position < 0) {
    error = "invalid memory stream position";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  state->cursor = static_cast<size_t>(position);
  value_set_none(out);
  return true;
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
  Value tell;
  Value position;
  std::string ignored;
  if (attribute_get(buffer, "tell", tell, ignored)) {
    if (runtime_call_callable(runtime, tell, nullptr, 0, position, ignored)) {
      if (position.tag == ValueTag::Int64 && position.as.i64 > 0) {
        state->text_encoder_started = true;
      }
    } else {
      Value discarded;
      (void)runtime.take_pending_exception(discarded);
      ignored.clear();
    }
  }
  Value mode;
  bool have_mode = attribute_get(buffer, "mode", mode, ignored);
  if (!have_mode) {
    if (const Value* raw = buffered_raw_value(buffer)) {
      have_mode = attribute_get(*raw, "mode", mode, ignored);
    }
  }
  if (have_mode) {
    if (auto* mode_text = value_as_string(mode)) {
      const std::string mode_value = string_object_to_string(*mode_text);
      if (mode_value.find('a') != std::string::npos) {
        state->text_encoder_started = true;
      }
    }
  }
  if (!instance_set_native_data(self, "_io.TextIOWrapper", state, memory_stream_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool text_io_wrapper_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return text_io_wrapper_init_kw(
      runtime, args, argc, nullptr, 0, out, error, nullptr);
}

bool text_io_wrapper_new(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || value_as_class(args[0]) == nullptr) {
    error = "_io.TextIOWrapper.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::instance(args[0]);
  if (argc == 1) return true;
  if (auto* klass = value_as_class(args[0]);
      klass != nullptr && klass->name != "TextIOWrapper") {
    Value init;
    if (!attribute_get(out, "__init__", init, error)) return false;
    Value ignored;
    return runtime_call_callable(
        runtime, init, args + 1, argc - 1, ignored, error);
  }
  std::vector<Value> init_args(argc);
  value_assign_fast(init_args[0], out);
  for (uint32_t index = 1; index < argc; ++index) {
    value_assign_fast(init_args[index], args[index]);
  }
  Value ignored;
  return text_io_wrapper_init_kw(
      runtime, init_args.data(), argc, nullptr, 0, ignored, error, nullptr);
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
  if (argc < 2 || argc > 7) {
    error = "_io.TextIOWrapper() missing required buffer argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (auto* existing = static_cast<MemoryStreamState*>(
          instance_get_native_data(args[0], "_io.TextIOWrapper"))) {
    existing->closed = true;
  }
  const Value* encoding_value = argc > 2 ? &args[2] : nullptr;
  const Value* errors_value = argc > 3 ? &args[3] : nullptr;
  const Value* newline_value = argc > 4 ? &args[4] : nullptr;
  for (uint32_t index = 0; index < kwargc; ++index) {
    const std::string_view name = kwargs[index].name == nullptr
        ? std::string_view() : std::string_view(kwargs[index].name);
    if (name == "encoding") encoding_value = kwargs[index].value;
    else if (name == "errors") errors_value = kwargs[index].value;
    else if (name == "newline") newline_value = kwargs[index].value;
    else if (name != "line_buffering" && name != "write_through") {
      error = "TextIOWrapper() got an unexpected keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (encoding_value != nullptr && encoding_value->tag != ValueTag::None &&
      value_as_string(*encoding_value) == nullptr) {
    error = "encoding must be str or None";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (errors_value != nullptr && errors_value->tag != ValueTag::None &&
      value_as_string(*errors_value) == nullptr) {
    error = "errors must be str or None";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (const auto* option : {encoding_value, errors_value}) {
    if (option == nullptr || option->tag == ValueTag::None) continue;
    const std::string candidate = string_object_to_string(*value_as_string(*option));
    if (text_contains_utf8_surrogate(candidate)) {
      error = "surrogates not allowed in codec name";
      runtime.raise_class_error("UnicodeEncodeError", error);
      return false;
    }
    if (candidate.find('\0') != std::string::npos) {
      error = "embedded null character";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  }
  if (newline_value != nullptr && newline_value->tag != ValueTag::None) {
    auto* newline_text = value_as_string(*newline_value);
    if (newline_text == nullptr) {
      error = "newline must be str or None";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const std::string candidate = string_object_to_string(*newline_text);
    if (text_contains_utf8_surrogate(candidate) ||
        candidate.find('\0') != std::string::npos) {
      error = "illegal newline value";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (candidate != "" && candidate != "\n" && candidate != "\r" &&
        candidate != "\r\n") {
      error = "illegal newline value";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  }
  std::string newline;
  bool newline_is_none = true;
  text_io_newline_from_args(args, argc, kwargs, kwargc, newline, newline_is_none);
  std::string encoding = text_io_option_from_args(
      args, argc, 2, kwargs, kwargc, "encoding", "UTF-8");
  if (encoding == "locale") encoding = "utf-8";
  std::string normalized_encoding = encoding;
  std::transform(
      normalized_encoding.begin(), normalized_encoding.end(),
      normalized_encoding.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  std::replace(normalized_encoding.begin(), normalized_encoding.end(), '-', '_');
  if (normalized_encoding == "hex" || normalized_encoding == "hex_codec") {
    error = "'" + encoding + "' is not a text encoding; use codecs.encode() to handle arbitrary codecs";
    runtime.raise_class_error("LookupError", error);
    return false;
  }
  std::string errors = text_io_option_from_args(
      args, argc, 3, kwargs, kwargc, "errors", "strict");
  Value codecs;
  if (!runtime.import_module("codecs", codecs, error)) return false;
  const std::pair<const char*, std::string> checks[] = {
      {"lookup", encoding}, {"lookup_error", errors}};
  for (const auto& [method_name, value] : checks) {
    Value method;
    if (!module_get_attr(codecs, method_name, method, error)) return false;
    Value argument = Value::string(value);
    Value ignored;
    if (!runtime_call_callable(runtime, method, &argument, 1, ignored, error)) {
      return false;
    }
  }
  return text_io_wrapper_load_buffer(runtime, args[0], args[1],
      std::move(encoding), std::move(errors), std::move(newline),
      newline_is_none,
      text_io_flag_from_args(args, argc, 5, kwargs, kwargc, "line_buffering"),
      text_io_flag_from_args(args, argc, 6, kwargs, kwargc, "write_through"),
      out, error);
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
  if (argc < 1 || value_as_class(args[0]) == nullptr) {
    error = "_io.TextIOWrapper.__new__ first argument must be a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::instance(args[0]);
  if (argc == 1) return true;
  if (auto* klass = value_as_class(args[0]);
      klass != nullptr && klass->name != "TextIOWrapper") {
    Value init;
    if (!attribute_get(out, "__init__", init, error)) return false;
    std::vector<std::pair<std::string, Value>> keyword_values;
    keyword_values.reserve(kwargc);
    for (uint32_t index = 0; index < kwargc; ++index) {
      if (kwargs[index].name != nullptr && kwargs[index].value != nullptr) {
        keyword_values.emplace_back(kwargs[index].name, *kwargs[index].value);
      }
    }
    Value ignored;
    return runtime_call_callable_kw(
        runtime, init, args + 1, argc - 1, keyword_values, ignored, error);
  }
  std::vector<Value> init_args(argc);
  value_assign_fast(init_args[0], out);
  for (uint32_t index = 1; index < argc; ++index) {
    value_assign_fast(init_args[index], args[index]);
  }
  Value ignored;
  return text_io_wrapper_init_kw(
      runtime, init_args.data(), argc, kwargs, kwargc, ignored, error,
      user_data);
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

bool buffered_stream_load_buffer(Runtime& runtime, const Value& self, const Value& buffer, const char* type,
                                 Value& out, std::string& error, size_t buffer_size = 8192) {
  auto* state = new MemoryStreamState();
  state->binary = true;
  state->wraps_buffer = true;
  state->wrapped_buffer = buffer;
  state->buffer_size = buffer_size;
  state->random_access = std::string_view(type) == "_io.BufferedRandom";
  if (!instance_set_native_data(self, type, state, memory_stream_cleanup, error)) {
    delete state;
    return false;
  }
  io_set_instance_attr(self, "__xlang3_io_closed", Value::boolean(false));
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
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 3) {
    int64_t size = 0;
    if (!value_int_like_to_i64(args[2], size)) {
      error = "buffer size must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (size <= 0) {
      error = "buffer size must be strictly positive";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedReader", out, error,
                                       static_cast<size_t>(size));
  }
  return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedReader", out, error);
}

bool buffered_writer_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "_io.BufferedWriter() expected raw stream and optional buffer size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 3) {
    int64_t size = 0;
    if (!value_int_like_to_i64(args[2], size)) {
      error = "buffer size must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (size <= 0) {
      error = "buffer size must be strictly positive";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedWriter", out, error,
                                       static_cast<size_t>(size));
  }
  return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedWriter", out, error);
}

bool buffered_random_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "_io.BufferedRandom() expected raw stream and optional buffer size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 3) {
    int64_t size = 0;
    if (!value_int_like_to_i64(args[2], size)) {
      error = "buffer size must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (size <= 0) {
      error = "buffer size must be strictly positive";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedRandom", out, error,
                                       static_cast<size_t>(size));
  }
  return buffered_stream_load_buffer(runtime, args[0], args[1], "_io.BufferedRandom", out, error);
}

bool buffered_rw_pair_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return buffered_stream_init_kw(
      runtime, args, argc, nullptr, 0, out, error,
      const_cast<char*>("_io.BufferedRWPair"));
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
    size_t start = 0;
    if (state.text_pending_cr) {
      if (!text.empty() && text.front() == '\n') start = 1;
      state.text_pending_cr = false;
    }
    for (size_t i = start; i < text.size(); ++i) {
      if (text[i] == '\r') {
        if (i + 1 < text.size() && text[i + 1] == '\n') {
          ++i;
        } else if (i + 1 == text.size()) {
          state.text_pending_cr = true;
        }
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
  std::string decode_encoding = state.encoding;
  std::string normalized_encoding = state.encoding;
  std::transform(
      normalized_encoding.begin(), normalized_encoding.end(),
      normalized_encoding.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  std::replace(normalized_encoding.begin(), normalized_encoding.end(), '-', '_');
  if ((normalized_encoding == "utf_16" || normalized_encoding == "utf_32") &&
      state.text_decoder_started && !state.text_decode_encoding.empty()) {
    decode_encoding = state.text_decode_encoding;
  } else if (normalized_encoding == "utf_16" && bytes.size() >= 2) {
    state.text_decode_encoding =
        static_cast<unsigned char>(bytes[0]) == 0xfeu ? "utf-16-be" : "utf-16-le";
  } else if (normalized_encoding == "utf_32" && bytes.size() >= 4) {
    state.text_decode_encoding =
        static_cast<unsigned char>(bytes[0]) == 0x00u ? "utf-32-be" : "utf-32-le";
  }
  Value encoded = Value::bytes(std::move(bytes));
  if (state.encoding == "quopri" || state.encoding == "quopri_codec") {
    Value codecs;
    Value decode_function;
    if (!runtime.import_module("codecs", codecs, error) ||
        !module_get_attr(codecs, "decode", decode_function, error)) return false;
    Value codec_args[] = {encoded, Value::string(state.encoding)};
    Value decoded;
    if (!runtime_call_callable(
            runtime, decode_function, codec_args, 2, decoded, error)) return false;
    if (value_as_string(decoded) == nullptr) {
      error = "TextIOWrapper decoder should return str";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_assign_fast(out, decoded);
    return true;
  }
  Value decode;
  if (!attribute_get(encoded, "decode", decode, error)) {
    return false;
  }
  Value decode_args[] = {Value::string(decode_encoding), Value::string(state.errors)};
  Value decoded;
  if (!runtime_call_callable(runtime, decode, decode_args, 2, decoded, error)) {
    return false;
  }
  state.text_decoder_started = true;
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

bool buffered_reader_read(
    Runtime& runtime, MemoryStreamState& state, int64_t requested,
    bool single_raw_read, Value& out, std::string& error,
    bool return_pending_immediately = true) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (state.random_access &&
      !buffered_random_prepare_read(runtime, state, error)) return false;
  if (requested < -1) {
    error = "read length must be non-negative or -1";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (requested == 0) {
    out = Value::bytes("");
    return true;
  }
  Value readinto;
  if (!attribute_get(state.wrapped_buffer, "readinto", readinto, error)) return false;
  std::string result;
  const auto consume_pending = [&](size_t limit) {
    const size_t available = state.cursor >= state.buffer.size() ? 0 : state.buffer.size() - state.cursor;
    const size_t count = std::min(available, limit);
    if (count != 0) result.append(state.buffer.data() + state.cursor, count);
    state.cursor += count;
    if (state.cursor == state.buffer.size()) {
      state.buffer.clear();
      state.cursor = 0;
    }
  };
  if (!state.buffer.empty()) {
    consume_pending(requested < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(requested));
    if ((single_raw_read && return_pending_immediately) ||
        (requested >= 0 && result.size() == static_cast<size_t>(requested))) {
      out = Value::bytes(std::move(result));
      return true;
    }
  }
  bool attempted_raw_read = false;
  for (;;) {
    if (single_raw_read && attempted_raw_read) break;
    if (requested >= 0 && result.size() >= static_cast<size_t>(requested)) break;
    attempted_raw_read = true;
    Value buffer = Value::bytearray(std::string(state.buffer_size, '\0'));
    Value count_value;
    if (!runtime_call_callable(runtime, readinto, &buffer, 1, count_value, error)) return false;
    if (count_value.tag == ValueTag::None) {
      if (result.empty()) value_set_none(out);
      else out = Value::bytes(std::move(result));
      return true;
    }
    if (count_value.tag != ValueTag::Int64) {
      error = "raw readinto() returned non-int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    auto* bytes = value_as_bytearray(buffer);
    const int64_t count = count_value.as.i64;
    if (bytes == nullptr || count < 0 || static_cast<size_t>(count) > bytes->value.size()) {
      error = "raw readinto() returned invalid length";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    if (count == 0) break;
    state.buffer.assign(bytes->value.data(), static_cast<size_t>(count));
    state.cursor = 0;
    const size_t need = requested < 0 ? static_cast<size_t>(-1)
                                      : static_cast<size_t>(requested) - result.size();
    consume_pending(need);
  }
  out = Value::bytes(std::move(result));
  return true;
}

bool stream_read(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "memory stream read() expected optional size";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  if (std::string_view(type) == "_io.BufferedWriter") {
    return io_base_unsupported(
        runtime, args, argc, out, error, const_cast<char*>("read"));
  }
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return raise_stream_state_error(runtime, error);
  }
  memory_stream_sync_exported_buffer(*state);
  if (state->wraps_buffer) {
    if (state->binary) {
      if (std::string_view(type) == "_io.BufferedRandom" &&
          !buffered_random_prepare_read(runtime, *state, error)) return false;
      int64_t requested = -1;
      if (argc == 2 && args[1].tag != ValueTag::None &&
          !value_int_like_to_i64(args[1], requested)) {
        error = "read size must be an integer or None";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      return buffered_reader_read(runtime, *state, requested, false, out, error);
    }
    if (!state->binary) state->text_decoded_read = true;
    Value readable_method;
    std::string capability_error;
    if (attribute_get(state->wrapped_buffer, "readable", readable_method, capability_error)) {
      Value readable;
      if (!runtime_call_callable(
              runtime, readable_method, nullptr, 0, readable, error)) return false;
      if (!value_truthy(readable)) {
        error = "not readable";
        runtime.raise_class_error("OSError", error);
        return false;
      }
    }
    if (!state->text_read_ahead.empty()) {
      const int64_t requested = argc == 2 && args[1].tag == ValueTag::Int64
          ? args[1].as.i64 : -1;
      size_t take_characters = utf8_codepoint_count(state->text_read_ahead);
      if (requested >= 0) {
        take_characters = std::min(
            take_characters, static_cast<size_t>(requested));
      }
      size_t take_bytes = 0;
      for (size_t index = 0;
           index < take_characters && take_bytes < state->text_read_ahead.size();
           ++index) {
        size_t width = utf8_codepoint_width(static_cast<unsigned char>(
            state->text_read_ahead[take_bytes]));
        if (width == 0 || take_bytes + width > state->text_read_ahead.size()) width = 1;
        take_bytes += width;
      }
      std::string prefix = state->text_read_ahead.substr(0, take_bytes);
      state->text_read_ahead.erase(0, take_bytes);
      if (requested >= 0 && take_characters == static_cast<size_t>(requested)) {
        out = Value::string(std::move(prefix));
        return true;
      }
      Value remainder_args[] = {
          args[0], requested < 0 ? Value::int64(-1) :
              Value::int64(requested - static_cast<int64_t>(take_characters))};
      Value remainder;
      if (!stream_read(
              runtime, remainder_args, 2, remainder, error, user_data)) return false;
      if (auto* remainder_text = value_as_string(remainder)) {
        prefix += string_object_to_string(*remainder_text);
      }
      out = Value::string(std::move(prefix));
      return true;
    }
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
    Value adjusted_read_size;
    const Value* read_args = argc == 2 ? &args[1] : nullptr;
    const uint32_t read_argc = argc == 2 ? 1 : 0;
    std::string normalized_read_encoding = state->encoding;
    std::transform(
        normalized_read_encoding.begin(), normalized_read_encoding.end(),
        normalized_read_encoding.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::replace(
        normalized_read_encoding.begin(), normalized_read_encoding.end(), '-', '_');
    if (argc == 2 && args[1].tag == ValueTag::Int64 && args[1].as.i64 >= 0) {
      int64_t unit = 1;
      if (normalized_read_encoding == "utf_16" ||
          normalized_read_encoding == "utf_16_le" ||
          normalized_read_encoding == "utf_16_be") unit = 2;
      else if (normalized_read_encoding == "utf_32" ||
               normalized_read_encoding == "utf_32_le" ||
               normalized_read_encoding == "utf_32_be") unit = 4;
      if (unit != 1) {
        int64_t byte_count = args[1].as.i64 * unit;
        if (!state->text_decoder_started &&
            (normalized_read_encoding == "utf_16" ||
             normalized_read_encoding == "utf_32")) byte_count += unit;
        adjusted_read_size = Value::int64(byte_count);
        read_args = &adjusted_read_size;
      }
    }
    if (!runtime_call_callable(runtime, read_method, read_args, read_argc, data, error)) {
      return false;
    }
    if (state->encoding == "utf-8" || state->encoding == "UTF-8" ||
        state->encoding == "utf8" || state->encoding == "UTF8") {
      std::string bytes;
      if (bytes_value(data, bytes) && !bytes.empty()) {
        size_t offset = 0;
        while (offset < bytes.size()) {
          size_t width = utf8_codepoint_width(
              static_cast<unsigned char>(bytes[offset]));
          if (width == 0) width = 1;
          if (offset + width <= bytes.size()) {
            offset += width;
            continue;
          }
          const size_t missing = offset + width - bytes.size();
          Value extra_size = Value::int64(static_cast<int64_t>(missing));
          Value extra;
          if (!runtime_call_callable(
                  runtime, read_method, &extra_size, 1, extra, error)) return false;
          std::string extra_bytes;
          if (!bytes_value(extra, extra_bytes)) {
            error = "underlying read() should have returned a bytes-like object";
            runtime.raise_class_error("TypeError", error);
            return false;
          }
          bytes += extra_bytes;
          if (extra_bytes.empty()) break;
        }
        data = Value::bytes(std::move(bytes));
      }
    }
    if (value_as_string(data) != nullptr) {
      error = "underlying read() should have returned a bytes-like object";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (state->binary) {
      value_assign_fast(out, data);
      return true;
    }
    if (!decode_text_io_data(runtime, data, *state, out, error)) return false;
    if (state->text_pending_cr &&
        (state->encoding == "ascii" || state->encoding == "ASCII" ||
         state->encoding == "utf-8" || state->encoding == "UTF-8" ||
         state->encoding == "utf8" || state->encoding == "UTF8")) {
      Value tell_method;
      Value saved_position;
      std::string ignored;
      const bool can_rewind = attribute_get(
          state->wrapped_buffer, "tell", tell_method, ignored) &&
          runtime_call_callable(
              runtime, tell_method, nullptr, 0, saved_position, ignored) &&
          saved_position.tag == ValueTag::Int64;
      Value one = Value::int64(1);
      Value lookahead;
      if (runtime_call_callable(runtime, read_method, &one, 1, lookahead, error)) {
        std::string lookahead_bytes;
        if (bytes_value(lookahead, lookahead_bytes) && lookahead_bytes == "\n") {
          state->text_pending_cr = false;
        } else if (!lookahead_bytes.empty() && can_rewind) {
          Value seek_method;
          if (attribute_get(state->wrapped_buffer, "seek", seek_method, error)) {
            Value seek_args[] = {saved_position, Value::int64(0)};
            Value seek_result;
            if (!runtime_call_callable(
                    runtime, seek_method, seek_args, 2, seek_result, error)) return false;
          }
          state->text_pending_cr = false;
        } else if (lookahead_bytes.empty()) {
          state->text_pending_cr = false;
        }
      } else {
        return false;
      }
    }
    auto* decoded = value_as_string(out);
    std::string raw_bytes;
    if (decoded != nullptr && string_object_view(*decoded).empty() &&
        bytes_value(data, raw_bytes) && !raw_bytes.empty()) {
      return stream_read(runtime, args, argc, out, error, user_data);
    }
    if (decoded != nullptr && argc == 2 && args[1].tag == ValueTag::Int64 &&
        args[1].as.i64 >= 0) {
      const size_t requested = static_cast<size_t>(args[1].as.i64);
      std::string result = string_object_to_string(*decoded);
      size_t count = utf8_codepoint_count(result);
      const auto prefix_bytes = [](std::string_view text, size_t characters) {
        size_t offset = 0;
        for (size_t index = 0; index < characters && offset < text.size(); ++index) {
          size_t width = utf8_codepoint_width(
              static_cast<unsigned char>(text[offset]));
          if (width == 0 || offset + width > text.size()) width = 1;
          offset += width;
        }
        return offset;
      };
      if (count > requested) {
        const size_t split = prefix_bytes(result, requested);
        state->text_read_ahead = result.substr(split) + state->text_read_ahead;
        result.resize(split);
      } else if (count < requested && !result.empty()) {
        Value more_args[] = {
            args[0], Value::int64(static_cast<int64_t>(requested - count))};
        Value more;
        if (!stream_read(runtime, more_args, 2, more, error, user_data)) return false;
        if (auto* more_text = value_as_string(more)) {
          result += string_object_to_string(*more_text);
        }
      }
      out = Value::string(std::move(result));
    }
    return true;
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
  if (state == nullptr) return raise_stream_state_error(runtime, error);
  memory_stream_sync_exported_buffer(*state);
  char* destination = nullptr;
  size_t capacity = 0;
  Value exported_buffer;
  if (auto* bytearray = value_as_bytearray(args[1])) {
    destination = bytearray->value.data();
    capacity = bytearray->value.size();
  } else if (auto* view = value_as_memoryview(args[1]); view != nullptr && !view->readonly) {
    destination = memoryview_object_writable_data(*view);
    capacity = view->size;
  } else if (std::string ignored;
             object_get_attr(args[1], "__xlang3_bytes_value__", exported_buffer, ignored)) {
    if (auto* bytearray = value_as_bytearray(exported_buffer)) {
      destination = bytearray->value.data();
      capacity = bytearray->value.size();
    }
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
  if (state == nullptr) return raise_stream_state_error(runtime, error);
  if (!state->wraps_buffer) return false;
  if (state->random_access &&
      !buffered_random_prepare_read(runtime, *state, error)) return false;
  int64_t size = 8192;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "BufferedReader.peek() size must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (args[1].as.i64 > 0) size = args[1].as.i64;
  }
  const size_t available = state->cursor >= state->buffer.size() ? 0 : state->buffer.size() - state->cursor;
  if (available == 0) {
    Value ignored;
    if (!buffered_reader_read(runtime, *state, static_cast<int64_t>(state->buffer_size), true, ignored, error)) return false;
    if (ignored.tag == ValueTag::None) {
      out = Value::bytes("");
      return true;
    }
    std::string first;
    if (!bytes_value(ignored, first)) return false;
    if (!first.empty()) {
      state->buffer = std::move(first);
      state->cursor = 0;
    }
  }
  const size_t now_available = state->cursor >= state->buffer.size() ? 0 : state->buffer.size() - state->cursor;
  out = Value::bytes(state->buffer.substr(state->cursor, std::min(now_available, static_cast<size_t>(size))));
  return true;
}

bool buffered_stream_readinto1(Runtime& runtime, const Value* args, uint32_t argc,
                               Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "readinto1() expected one buffer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = memory_stream_state(args[0], static_cast<const char*>(user_data), error);
  if (state == nullptr) return raise_stream_state_error(runtime, error);
  char* destination = nullptr;
  size_t capacity = 0;
  Value exported;
  if (auto* bytearray = value_as_bytearray(args[1])) {
    destination = bytearray->value.data();
    capacity = bytearray->value.size();
  } else if (auto* view = value_as_memoryview(args[1]); view != nullptr && !view->readonly) {
    destination = memoryview_object_writable_data(*view);
    capacity = view->size;
  } else if (std::string ignored;
             object_get_attr(args[1], "__xlang3_bytes_value__", exported, ignored)) {
    if (auto* bytearray = value_as_bytearray(exported)) {
      destination = bytearray->value.data();
      capacity = bytearray->value.size();
    }
  }
  if (destination == nullptr && capacity != 0) {
    error = "readinto1() argument must be read-write bytes-like object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value data;
  if (!buffered_reader_read(runtime, *state, static_cast<int64_t>(capacity), true,
                            data, error, capacity <= state->buffer_size)) return false;
  if (data.tag == ValueTag::None) {
    value_set_none(out);
    return true;
  }
  std::string bytes;
  if (!bytes_value(data, bytes)) return false;
  if (!bytes.empty()) std::memcpy(destination, bytes.data(), bytes.size());
  out = Value::int64(static_cast<int64_t>(bytes.size()));
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
    if (error == "I/O operation on closed file") runtime.raise_class_error("ValueError", error);
    return false;
  }
  memory_stream_sync_exported_buffer(*state);
  if (state->wraps_buffer) {
    if (!state->binary) state->text_decoded_read = true;
    if (!state->binary &&
        (state->encoding == "ascii" || state->encoding == "ASCII" ||
         state->encoding == "utf-8" || state->encoding == "UTF-8" ||
         state->encoding == "utf8" || state->encoding == "UTF8" ||
         state->encoding == "latin-1" || state->encoding == "latin1" ||
         state->encoding == "utf-16" || state->encoding == "utf-16-le" ||
         state->encoding == "utf-16-be" || state->encoding == "utf-32" ||
         state->encoding == "utf-32-le" || state->encoding == "utf-32-be")) {
      const int64_t limit = argc == 2 && args[1].tag == ValueTag::Int64
          ? args[1].as.i64 : -1;
      std::string line;
      while (limit < 0 || static_cast<int64_t>(utf8_codepoint_count(line)) < limit) {
        Value read_args[] = {args[0], Value::int64(1)};
        Value character;
        if (!stream_read(runtime, read_args, 2, character, error, user_data)) return false;
        auto* text = value_as_string(character);
        if (text == nullptr) {
          error = "underlying read() should have returned a bytes-like object";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        const std::string piece = string_object_to_string(*text);
        if (piece.empty()) break;
        line += piece;
        bool complete = state->newline_is_none
            ? piece == "\n"
            : state->newline == "\n" ? piece == "\n"
            : state->newline == "\r" ? piece == "\r"
            : state->newline == "\r\n" ? line.size() >= 2 &&
                line.compare(line.size() - 2, 2, "\r\n") == 0
            : piece == "\n" || piece == "\r";
        if (complete && !state->newline_is_none && state->newline.empty() &&
            piece == "\r" &&
            (limit < 0 || static_cast<int64_t>(utf8_codepoint_count(line)) < limit)) {
          Value next;
          if (!stream_read(runtime, read_args, 2, next, error, user_data)) return false;
          if (auto* next_text = value_as_string(next)) {
            const std::string next_piece = string_object_to_string(*next_text);
            if (next_piece == "\n") line += next_piece;
            else state->text_read_ahead = next_piece + state->text_read_ahead;
          }
        }
        if (complete) break;
      }
      out = Value::string(std::move(line));
      return true;
    }
    Value read_method;
    std::string readline_error;
    if (state->binary) {
      const int64_t limit = argc == 2 && args[1].tag == ValueTag::Int64 ? args[1].as.i64 : -1;
      std::string line;
      while (limit < 0 || static_cast<int64_t>(line.size()) < limit) {
        Value byte;
        if (!buffered_reader_read(runtime, *state, 1, false, byte, error)) {
          Value cause;
          runtime.take_pending_exception(cause);
          if (auto* cause_instance = value_as_instance(cause)) {
            auto* cause_class = value_as_class(cause_instance->klass);
            if (cause_class != nullptr &&
                (cause_class->name == "OSError" ||
                 class_has_builtin_base_name(cause_class, "OSError"))) {
              runtime.set_pending_exception(std::move(cause));
              return false;
            }
          }
          error = "raw readinto() returned invalid result";
          Value outer = runtime.make_exception("OSError", error);
          if (cause.tag != ValueTag::Invalid) {
            std::string cause_error;
            object_set_attr(outer, "__cause__", cause, cause_error);
          }
          runtime.set_pending_exception(std::move(outer));
          return false;
        }
        if (byte.tag == ValueTag::None) {
          if (line.empty()) value_set_none(out);
          else out = Value::bytes(std::move(line));
          return true;
        }
        std::string bytes;
        if (!bytes_value(byte, bytes) || bytes.empty()) {
          out = Value::bytes(std::move(line));
          return true;
        }
        line.push_back(bytes[0]);
        if (bytes[0] == '\n') {
          out = Value::bytes(std::move(line));
          return true;
        }
      }
      out = Value::bytes(std::move(line));
      return true;
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
    int64_t hint = -1;
    if (argc == 2 && args[1].tag != ValueTag::None &&
        !value_int_like_to_i64(args[1], hint)) {
      error = "readlines hint must be an integer or None";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    size_t total = 0;
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
      if (auto* text = value_as_string(line)) total += string_object_view(*text).size();
      else if (auto* bytes = value_as_bytes(line)) total += bytes_object_view(*bytes).size();
      lines.push_back(std::move(line));
      if (hint > 0 && total > static_cast<size_t>(hint)) break;
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

bool buffered_writer_flush_pending(
    Runtime& runtime, MemoryStreamState& state, const Value& target,
    std::string& error, bool* blocked = nullptr) {
  if (blocked != nullptr) *blocked = false;
  Value write;
  if (!attribute_get(target, "write", write, error)) return false;
  while (!state.buffer.empty()) {
    Value chunk = Value::bytes(state.buffer);
    Value written;
    if (!runtime_call_callable(runtime, write, &chunk, 1, written, error)) return false;
    if (written.tag == ValueTag::None) {
      if (blocked != nullptr) *blocked = true;
      return true;
    }
    if (written.tag != ValueTag::Int64 || written.as.i64 < 0 ||
        static_cast<size_t>(written.as.i64) > state.buffer.size()) {
      error = "raw write() returned invalid length";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    if (written.as.i64 == 0) {
      if (blocked != nullptr) *blocked = true;
      return true;
    }
    state.buffer.erase(0, static_cast<size_t>(written.as.i64));
  }
  return true;
}

bool buffered_random_rewind_read(
    Runtime& runtime, MemoryStreamState& state, std::string& error) {
  if (state.random_writing || state.buffer.empty()) return true;
  const size_t pending = state.cursor >= state.buffer.size()
      ? 0 : state.buffer.size() - state.cursor;
  if (pending != 0) {
    Value seek;
    if (!attribute_get(state.wrapped_buffer, "seek", seek, error)) return false;
    Value seek_args[] = {
        Value::int64(-static_cast<int64_t>(pending)), Value::int64(1)};
    Value position;
    if (!runtime_call_callable(runtime, seek, seek_args, 2, position, error)) return false;
  }
  state.buffer.clear();
  state.cursor = 0;
  return true;
}

bool buffered_random_prepare_read(
    Runtime& runtime, MemoryStreamState& state, std::string& error) {
  if (!state.random_writing) return true;
  if (!buffered_writer_flush_pending(
          runtime, state, state.wrapped_buffer, error)) return false;
  state.random_writing = false;
  state.cursor = 0;
  return true;
}

bool buffered_random_prepare_write(
    Runtime& runtime, MemoryStreamState& state, std::string& error) {
  if (state.random_writing) return true;
  if (!buffered_random_rewind_read(runtime, state, error)) return false;
  state.random_writing = true;
  return true;
}

bool stream_write(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "memory stream write() expected data";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  if (std::string_view(type) == "_io.BufferedReader") {
    return io_base_unsupported(
        runtime, args, argc, out, error, const_cast<char*>("write"));
  }
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return raise_stream_state_error(runtime, error);
  }
  if (state->wraps_buffer && !state->binary) {
    std::string data;
    if (!string_value(args[1], data)) {
      error = "TextIOWrapper.write() argument must be str";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const int64_t written = static_cast<int64_t>(utf8_codepoint_count(data));
    if (!state->text_encoder_started) {
      bool position_is_meaningful = true;
      Value seekable_method;
      Value seekable;
      Value tell_method;
      Value position;
      std::string ignored;
      if (attribute_get(state->wrapped_buffer, "seekable", seekable_method, ignored) &&
          runtime_call_callable(
              runtime, seekable_method, nullptr, 0, seekable, ignored)) {
        position_is_meaningful = value_truthy(seekable);
      }
      if (position_is_meaningful &&
          attribute_get(state->wrapped_buffer, "tell", tell_method, ignored) &&
          runtime_call_callable(runtime, tell_method, nullptr, 0, position, ignored) &&
          position.tag == ValueTag::Int64 && position.as.i64 > 0) {
        state->text_encoder_started = true;
      }
    }
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
    Value encode_args[] = {Value::string(state->encoding), Value::string(state->errors)};
    Value bytes_arg;
    if (state->encoding == "rot13" || state->encoding == "rot_13") {
      Value codecs;
      if (!runtime.import_module("codecs", codecs, error) ||
          !module_get_attr(codecs, "encode", encode, error)) return false;
      Value codec_args[] = {text, Value::string(state->encoding)};
      if (!runtime_call_callable(
              runtime, encode, codec_args, 2, bytes_arg, error)) return false;
    } else {
      if (!attribute_get(text, "encode", encode, error)) return false;
      if (!runtime_call_callable(runtime, encode, encode_args, 2, bytes_arg, error)) {
        return false;
      }
    }
    if (state->text_encoder_started) {
      if (auto* encoded = value_as_bytes(bytes_arg)) {
        std::string bytes = bytes_object_to_string(*encoded);
        size_t bom_size = 0;
        if (bytes.size() >= 3 &&
            bytes.compare(0, 3, "\xEF\xBB\xBF", 3) == 0) bom_size = 3;
        else if (bytes.size() >= 4 &&
                 (bytes.compare(0, 4, "\xFF\xFE\x00\x00", 4) == 0 ||
                  bytes.compare(0, 4, "\x00\x00\xFE\xFF", 4) == 0)) bom_size = 4;
        else if (bytes.size() >= 2 &&
                 (bytes.compare(0, 2, "\xFF\xFE", 2) == 0 ||
                  bytes.compare(0, 2, "\xFE\xFF", 2) == 0)) bom_size = 2;
        if (bom_size != 0) bytes_arg = Value::bytes(bytes.substr(bom_size));
      }
    }
    if (written != 0) state->text_encoder_started = true;
    std::string encoded_bytes;
    if (!bytes_value(bytes_arg, encoded_bytes)) {
      error = "TextIOWrapper encoder must return bytes";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    size_t chunk_size = state->buffer_size;
    Value configured_chunk;
    std::string chunk_error;
    if (object_get_attr(args[0], "_CHUNK_SIZE", configured_chunk, chunk_error) &&
        configured_chunk.tag == ValueTag::Int64 && configured_chunk.as.i64 > 0) {
      chunk_size = static_cast<size_t>(configured_chunk.as.i64);
    }
    if (!state->write_through && !flush_line && encoded_bytes.size() < chunk_size &&
        state->buffer.size() + encoded_bytes.size() <= chunk_size) {
      state->buffer += encoded_bytes;
      value_set_int64(out, written);
      return true;
    }
    if (!state->buffer.empty() && !text_io_flush_pending(runtime, *state, error)) {
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
    const Value& write_target = std::string_view(type) == "_io.BufferedRWPair"
        ? state->wrapped_writer : state->wrapped_buffer;
    if (!attribute_get(write_target, "write", write_method, error)) {
      return false;
    }
    if (state->binary) {
      std::string bytes;
      if (!bytes_value(args[1], bytes)) {
        error = "BufferedIOBase.write() argument must be bytes-like";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      if (std::string_view(type) == "_io.BufferedWriter" ||
          std::string_view(type) == "_io.BufferedRandom") {
        std::lock_guard<std::recursive_mutex> lock(state->mutex);
        if (state->closed) {
          error = "I/O operation on closed file";
          runtime.raise_class_error("ValueError", error);
          return false;
        }
        if (std::string_view(type) == "_io.BufferedRandom" &&
            !buffered_random_prepare_write(runtime, *state, error)) return false;
        const size_t accepted = bytes.size();
        state->buffer.append(bytes);
        if (state->buffer.size() >= state->buffer_size) {
          bool blocked = false;
          if (!buffered_writer_flush_pending(
                  runtime, *state, write_target, error, &blocked)) return false;
          if (blocked && state->buffer.size() > state->buffer_size) {
            const size_t lost = state->buffer.size() - state->buffer_size;
            state->buffer.resize(state->buffer_size);
            const size_t accepted_before_block = accepted > lost ? accepted - lost : 0;
            error = "write could not complete without blocking";
            if (const Value* exception_class = runtime.find_builtin("BlockingIOError")) {
              Value exception = runtime.make_exception_from_class(*exception_class, error);
              std::string ignored;
              object_set_attr(
                  exception, "characters_written",
                  Value::int64(static_cast<int64_t>(accepted_before_block)), ignored);
              runtime.set_pending_exception(std::move(exception));
            } else {
              runtime.raise_class_error("OSError", error);
            }
            return false;
          }
        }
        value_set_int64(out, static_cast<int64_t>(accepted));
        return true;
      }
      Value bytes_arg = Value::bytes(std::move(bytes));
      return runtime_call_callable(runtime, write_method, &bytes_arg, 1, out, error);
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
    Value pending;
    if (!runtime.take_pending_exception(pending)) {
      runtime.raise_class_error("TypeError", error);
    } else {
      runtime.set_pending_exception(std::move(pending));
    }
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
  if (argc < 2 || argc > 3 || args[1].tag != ValueTag::Int64 ||
      (argc == 3 && args[2].tag != ValueTag::Int64)) {
    error = "memory stream seek() expected offset and optional whence";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return raise_stream_state_error(runtime, error);
  }
  std::lock_guard<std::recursive_mutex> lock(state->mutex);
  if (state->wraps_buffer) {
    if (std::string_view(type) == "_io.TextIOWrapper" &&
        !text_io_flush_pending(runtime, *state, error)) return false;
    if ((std::string_view(type) == "_io.BufferedWriter" ||
         (std::string_view(type) == "_io.BufferedRandom" && state->random_writing)) &&
        !buffered_writer_flush_pending(runtime, *state, state->wrapped_buffer, error)) return false;
    const int64_t whence = argc == 3 ? args[2].as.i64 : 0;
    if (whence < 0 || whence > 2) {
      error = "invalid whence";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    Value seek_method;
    if (!attribute_get(state->wrapped_buffer, "seek", seek_method, error)) return false;
    int64_t offset = args[1].as.i64;
    if (whence == 1) {
      const size_t pending = state->cursor >= state->buffer.size() ? 0 : state->buffer.size() - state->cursor;
      offset -= static_cast<int64_t>(pending);
    }
    Value seek_args[] = {Value::int64(offset), Value::int64(whence)};
    if (!runtime_call_callable(runtime, seek_method, seek_args, 2, out, error)) return false;
    if (out.tag == ValueTag::Int64 && out.as.i64 < 0) {
      error = "raw stream returned invalid position";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    state->buffer.clear();
    state->cursor = 0;
    state->random_writing = false;
    if (std::string_view(type) == "_io.TextIOWrapper" && out.tag == ValueTag::Int64) {
      state->text_encoder_started = out.as.i64 != 0;
      state->text_decoder_started = out.as.i64 != 0;
      state->text_pending_cr = false;
      state->text_read_ahead.clear();
    }
    return true;
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
    runtime.raise_class_error("ValueError", error);
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
  Value flushed;
  if (!stream_flush(
          runtime, args, 1, flushed, error,
          const_cast<char*>("_io.TextIOWrapper"))) return false;
  value_assign_fast(out, state->wrapped_buffer);
  value_set_invalid(state->wrapped_buffer);
  state->wraps_buffer = false;
  state->closed = true;
  return true;
}

bool stream_tell(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream tell() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return raise_stream_state_error(runtime, error);
  }
  if (state->wraps_buffer) {
    Value tell_method;
    if (!attribute_get(state->wrapped_buffer, "tell", tell_method, error)) return false;
    if (!runtime_call_callable(runtime, tell_method, nullptr, 0, out, error)) return false;
    if (out.tag == ValueTag::Int64) {
      if (out.as.i64 < 0) {
        error = "raw stream returned invalid position";
        runtime.raise_class_error("OSError", error);
        return false;
      }
      if (std::string_view(type) == "_io.BufferedWriter" ||
          (std::string_view(type) == "_io.BufferedRandom" && state->random_writing)) {
        out.as.i64 += static_cast<int64_t>(state->buffer.size());
      } else {
        const size_t pending = state->cursor >= state->buffer.size() ? 0 : state->buffer.size() - state->cursor;
        out.as.i64 -= static_cast<int64_t>(pending);
        if (out.as.i64 < 0) out.as.i64 = 0;
      }
    }
    return true;
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
  if (state == nullptr) return raise_stream_state_error(runtime, error);
  if (std::string_view(type) == "_io.BufferedReader") {
    return io_base_unsupported(runtime, args, argc, out, error, const_cast<char*>("truncate"));
  }
  if (state->wraps_buffer) {
    if (std::string_view(type) == "_io.BufferedWriter") {
      if (!buffered_writer_flush_pending(runtime, *state, state->wrapped_buffer, error)) return false;
    } else if (std::string_view(type) == "_io.BufferedRandom") {
      std::lock_guard<std::recursive_mutex> lock(state->mutex);
      if (state->random_writing) {
        if (!buffered_writer_flush_pending(runtime, *state, state->wrapped_buffer, error)) return false;
        state->random_writing = false;
      } else if (!buffered_random_rewind_read(runtime, *state, error)) {
        return false;
      }
    }
    Value truncate_method;
    if (!attribute_get(state->wrapped_buffer, "truncate", truncate_method, error)) return false;
    return runtime_call_callable(runtime, truncate_method, args + 1, argc - 1, out, error);
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
    if (auto* instance = value_as_instance(args[0]);
        instance != nullptr && instance->finalizer_started) {
      value_set_none(out);
      return true;
    }
    error = "invalid memory stream object";
    return false;
  }
  std::lock_guard<std::recursive_mutex> close_lock(state->mutex);
  if (state->binary && !memory_stream_export_allowed(runtime, *state, error)) {
    return false;
  }
  if (auto* instance = value_as_instance(args[0]);
      instance != nullptr && instance->finalizer_started && !state->closed && state->wraps_buffer) {
    Value warnings;
    Value warn;
    std::string ignored;
    if (runtime.import_module("warnings", warnings, ignored) &&
        module_get_attr(warnings, "warn", warn, ignored)) {
      if (const Value* category = runtime.find_builtin("ResourceWarning")) {
        Value warning_args[] = {Value::string("unclosed buffered file"), *category};
        Value warning_result;
        if (!runtime_call_callable(runtime, warn, warning_args, 2, warning_result, ignored)) {
          Value discarded;
          (void)runtime.take_pending_exception(discarded);
        }
      }
    }
  }
  if (std::string_view(static_cast<const char*>(user_data)) == "_io.BufferedRWPair" &&
      state->wraps_buffer && !state->closed) {
    const auto close_endpoint = [&](const Value& endpoint, Value& exception,
                                    std::string& endpoint_error) {
      Value close;
      if (!attribute_get(endpoint, "close", close, endpoint_error)) return false;
      Value result;
      if (runtime_call_callable(runtime, close, nullptr, 0, result, endpoint_error)) return true;
      (void)runtime.take_pending_exception(exception);
      return false;
    };
    Value writer_exception;
    Value reader_exception;
    std::string writer_error;
    std::string reader_error;
    const bool writer_ok = close_endpoint(
        state->wrapped_writer, writer_exception, writer_error);
    const bool reader_ok = close_endpoint(
        state->wrapped_buffer, reader_exception, reader_error);
    std::string ignored;
    state->closed = writer_ok;
    io_set_instance_attr(
        args[0], "__xlang3_io_closed", Value::boolean(state->closed));
    if (!reader_ok) {
      if (reader_exception.tag != ValueTag::Invalid &&
          writer_exception.tag != ValueTag::Invalid) {
        object_set_attr(
            reader_exception, "__context__", writer_exception, ignored);
      }
      error = reader_error;
      if (reader_exception.tag != ValueTag::Invalid) {
        runtime.set_pending_exception(std::move(reader_exception));
      }
      return false;
    }
    if (!writer_ok) {
      error = writer_error;
      if (writer_exception.tag != ValueTag::Invalid) {
        runtime.set_pending_exception(std::move(writer_exception));
      }
      return false;
    }
    value_set_none(out);
    return true;
  }
  Value flush_method;
  Value flush_result;
  Value pending;
  std::string flush_error;
  bool flush_failed = false;
  bool wrapped_already_closed = false;
  if (state->wraps_buffer) {
    for (const char* wrapped_type : {
             "_io.BytesIO", "_io.StringIO", "_io.BufferedReader",
             "_io.BufferedWriter", "_io.BufferedRandom",
             "_io.BufferedRWPair", "_io.TextIOWrapper"}) {
      if (auto* wrapped_state = static_cast<MemoryStreamState*>(
              instance_get_native_data(state->wrapped_buffer, wrapped_type))) {
        wrapped_already_closed = wrapped_state->closed;
        break;
      }
    }
    if (state->wrapped_buffer.tag == ValueTag::Object &&
        state->wrapped_buffer.as.obj != nullptr &&
        state->wrapped_buffer.as.obj->kind == ObjectKind::File) {
      wrapped_already_closed =
          reinterpret_cast<FileObject*>(state->wrapped_buffer.as.obj)->closed;
    }
  }
  if (!state->closed &&
      !wrapped_already_closed &&
      (std::string_view(static_cast<const char*>(user_data)) == "_io.BufferedWriter" ||
       (std::string_view(static_cast<const char*>(user_data)) == "_io.BufferedRandom" &&
        state->random_writing)) &&
      !state->buffer.empty() &&
      !buffered_writer_flush_pending(runtime, *state, state->wrapped_buffer, flush_error)) {
    flush_failed = true;
    (void)runtime.take_pending_exception(pending);
  }
  if (flush_failed && flush_error.find("closed file") != std::string::npos) {
    state->buffer.clear();
    state->closed = true;
    io_set_instance_attr(args[0], "__xlang3_io_closed", Value::boolean(true));
    value_set_none(out);
    error.clear();
    return true;
  }
  bool has_flush_override = false;
  if (auto* instance = value_as_instance(args[0])) {
    has_flush_override = std::any_of(
        instance->attrs.begin(), instance->attrs.end(),
        [](const auto& attr) { return attr.first == "flush"; });
    if (!has_flush_override) {
      const std::string_view type_name(static_cast<const char*>(user_data));
      const size_t dot = type_name.rfind('.');
      const std::string_view base_name = type_name.substr(dot == std::string_view::npos ? 0 : dot + 1);
      Value override;
      std::string ignored;
      auto* actual_class = value_as_class(instance->klass);
      has_flush_override = actual_class != nullptr && actual_class->name != base_name &&
          object_lookup_class_attr_before_base(instance->klass, "flush", base_name, override, ignored);
    }
  }
  if (!state->closed && state->wraps_buffer && wrapped_already_closed &&
      !has_flush_override) {
    state->buffer.clear();
    state->closed = true;
    io_set_instance_attr(args[0], "__xlang3_io_closed", Value::boolean(true));
    value_set_none(out);
    return true;
  }
  if (!state->closed && !flush_failed && (has_flush_override || !wrapped_already_closed)) {
    bool flushed = true;
    if (has_flush_override && object_get_attr(args[0], "flush", flush_method, flush_error)) {
      flushed = runtime_call_callable(runtime, flush_method, nullptr, 0, flush_result, flush_error);
    } else {
      flushed = stream_flush(runtime, args, 1, flush_result, flush_error, user_data);
    }
    if (!flushed) {
      flush_failed = true;
      (void)runtime.take_pending_exception(pending);
    }
  }
  if (state->wraps_buffer && !state->closed) {
    Value close_method;
    std::string ignored;
    if (attribute_get(state->wrapped_buffer, "close", close_method, ignored)) {
      Value close_result;
      if (!runtime_call_callable(runtime, close_method, nullptr, 0, close_result, error)) {
        Value close_exception;
        (void)runtime.take_pending_exception(close_exception);
        Value closed_value;
        if (attribute_get(state->wrapped_buffer, "closed", closed_value, ignored)) {
          state->closed = closed_value.tag == ValueTag::Bool && closed_value.as.b;
        } else {
          state->closed = false;
        }
        bool direct_close_override = false;
        if (auto* raw = value_as_instance(state->wrapped_buffer)) {
          direct_close_override = std::any_of(
              raw->attrs.begin(), raw->attrs.end(),
              [](const auto& attr) { return attr.first == "close"; });
        }
        if (!direct_close_override) state->closed = true;
        io_set_instance_attr(args[0], "__xlang3_io_closed", Value::boolean(state->closed));
        if (!direct_close_override && close_exception.tag != ValueTag::Invalid) {
          auto* close_class = value_as_class(runtime.exception_type(close_exception));
          if (close_class != nullptr && close_class->name == "ValueError") {
            value_set_none(out);
            error.clear();
            return true;
          }
        }
        if (close_exception.tag != ValueTag::Invalid) {
          if (flush_failed && pending.tag != ValueTag::Invalid) {
            std::string context_error;
            object_set_attr(close_exception, "__context__", pending, context_error);
          }
          runtime.set_pending_exception(std::move(close_exception));
        }
        return false;
      }
    }
    if (std::string_view(static_cast<const char*>(user_data)) == "_io.BufferedRWPair") {
      if (attribute_get(state->wrapped_writer, "close", close_method, ignored)) {
        Value close_result;
        if (!runtime_call_callable(runtime, close_method, nullptr, 0, close_result, error)) return false;
      }
    }
  }
  state->closed = true;
  io_set_instance_attr(args[0], "__xlang3_io_closed", Value::boolean(true));
  if (flush_failed) {
    error = flush_error;
    if (pending.tag != ValueTag::Invalid) runtime.set_pending_exception(std::move(pending));
    return false;
  }
  value_set_none(out);
  return true;
}

bool text_io_flush_pending(
    Runtime& runtime, MemoryStreamState& state, std::string& error) {
  while (!state.buffer.empty()) {
    std::string pending = std::move(state.buffer);
    state.buffer.clear();
    Value write_method;
    if (!attribute_get(state.wrapped_buffer, "write", write_method, error)) return false;
    Value bytes = Value::bytes(std::move(pending));
    Value result;
    if (!runtime_call_callable(runtime, write_method, &bytes, 1, result, error)) return false;
  }
  return true;
}

bool buffered_stream_read1(Runtime& runtime, const Value* args, uint32_t argc,
                           Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "read1() expected optional size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = memory_stream_state(args[0], static_cast<const char*>(user_data), error);
  if (state == nullptr) return raise_stream_state_error(runtime, error);
  int64_t requested = -1;
  if (argc == 2 && !value_int_like_to_i64(args[1], requested)) {
    error = "read1 size must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return buffered_reader_read(runtime, *state, requested, true, out, error);
}

bool buffered_pair_require_capability(
    Runtime& runtime, const Value& endpoint, const char* capability,
    const char* message, std::string& error) {
  Value method;
  Value result;
  if (!attribute_get(endpoint, capability, method, error) ||
      !runtime_call_callable(runtime, method, nullptr, 0, result, error)) {
    return false;
  }
  if (!value_truthy(result)) {
    error = message;
    runtime.raise_class_error("OSError", error);
    return false;
  }
  return true;
}

bool buffered_stream_init_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,
    std::string& error, void* user_data) {
  const char* type = static_cast<const char*>(user_data);
  auto invalidate_existing = [&]() {
    if (auto* existing = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], type))) {
      existing->closed = true;
      io_set_instance_attr(args[0], "__xlang3_io_closed", Value::boolean(true));
    }
  };
  const bool pair = std::string_view(type) == "_io.BufferedRWPair";
  const uint32_t required = pair ? 3u : 2u;
  if (argc < required || argc > required + 1) {
    invalidate_existing();
    error = pair ? "_io.BufferedRWPair() expected reader, writer, and optional buffer size"
                 : std::string(type) + "() expected raw stream and optional buffer size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* buffer_size_value = argc == required + 1 ? &args[required] : nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr ||
        std::string_view(kwargs[i].name) != "buffer_size" || buffer_size_value != nullptr) {
      invalidate_existing();
      error = std::string(type) + "() got an unexpected or duplicate keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    buffer_size_value = kwargs[i].value;
  }
  int64_t buffer_size = 8192;
  if (buffer_size_value != nullptr &&
      (!value_int_like_to_i64(*buffer_size_value, buffer_size) || buffer_size <= 0)) {
    invalidate_existing();
    error = "buffer size must be strictly positive";
    runtime.raise_class_error(buffer_size_value->tag == ValueTag::Int64 ? "ValueError" : "TypeError", error);
    return false;
  }
  if (pair) {
    if (!buffered_pair_require_capability(
            runtime, args[1], "readable", "reader is not readable", error) ||
        !buffered_pair_require_capability(
            runtime, args[2], "writable", "writer is not writable", error)) {
      invalidate_existing();
      return false;
    }
    auto* state = new MemoryStreamState();
    state->binary = true;
    state->wraps_buffer = true;
    state->wrapped_buffer = args[1];
    state->wrapped_writer = args[2];
    state->buffer_size = static_cast<size_t>(buffer_size);
    if (!instance_set_native_data(args[0], type, state, memory_stream_cleanup, error)) {
      delete state;
      return false;
    }
    value_set_none(out);
    return true;
  }
  return buffered_stream_load_buffer(runtime, args[0], args[1], type, out, error,
                                     static_cast<size_t>(buffer_size));
}

bool stream_flush(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream flush() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    if (error == "I/O operation on closed file") runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (state->wraps_buffer) {
    if (std::string_view(type) == "_io.TextIOWrapper" &&
        !text_io_flush_pending(runtime, *state, error)) return false;
      if (std::string_view(type) == "_io.BufferedWriter" ||
          std::string_view(type) == "_io.BufferedRandom") {
        std::lock_guard<std::recursive_mutex> lock(state->mutex);
        if (state->closed) {
          error = "I/O operation on closed file";
          runtime.raise_class_error("ValueError", error);
          return false;
        }
      if (std::string_view(type) == "_io.BufferedRandom" && !state->random_writing) {
        if (!buffered_random_rewind_read(runtime, *state, error)) return false;
      } else {
        if (!buffered_writer_flush_pending(runtime, *state, state->wrapped_buffer, error)) return false;
        if (std::string_view(type) == "_io.BufferedRandom") state->random_writing = false;
      }
    }
    Value flush_method;
    std::string ignored;
    if (attribute_get(state->wrapped_buffer, "flush", flush_method, ignored)) {
      Value flush_result;
      if (!runtime_call_callable(runtime, flush_method, nullptr, 0, flush_result, error)) {
        return false;
      }
    }
    if (std::string_view(type) == "_io.BufferedRWPair" &&
        attribute_get(state->wrapped_writer, "flush", flush_method, ignored)) {
      Value flush_result;
      if (!runtime_call_callable(runtime, flush_method, nullptr, 0, flush_result, error)) return false;
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

bool buffered_stream_detach(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                            std::string& error, void* user_data) {
  if (argc != 1) {
    error = "detach() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], static_cast<const char*>(user_data)));
  if (state == nullptr || !state->wraps_buffer || state->closed) {
    error = "raw stream has been detached";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (std::string_view(static_cast<const char*>(user_data)) == "_io.BufferedWriter" ||
      std::string_view(static_cast<const char*>(user_data)) == "_io.BufferedRandom") {
    Value flushed;
    if (!stream_flush(runtime, args, 1, flushed, error, user_data)) return false;
  }
  value_assign_fast(out, state->wrapped_buffer);
  value_set_invalid(state->wrapped_buffer);
  state->wraps_buffer = false;
  state->closed = true;
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

bool string_io_text_property(Runtime&, const Value* args, uint32_t argc, Value& out,
                             std::string& error, void* user_data) {
  if (argc != 1) {
    error = "StringIO property getter expected self";
    return false;
  }
  if (std::string_view(static_cast<const char*>(user_data)) == "line_buffering") {
    value_set_bool(out, false);
  } else {
    value_set_none(out);
  }
  return true;
}

bool stream_capability_impl(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                            std::string& error, void* user_data, const char* capability) {
  if (argc != 1) {
    error = "memory stream capability method expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  auto* state = memory_stream_state(args[0], type, error);
  if (state == nullptr) {
    return false;
  }
  const std::string_view stream_type(type);
  if (stream_type == "_io.BytesIO" || stream_type == "_io.StringIO") {
    value_set_bool(out, true);
    return true;
  }
  if (stream_type == "_io.BufferedReader" && std::string_view(capability) == "writable") {
    value_set_bool(out, false);
    return true;
  }
  if (stream_type == "_io.BufferedWriter" && std::string_view(capability) == "readable") {
    value_set_bool(out, false);
    return true;
  }
  if (stream_type == "_io.BufferedRWPair") {
    value_set_bool(out, std::string_view(capability) != "seekable");
    return true;
  }
  const Value& endpoint = std::string_view(capability) == "writable" &&
          state->wrapped_writer.tag != ValueTag::Invalid
      ? state->wrapped_writer : state->wrapped_buffer;
  if (state->wraps_buffer) {
    Value method;
    if (!attribute_get(endpoint, capability, method, error)) return false;
    return runtime_call_callable(runtime, method, nullptr, 0, out, error);
  }
  value_set_bool(out, true);
  return true;
}

bool stream_readable(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return stream_capability_impl(runtime, args, argc, out, error, user_data, "readable");
}

bool stream_writable(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return stream_capability_impl(runtime, args, argc, out, error, user_data, "writable");
}

bool stream_seekable(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return stream_capability_impl(runtime, args, argc, out, error, user_data, "seekable");
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
  if (std::string_view(type) == "_io.BufferedRWPair") {
    for (const Value* endpoint : {&state->wrapped_buffer, &state->wrapped_writer}) {
      Value method;
      Value result;
      if (!attribute_get(*endpoint, "isatty", method, error) ||
          !runtime_call_callable(runtime, method, nullptr, 0, result, error)) {
        return false;
      }
      if (value_truthy(result)) {
        value_set_bool(out, true);
        return true;
      }
    }
  }
  value_set_bool(out, false);
  return true;
}

bool stream_enter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "memory stream __enter__() expected no arguments";
    return false;
  }
  const char* type = static_cast<const char*>(user_data);
  if (memory_stream_state(args[0], type, error) == nullptr) {
    if (error == "I/O operation on closed file") runtime.raise_class_error("ValueError", error);
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
  if (!open_fn->callback(runtime, open_args, 8, out, error, open_fn->user_data)) return false;
  if (out.tag == ValueTag::Object && out.as.obj != nullptr &&
      out.as.obj->kind == ObjectKind::File) {
    auto* opened = reinterpret_cast<FileObject*>(out.as.obj);
    value_assign_fast(opened->klass, args[0]);
  }
  return true;
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

bool file_io_readinto(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "FileIO.readinto() expected one buffer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  char* destination = nullptr;
  size_t capacity = 0;
  Value exported_buffer;
  if (auto* bytearray = value_as_bytearray(args[1])) {
    destination = bytearray->value.data();
    capacity = bytearray->value.size();
  } else if (auto* view = value_as_memoryview(args[1])) {
    destination = memoryview_object_writable_data(*view);
    capacity = view->size;
  } else if (std::string ignored;
             object_get_attr(args[1], "__xlang3_bytes_value__", exported_buffer, ignored)) {
    if (auto* bytearray = value_as_bytearray(exported_buffer)) {
      destination = bytearray->value.data();
      capacity = bytearray->value.size();
    }
  }
  if (destination == nullptr && capacity != 0) {
    error = "readinto() argument must be read-write bytes-like object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value read;
  const char* read_name = user_data == nullptr ? "read" : static_cast<const char*>(user_data);
  if (!attribute_get(args[0], read_name, read, error)) return false;
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
    runtime.raise_class_error("ValueError", error);
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

bool memory_stream_fileno(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                          std::string& error, void* user_data) {
  if (argc != 1) {
    error = "fileno() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = memory_stream_state(args[0], static_cast<const char*>(user_data), error);
  if (state == nullptr) return false;
  error = "fileno";
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

bool buffered_raw_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void* user_data) {
  if (argc != 1) {
    error = "raw getter expected self";
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], static_cast<const char*>(user_data)));
  if (state == nullptr || !state->wraps_buffer || state->closed) {
    error = "I/O operation on closed file";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  value_assign_fast(out, state->wrapped_buffer);
  return true;
}

bool buffered_rw_pair_endpoint(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                               std::string& error, void* user_data) {
  if (argc != 1) {
    error = "BufferedRWPair endpoint getter expected self";
    return false;
  }
  auto* state = static_cast<MemoryStreamState*>(instance_get_native_data(args[0], "_io.BufferedRWPair"));
  if (state == nullptr || state->closed) {
    error = "I/O operation on closed file";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (std::string_view(static_cast<const char*>(user_data)) == "writer") {
    value_assign_fast(out, state->wrapped_writer);
  } else {
    value_assign_fast(out, state->wrapped_buffer);
  }
  return true;
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
  bool have_name = !is_closed && object_get_attr(args[0], "name", name, error) &&
      !(name.tag == ValueTag::Int64 && name.as.i64 == -1);
  if (!is_closed && state != nullptr && state->wraps_buffer) {
    Value raw_name;
    std::string raw_error;
    if (object_get_attr(state->wrapped_buffer, "name", raw_name, raw_error) &&
        !(raw_name.tag == ValueTag::Int64 && raw_name.as.i64 == -1)) {
      name = std::move(raw_name);
      have_name = true;
    }
  }
  error.clear();
  if (have_name && value_is(name, args[0])) {
    error = "maximum recursion depth exceeded while getting the repr of an object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  std::string name_text;
  if (auto* text = value_as_string(name)) {
    name_text = "'" + string_object_to_string(*text) + "'";
  } else {
    name_text = value_to_string(name);
  }
  std::string type = type_name;
  out = Value::string(have_name ? "<" + type + " name=" + name_text + ">"
                                : "<" + type + ">");
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
  if (state == nullptr || !state->wraps_buffer) {
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
  Value name;
  std::string ignored;
  if (attribute_get(state->wrapped_buffer, "name", name, ignored) &&
      value_as_property(name) == nullptr) {
    value_assign_fast(out, name);
    return true;
  }
  if (const Value* raw = buffered_raw_value(state->wrapped_buffer)) {
    return attribute_get(*raw, "name", out, error);
  }
  error = "underlying buffer has no name";
  runtime.raise_class_error("AttributeError", error);
  return false;
}

bool text_io_repr(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                  std::string& error, void*) {
  auto* state = argc == 1 ? static_cast<MemoryStreamState*>(
      instance_get_native_data(args[0], "_io.TextIOWrapper")) : nullptr;
  if (state == nullptr) {
    error = "uninitialized TextIOWrapper";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::string class_name = "TextIOWrapper";
  if (auto* instance = value_as_instance(args[0])) {
    if (auto* klass = value_as_class(instance->klass)) class_name = klass->name;
  }
  std::string result = "<_io." + class_name;
  Value name;
  std::string ignored;
  bool have_name = state->wraps_buffer &&
      attribute_get(state->wrapped_buffer, "name", name, ignored) &&
      value_as_property(name) == nullptr;
  if (!have_name && state->wraps_buffer) {
    if (const Value* raw = buffered_raw_value(state->wrapped_buffer)) {
      have_name = attribute_get(*raw, "name", name, ignored);
    }
  }
  if (have_name) {
    if (value_is(name, args[0])) {
      error = "maximum recursion depth exceeded while getting the repr of an object";
      runtime.raise_class_error("RuntimeError", error);
      return false;
    }
    result += " name=" + value_to_repr(name);
  }
  Value mode;
  if (object_get_attr(args[0], "mode", mode, ignored)) {
    result += " mode=" + value_to_repr(mode);
  }
  result += " encoding='" + state->encoding + "'>";
  out = Value::string(std::move(result));
  return true;
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
  if (!text_io_flush_pending(runtime, *state, error)) return false;
  bool encoding_changed = false;
  bool errors_changed = false;
  const auto parse_flag = [&](const Value& value, bool& flag) {
    int64_t integer = 0;
    if (value_int_like_to_i64(value, integer) ||
        value_bigint_to_i64(value, integer)) {
      flag = integer != 0;
      return true;
    }
    if (value_as_bigint(value) != nullptr) {
      error = "Python int too large to convert to C long";
      runtime.raise_class_error("OverflowError", error);
      return false;
    }
    Value index_method;
    std::string lookup_error;
    if (!object_get_attr(value, "__index__", index_method, lookup_error)) {
      error = "an integer is required";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    Value converted;
    if (!runtime_call_callable(
            runtime, index_method, nullptr, 0, converted, error)) return false;
    if (!value_int_like_to_i64(converted, integer) &&
        !value_bigint_to_i64(converted, integer)) {
      error = "__index__ returned non-int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    flag = integer != 0;
    return true;
  };
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (kwargs[index].name == nullptr || kwargs[index].value == nullptr) {
      error = "invalid TextIOWrapper.reconfigure() keyword";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const std::string_view name(kwargs[index].name);
    const Value& value = *kwargs[index].value;
    if (name == "encoding" || name == "errors") {
      if (value.tag == ValueTag::None) continue;
      auto* text = value_as_string(value);
      if (text == nullptr) {
        error = std::string(name) + " must be str";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      const std::string candidate = string_object_to_string(*text);
      if (text_contains_utf8_surrogate(candidate)) {
        error = "surrogates not allowed in codec name";
        runtime.raise_class_error("UnicodeEncodeError", error);
        return false;
      }
      if (candidate.find('\0') != std::string::npos) {
        error = "embedded null character in codec name";
        runtime.raise_class_error(name == "encoding" ? "LookupError" : "ValueError", error);
        return false;
      }
      if (name == "encoding") {
        if (state->text_decoded_read) {
          return io_base_unsupported(
              runtime, args, argc, out, error,
              const_cast<char*>("It is not possible to set the encoding after the first read"));
        }
        state->encoding = candidate;
        encoding_changed = true;
      }
      else {
        state->errors = candidate;
        errors_changed = true;
      }
    } else if (name == "newline") {
      if (state->text_decoded_read) {
        return io_base_unsupported(
            runtime, args, argc, out, error,
            const_cast<char*>("It is not possible to set the newline after the first read"));
      }
      if (value.tag == ValueTag::None) {
        state->newline.clear();
        state->newline_is_none = true;
      } else if (auto* text = value_as_string(value)) {
        const std::string candidate = string_object_to_string(*text);
        if (text_contains_utf8_surrogate(candidate) ||
            candidate.find('\0') != std::string::npos ||
            (candidate != "" && candidate != "\n" && candidate != "\r" &&
             candidate != "\r\n")) {
          error = "illegal newline value";
          runtime.raise_class_error("ValueError", error);
          return false;
        }
        state->newline = candidate;
        state->newline_is_none = false;
      } else {
        error = "newline must be str or None";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    } else if (name == "line_buffering") {
      if (value.tag != ValueTag::None) {
        if (!parse_flag(value, state->line_buffering)) return false;
      }
    } else if (name == "write_through") {
      if (value.tag != ValueTag::None) {
        if (!parse_flag(value, state->write_through)) return false;
      }
    } else {
      error = "TextIOWrapper.reconfigure() got an unexpected keyword argument '" + std::string(name) + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (encoding_changed && !errors_changed) state->errors = "strict";
  Value flush_method;
  std::string ignored;
  if (attribute_get(state->wrapped_buffer, "flush", flush_method, ignored)) {
    Value flush_result;
    if (!runtime_call_callable(runtime, flush_method, nullptr, 0, flush_result, error)) return false;
  }
  if (encoding_changed) {
    state->text_encoder_started = false;
    Value seekable_method;
    Value seekable;
    if (attribute_get(state->wrapped_buffer, "seekable", seekable_method, ignored) &&
        runtime_call_callable(
            runtime, seekable_method, nullptr, 0, seekable, ignored) &&
        value_truthy(seekable)) {
      Value tell_method;
      Value position;
      if (attribute_get(state->wrapped_buffer, "tell", tell_method, ignored) &&
          runtime_call_callable(
              runtime, tell_method, nullptr, 0, position, ignored) &&
          position.tag == ValueTag::Int64 && position.as.i64 > 0) {
        state->text_encoder_started = true;
      }
    } else {
      Value discarded;
      (void)runtime.take_pending_exception(discarded);
    }
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
  attrs.push_back({"__module__", Value::string("_io")});
  attrs.push_back({"__init__", runtime.make_native_function(std::string("_io.") + name + ".__init__", init, nullptr, nullptr, nullptr, false, init_kw)});
  if (std::string_view(name) == "StringIO" || std::string_view(name) == "BytesIO") {
    attrs.push_back({"__reduce_ex__", runtime.make_native_function(
        std::string("_io.") + name + ".__reduce_ex__", memory_stream_reduce_ex, const_cast<char*>(type))});
    attrs.push_back({"__getstate__", runtime.make_native_function(
        std::string("_io.") + name + ".__getstate__", memory_stream_getstate, const_cast<char*>(type))});
    attrs.push_back({"__setstate__", runtime.make_native_function(
        std::string("_io.") + name + ".__setstate__", memory_stream_setstate, const_cast<char*>(type))});
    attrs.push_back({"fileno", runtime.make_native_function(
        std::string("_io.") + name + ".fileno", memory_stream_fileno, const_cast<char*>(type))});
  }
  if (std::string_view(name) == "TextIOWrapper") {
    attrs.push_back({"_CHUNK_SIZE", Value::int64(8192)});
    attrs.push_back({"__repr__", runtime.make_native_function(
        "_io.TextIOWrapper.__repr__", text_io_repr)});
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
    attrs.push_back({"read1", runtime.make_native_function("_io.BytesIO.read1", stream_read, const_cast<char*>(type))});
    attrs.push_back({"getbuffer", runtime.make_native_function("_io.BytesIO.getbuffer", stream_getbuffer, const_cast<char*>(type))});
    attrs.push_back({"readinto", runtime.make_native_function("_io.BytesIO.readinto", stream_readinto, const_cast<char*>(type))});
    attrs.push_back({"readinto1", runtime.make_native_function("_io.BytesIO.readinto1", stream_readinto, const_cast<char*>(type))});
  }
  if (std::string_view(name) == "StringIO") {
    attrs.push_back({"newlines", Value::property(
        runtime.make_native_function("_io.StringIO.newlines", string_io_newlines, const_cast<char*>(type)),
        Value::none(), Value::none(), Value::none())});
    for (const char* property : {"encoding", "errors", "line_buffering"}) {
      attrs.push_back({property, Value::property(
          runtime.make_native_function(std::string("_io.StringIO.") + property,
              string_io_text_property, const_cast<char*>(property)),
          Value::none(), Value::none(), Value::none())});
    }
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
  attrs.push_back({"readable", runtime.make_native_function(std::string("_io.") + name + ".readable", stream_readable, const_cast<char*>(type))});
  attrs.push_back({"writable", runtime.make_native_function(std::string("_io.") + name + ".writable", stream_writable, const_cast<char*>(type))});
  attrs.push_back({"seekable", runtime.make_native_function(std::string("_io.") + name + ".seekable", stream_seekable, const_cast<char*>(type))});
  attrs.push_back({"isatty", runtime.make_native_function(std::string("_io.") + name + ".isatty", stream_isatty, const_cast<char*>(type))});
  return Value::class_object(name, std::move(attrs));
}

Value make_buffered_stream_class(Runtime& runtime, const char* name, const char* type, NativeFunctionCallback init) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_io")});
  attrs.push_back({"__repr__", runtime.make_native_function(std::string("_io.") + name + ".__repr__", buffered_repr, const_cast<char*>(type))});
  attrs.push_back({"fileno", runtime.make_native_function(std::string("_io.") + name + ".fileno", stream_fileno, const_cast<char*>(type))});
  if (std::string_view(name) != "BufferedRWPair") {
    attrs.push_back({"raw", Value::property(
        runtime.make_native_function(std::string("_io.") + name + ".raw", buffered_raw_get, const_cast<char*>(type)),
        Value::none(), Value::none(), Value::none())});
    attrs.push_back({"detach", runtime.make_native_function(
        std::string("_io.") + name + ".detach", buffered_stream_detach, const_cast<char*>(type))});
  } else {
    attrs.push_back({"reader", Value::property(
        runtime.make_native_function("_io.BufferedRWPair.reader", buffered_rw_pair_endpoint, const_cast<char*>("reader")),
        Value::none(), Value::none(), Value::none())});
    attrs.push_back({"writer", Value::property(
        runtime.make_native_function("_io.BufferedRWPair.writer", buffered_rw_pair_endpoint, const_cast<char*>("writer")),
        Value::none(), Value::none(), Value::none())});
    attrs.push_back({"detach", runtime.make_native_function("_io.BufferedRWPair.detach", stream_detach)});
  }
  attrs.push_back({"__init__", runtime.make_native_function(
      std::string("_io.") + name + ".__init__", init, const_cast<char*>(type),
      nullptr, nullptr, false, buffered_stream_init_kw)});
  attrs.push_back({"__enter__", runtime.make_native_function(std::string("_io.") + name + ".__enter__", stream_enter, const_cast<char*>(type))});
  attrs.push_back({"__exit__", runtime.make_native_function(std::string("_io.") + name + ".__exit__", stream_exit, const_cast<char*>(type))});
  attrs.push_back({"__iter__", runtime.make_native_function(std::string("_io.") + name + ".__iter__", stream_iter, const_cast<char*>(type))});
  attrs.push_back({"__next__", runtime.make_native_function(std::string("_io.") + name + ".__next__", stream_next, const_cast<char*>(type))});
  attrs.push_back({"read", runtime.make_native_function(std::string("_io.") + name + ".read", stream_read, const_cast<char*>(type))});
  if (std::string_view(name) == "BufferedReader" || std::string_view(name) == "BufferedRandom" ||
      std::string_view(name) == "BufferedRWPair") {
    attrs.push_back({"read1", runtime.make_native_function(
        std::string("_io.") + name + ".read1", buffered_stream_read1, const_cast<char*>(type))});
    attrs.push_back({"readinto", runtime.make_native_function(
        std::string("_io.") + name + ".readinto", stream_readinto, const_cast<char*>(type))});
    attrs.push_back({"readinto1", runtime.make_native_function(
        std::string("_io.") + name + ".readinto1", buffered_stream_readinto1, const_cast<char*>(type))});
    attrs.push_back({"peek", runtime.make_native_function(
        std::string("_io.") + name + ".peek", buffered_stream_peek, const_cast<char*>(type))});
  }
  attrs.push_back({"readline", runtime.make_native_function(std::string("_io.") + name + ".readline", stream_readline, const_cast<char*>(type))});
  attrs.push_back({"readlines", runtime.make_native_function(std::string("_io.") + name + ".readlines", stream_readlines, const_cast<char*>(type))});
  attrs.push_back({"write", runtime.make_native_function(std::string("_io.") + name + ".write", stream_write, const_cast<char*>(type))});
  attrs.push_back({"writelines", runtime.make_native_function(std::string("_io.") + name + ".writelines", stream_writelines, const_cast<char*>(type))});
  attrs.push_back({"seek", runtime.make_native_function(std::string("_io.") + name + ".seek", stream_seek, const_cast<char*>(type))});
  attrs.push_back({"tell", runtime.make_native_function(std::string("_io.") + name + ".tell", stream_tell, const_cast<char*>(type))});
  attrs.push_back({"flush", runtime.make_native_function(std::string("_io.") + name + ".flush", stream_flush, const_cast<char*>(type))});
  attrs.push_back({"truncate", runtime.make_native_function(std::string("_io.") + name + ".truncate", stream_truncate, const_cast<char*>(type))});
  attrs.push_back({"close", runtime.make_native_function(std::string("_io.") + name + ".close", stream_close, const_cast<char*>(type))});
  attrs.push_back({"closed", Value::property(runtime.make_native_function(std::string("_io.") + name + ".closed", stream_closed, const_cast<char*>(type)), Value::none(), Value::none(), Value::none())});
  attrs.push_back({"readable", runtime.make_native_function(std::string("_io.") + name + ".readable", stream_readable, const_cast<char*>(type))});
  attrs.push_back({"writable", runtime.make_native_function(std::string("_io.") + name + ".writable", stream_writable, const_cast<char*>(type))});
  attrs.push_back({"seekable", runtime.make_native_function(std::string("_io.") + name + ".seekable", stream_seekable, const_cast<char*>(type))});
  attrs.push_back({"isatty", runtime.make_native_function(
      std::string("_io.") + name + ".isatty", stream_isatty,
      const_cast<char*>(type))});
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

bool io_text_encoding(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "_io.text_encoding() expected one or two arguments";
    return false;
  }
  if (args[0].tag != ValueTag::None) {
    value_assign_fast(out, args[0]);
  } else {
    bool utf8_mode = false;
    Value sys;
    Value flags;
    Value mode;
    std::string ignored;
    if (runtime.import_module("sys", sys, ignored) &&
        module_get_attr(sys, "flags", flags, ignored) &&
        attribute_get(flags, "utf8_mode", mode, ignored)) {
      utf8_mode = value_truthy(mode);
    }
    out = Value::string(utf8_mode ? "utf-8" : "locale");
    Value warn_enabled;
    if (attribute_get(flags, "warn_default_encoding", warn_enabled, ignored) &&
        value_truthy(warn_enabled)) {
      Value warnings;
      Value warn;
      const Value* category = runtime.find_builtin("EncodingWarning");
      if (category == nullptr ||
          !runtime.import_module("warnings", warnings, error) ||
          !module_get_attr(warnings, "warn", warn, error)) {
        return false;
      }
      const int64_t stacklevel = argc == 2 && args[1].tag == ValueTag::Int64
          ? args[1].as.i64 : 2;
      Value warning_args[] = {
          Value::string("'encoding' argument not specified."), *category,
          Value::int64(stacklevel)};
      Value ignored_result;
      if (!runtime_call_callable(
              runtime, warn, warning_args, 3, ignored_result, error)) {
        return false;
      }
    }
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

bool io_open_alias(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  const Value* builtin_open = runtime.find_builtin("open");
  if (builtin_open == nullptr) {
    error = "builtin open is not available";
    return false;
  }
  return runtime_call_callable(runtime, *builtin_open, args, argc, out, error);
}

bool io_open_alias_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,
    std::string& error, void*) {
  const Value* builtin_open = runtime.find_builtin("open");
  if (builtin_open == nullptr) {
    error = "builtin open is not available";
    return false;
  }
  std::vector<std::pair<std::string, Value>> forwarded;
  forwarded.reserve(kwargc);
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (kwargs[index].name != nullptr && kwargs[index].value != nullptr) {
      forwarded.emplace_back(kwargs[index].name, *kwargs[index].value);
    }
  }
  return runtime_call_callable_kw(
      runtime, *builtin_open, args, argc, forwarded, out, error);
}

bool io_base_del(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_io._IOBase.__del__() expected self";
    return false;
  }
  Value close_method;
  if (!attribute_get(args[0], "close", close_method, error)) return false;
  return runtime_call_callable(runtime, close_method, nullptr, 0, out, error);
}

bool io_base_close(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_io._IOBase.close() expected no arguments";
    return false;
  }
  Value flush;
  Value flush_result;
  Value pending;
  bool flush_failed = false;
  std::string flush_error;
  if (attribute_get(args[0], "flush", flush, flush_error) &&
      !runtime_call_callable(runtime, flush, nullptr, 0, flush_result, flush_error)) {
    flush_failed = true;
    (void)runtime.take_pending_exception(pending);
  }
  Value self = args[0];
  if (!object_set_attr(self, "__xlang3_io_closed", Value::boolean(true), error)) {
    return false;
  }
  if (flush_failed) {
    error = flush_error;
    if (pending.tag != ValueTag::Invalid) {
      runtime.set_pending_exception(std::move(pending));
    }
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
    if (value_truthy(state_value)) {
      value_set_bool(out, true);
      return true;
    }
  }
  Value raw;
  Value raw_closed;
  if (object_get_attr(args[0], "raw", raw, ignored) &&
      object_get_attr(raw, "closed", raw_closed, ignored) && value_truthy(raw_closed)) {
    value_set_bool(out, true);
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

bool io_base_unsupported(Runtime& runtime, const Value*, uint32_t argc, Value&, std::string& error, void* user_data) {
  const char* operation = static_cast<const char*>(user_data);
  if (argc < 1) {
    error = std::string(operation) + "() missing self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  error = operation;
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
    runtime.raise_class_error("TypeError", error);
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

bool raw_io_read(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "_io._RawIOBase.read() expected optional size";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t size = -1;
  if (argc == 2 && args[1].tag != ValueTag::None) {
    if (args[1].tag != ValueTag::Int64) {
      error = "read() argument must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    size = args[1].as.i64;
  }
  Value readinto;
  if (!attribute_get(args[0], "readinto", readinto, error)) return false;
  std::string collected;
  const bool unbounded = size < 0;
  while (unbounded || static_cast<int64_t>(collected.size()) < size) {
    const size_t request = unbounded ? 8192u : static_cast<size_t>(size - static_cast<int64_t>(collected.size()));
    if (request == 0) break;
    Value buffer = Value::bytearray(std::string(request, '\0'));
    Value count;
    if (!runtime_call_callable(runtime, readinto, &buffer, 1, count, error)) return false;
    if (count.tag == ValueTag::None) {
      if (collected.empty()) {
        value_set_none(out);
        return true;
      }
      break;
    }
    if (count.tag != ValueTag::Int64 || count.as.i64 < 0 || static_cast<size_t>(count.as.i64) > request) {
      const std::string count_text = count.tag == ValueTag::Int64
          ? std::to_string(count.as.i64) : value_binary_type_name(count);
      error = "readinto returned " + count_text + " outside buffer size " + std::to_string(request);
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (count.as.i64 == 0) break;
    auto* bytes = value_as_bytearray(buffer);
    collected.append(bytes->value.data(), static_cast<size_t>(count.as.i64));
    if (!unbounded) break;
  }
  out = Value::bytes(std::move(collected));
  return true;
}

bool raw_io_readall(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "_io._RawIOBase.readall() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return raw_io_read(runtime, args, argc, out, error, user_data);
}

void add_io_exports(NativeModuleBuilder& builder, Runtime& runtime, const Value& string_io, const Value& bytes_io) {
  builder.value(
      "open", runtime.make_native_function(
          "_io.open", io_open_alias, nullptr, nullptr, nullptr, false,
          io_open_alias_kw, false));
  Value closed_getter = runtime.make_native_function("_io._IOBase.closed.get", io_base_closed_get);
  const std::vector<std::pair<std::string, Value>> base_attrs = {
      {"__doc__", Value::none()},
      {"__module__", Value::string("_io")},
      {"__xlang3_finalize_on_release__", Value::boolean(true)},
      {"__init__", runtime.make_native_function("_io._IOBase.__init__", io_base_init)},
      {"__del__", runtime.make_native_function("_io._IOBase.__del__", io_base_del)},
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
      {"fileno", runtime.make_native_function("_io._IOBase.fileno", io_base_unsupported, const_cast<char*>("fileno"))},
      {"seek", runtime.make_native_function("_io._IOBase.seek", io_base_unsupported, const_cast<char*>("seek"))},
      {"tell", runtime.make_native_function("_io._IOBase.tell", io_base_unsupported, const_cast<char*>("tell"))},
      {"truncate", runtime.make_native_function("_io._IOBase.truncate", io_base_unsupported, const_cast<char*>("truncate"))},
      {"readline", runtime.make_native_function("_io._IOBase.readline", io_base_readline)},
      {"readlines", runtime.make_native_function("_io._IOBase.readlines", io_base_readlines)},
  };
  Value io_base = Value::class_object("_IOBase", base_attrs);
  std::string ignored;
  if (const Value* object_base = runtime.find_builtin("object")) {
    class_set_base(io_base, *object_base, ignored);
  }
  auto raw_base_attrs = base_attrs;
  raw_base_attrs.push_back({"read", runtime.make_native_function("_io._RawIOBase.read", raw_io_read)});
  raw_base_attrs.push_back({"readall", runtime.make_native_function("_io._RawIOBase.readall", raw_io_readall)});
  Value raw_io_base = Value::class_object("_RawIOBase", std::move(raw_base_attrs), io_base);
  Value text_io_base = Value::class_object("_TextIOBase", base_attrs, io_base);
  auto buffered_base_attrs = base_attrs;
  buffered_base_attrs.push_back({"readinto", runtime.make_native_function(
      "_io._BufferedIOBase.readinto", file_io_readinto)});
  buffered_base_attrs.push_back({"readinto1", runtime.make_native_function(
      "_io._BufferedIOBase.readinto1", file_io_readinto, const_cast<char*>("read1"))});
  buffered_base_attrs.push_back({"read", runtime.make_native_function(
      "_io._BufferedIOBase.read", io_base_unsupported, const_cast<char*>("read"))});
  buffered_base_attrs.push_back({"write", runtime.make_native_function(
      "_io._BufferedIOBase.write", io_base_unsupported, const_cast<char*>("write"))});
  Value buffered_io_base = Value::class_object("_BufferedIOBase", std::move(buffered_base_attrs), io_base);
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
  class_set_base(buffered_reader, buffered_io_base, ignored);
  class_set_base(buffered_writer, buffered_io_base, ignored);
  class_set_base(buffered_random, buffered_io_base, ignored);
  class_set_base(buffered_rw_pair, buffered_io_base, ignored);
  class_set_base(text_io_wrapper, text_io_base, ignored);
  class_set_base(bytes_io, buffered_io_base, ignored);
  class_set_base(string_io, text_io_base, ignored);
  Value incremental_newline_decoder = Value::class_object(
      "IncrementalNewlineDecoder",
      {{"__module__", Value::string("_io")},
       {"__init__", runtime.make_native_function(
           "_io.IncrementalNewlineDecoder.__init__",
           incremental_newline_decoder_init, nullptr, nullptr, nullptr, false,
           incremental_newline_decoder_init_kw)},
       {"decode", runtime.make_native_function(
           "_io.IncrementalNewlineDecoder.decode",
           incremental_newline_decoder_decode, nullptr, nullptr, nullptr, false,
           incremental_newline_decoder_decode_kw)},
       {"getstate", runtime.make_native_function(
           "_io.IncrementalNewlineDecoder.getstate", incremental_newline_decoder_getstate)},
       {"setstate", runtime.make_native_function(
           "_io.IncrementalNewlineDecoder.setstate", incremental_newline_decoder_setstate)},
       {"reset", runtime.make_native_function(
           "_io.IncrementalNewlineDecoder.reset", incremental_newline_decoder_reset)},
       {"newlines", Value::property(
           runtime.make_native_function(
               "_io.IncrementalNewlineDecoder.newlines", incremental_newline_decoder_newlines),
           Value::none(), Value::none(), Value::none())}});
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
