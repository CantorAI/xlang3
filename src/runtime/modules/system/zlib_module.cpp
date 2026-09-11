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

#include <limits>
#include <array>
#include <zlib.h>

namespace xlang3 {

namespace {

constexpr const char* kCompressObjectNativeType = "zlib.Compress";
constexpr const char* kDecompressObjectNativeType = "zlib.Decompress";
constexpr int kDefaultMemLevel = 8;
Value g_zlib_error_class;

bool zlib_fail(Runtime& runtime, std::string message, std::string& error) {
  error = std::move(message);
  runtime.set_pending_exception(runtime.make_exception_from_class(g_zlib_error_class, error));
  return false;
}

bool zlib_class_fail(Runtime& runtime, const char* class_name, std::string message, std::string& error) {
  error = std::move(message);
  runtime.raise_class_error(class_name, error);
  return false;
}

struct ZlibCompressState {
  z_stream stream{};
  bool finished = false;
};

struct ZlibDecompressState {
  z_stream stream{};
  bool finished = false;
  std::string unused_data;
  std::string unconsumed_tail;
  // zlib retains next_in when max_length stops output. Keep that memory alive
  // between calls so flush() can drain the remaining stream safely.
  std::string pending_input;
  std::string dictionary;
};

void zlib_compress_cleanup(void* data) {
  auto* state = static_cast<ZlibCompressState*>(data);
  if (state != nullptr) {
    deflateEnd(&state->stream);
  }
  delete state;
}

void zlib_decompress_cleanup(void* data) {
  auto* state = static_cast<ZlibDecompressState*>(data);
  if (state != nullptr) {
    inflateEnd(&state->stream);
  }
  delete state;
}

bool zlib_bytes_arg(const Value& value, const char* name, std::string& out, std::string& error) {
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
      error = "operation forbidden on released memoryview object";
      return false;
    }
    const auto bytes = memoryview_object_view(*view);
    if (bytes.data() != nullptr) {
      out.assign(bytes.data(), bytes.size());
      return true;
    }
  }
  error = std::string(name) + " must be bytes-like";
  return false;
}

int zlib_level_arg(const Value* args, uint32_t argc, uint32_t index, int default_value) {
  if (argc <= index || args[index].tag != ValueTag::Int64) {
    return default_value;
  }
  const int64_t level = args[index].as.i64;
  if (level < Z_DEFAULT_COMPRESSION || level > Z_BEST_COMPRESSION) {
    return default_value;
  }
  return static_cast<int>(level);
}

bool zlib_int_arg(const Value* args, uint32_t argc, uint32_t index, int default_value, int& out) {
  if (argc <= index || args[index].tag == ValueTag::None) {
    out = default_value;
    return true;
  }
  if (args[index].tag != ValueTag::Int64) {
    return false;
  }
  out = static_cast<int>(args[index].as.i64);
  return true;
}

bool zlib_stream_run(
    z_stream& stream,
    const std::string& input,
    int flush,
    std::string& output,
    int (*step)(z_stream*, int),
    std::string& error) {
  constexpr size_t kChunkSize = 16384;
  char chunk[kChunkSize];
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  do {
    stream.next_out = reinterpret_cast<Bytef*>(chunk);
    stream.avail_out = static_cast<uInt>(kChunkSize);
    const int rc = step(&stream, flush);
    if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
      error = "zlib stream failed: " + std::to_string(rc);
      return false;
    }
    output.append(chunk, kChunkSize - stream.avail_out);
    if (rc == Z_STREAM_END) {
      break;
    }
    if (stream.avail_out != 0) {
      break;
    }
  } while (true);
  return true;
}

bool zlib_compress(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    return zlib_class_fail(runtime, "TypeError", "zlib.compress() expected data and optional level", error);
  }
  std::string input;
  if (!zlib_bytes_arg(args[0], "zlib.compress data", input, error)) {
    return zlib_class_fail(runtime, "TypeError", error, error);
  }
  if (input.size() > std::numeric_limits<uLong>::max()) {
    error = "zlib input too large";
    return false;
  }

  if (argc == 2 && args[1].tag != ValueTag::Int64) {
    return zlib_class_fail(runtime, "TypeError", "zlib.compress() level must be int", error);
  }
  const int level = zlib_level_arg(args, argc, 1, Z_DEFAULT_COMPRESSION);
  if (argc == 2 && (args[1].as.i64 < Z_DEFAULT_COMPRESSION || args[1].as.i64 > Z_BEST_COMPRESSION)) {
    return zlib_fail(runtime, "Bad compression level", error);
  }
  uLongf capacity = compressBound(static_cast<uLong>(input.size()));
  std::string compressed;
  compressed.resize(static_cast<size_t>(capacity));
  const int rc = compress2(
      reinterpret_cast<Bytef*>(compressed.data()),
      &capacity,
      reinterpret_cast<const Bytef*>(input.data()),
      static_cast<uLong>(input.size()),
      level);
  if (rc != Z_OK) {
    return zlib_fail(runtime, "zlib.compress failed: " + std::to_string(rc), error);
  }
  compressed.resize(static_cast<size_t>(capacity));
  out = Value::bytes(std::move(compressed));
  return true;
}

bool zlib_compressobj(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* compress_class_ptr) {
  if (argc > 6) {
    error = "zlib.compressobj() expected optional level, method, wbits, memLevel, strategy, and zdict";
    return false;
  }
  int level = Z_DEFAULT_COMPRESSION;
  int method = Z_DEFLATED;
  int wbits = MAX_WBITS;
  int mem_level = kDefaultMemLevel;
  int strategy = Z_DEFAULT_STRATEGY;
  if (!zlib_int_arg(args, argc, 0, Z_DEFAULT_COMPRESSION, level) ||
      !zlib_int_arg(args, argc, 1, Z_DEFLATED, method) ||
      !zlib_int_arg(args, argc, 2, MAX_WBITS, wbits) ||
      !zlib_int_arg(args, argc, 3, kDefaultMemLevel, mem_level) ||
      !zlib_int_arg(args, argc, 4, Z_DEFAULT_STRATEGY, strategy)) {
    return zlib_class_fail(runtime, "TypeError", "zlib.compressobj() arguments must be integers", error);
  }
  if (method != Z_DEFLATED) {
    return zlib_class_fail(runtime, "ValueError", "zlib.compressobj() only supports DEFLATED", error);
  }
  auto* state = new ZlibCompressState();
  const int rc = deflateInit2(&state->stream, level, method, wbits, mem_level, strategy);
  if (rc != Z_OK) {
    delete state;
    return zlib_class_fail(runtime, "ValueError", "zlib.compressobj init failed: " + std::to_string(rc), error);
  }
  if (argc == 6 && args[5].tag != ValueTag::None) {
    std::string dictionary;
    if (!zlib_bytes_arg(args[5], "zlib.compressobj zdict", dictionary, error) ||
        deflateSetDictionary(&state->stream, reinterpret_cast<const Bytef*>(dictionary.data()), static_cast<uInt>(dictionary.size())) != Z_OK) {
      zlib_compress_cleanup(state);
      return zlib_class_fail(runtime, "ValueError", "zlib.compressobj dictionary setup failed", error);
    }
  }
  auto* compress_class = static_cast<Value*>(compress_class_ptr);
  out = Value::instance(*compress_class);
  if (!instance_set_native_data(out, kCompressObjectNativeType, state, zlib_compress_cleanup, error)) {
    zlib_compress_cleanup(state);
    return false;
  }
  (void)runtime;
  return true;
}

bool zlib_decompressobj(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* decompress_class_ptr) {
  if (argc > 2) {
    error = "zlib.decompressobj() expected optional wbits and zdict";
    return false;
  }
  int wbits = MAX_WBITS;
  if (!zlib_int_arg(args, argc, 0, MAX_WBITS, wbits)) {
    return zlib_class_fail(runtime, "TypeError", "zlib.decompressobj() wbits must be int", error);
  }
  auto* state = new ZlibDecompressState();
  const int rc = inflateInit2(&state->stream, wbits);
  if (rc != Z_OK) {
    delete state;
    return zlib_class_fail(runtime, "ValueError", "zlib.decompressobj init failed: " + std::to_string(rc), error);
  }
  if (argc == 2 && args[1].tag != ValueTag::None &&
      !zlib_bytes_arg(args[1], "zlib.decompressobj zdict", state->dictionary, error)) {
    zlib_decompress_cleanup(state);
    return zlib_class_fail(runtime, "TypeError", error, error);
  }
  auto* decompress_class = static_cast<Value*>(decompress_class_ptr);
  out = Value::instance(*decompress_class);
  if (!instance_set_native_data(out, kDecompressObjectNativeType, state, zlib_decompress_cleanup, error)) {
    zlib_decompress_cleanup(state);
    return false;
  }
  object_set_attr(out, "unused_data", Value::bytes(""), error);
  object_set_attr(out, "unconsumed_tail", Value::bytes(""), error);
  object_set_attr(out, "eof", Value::boolean(false), error);
  (void)runtime;
  return true;
}

bool zlib_decompress(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    return zlib_class_fail(runtime, "TypeError", "zlib.decompress() expected data, optional wbits, and optional bufsize", error);
  }
  std::string input;
  if (!zlib_bytes_arg(args[0], "zlib.decompress data", input, error)) {
    return zlib_class_fail(runtime, "TypeError", error, error);
  }
  if (argc >= 2 && args[1].tag != ValueTag::Int64) {
    return zlib_class_fail(runtime, "TypeError", "zlib.decompress() wbits must be int", error);
  }
  if (argc >= 3 && args[2].tag != ValueTag::Int64) {
    return zlib_class_fail(runtime, "TypeError", "zlib.decompress() bufsize must be int", error);
  }
  if (argc >= 3 && args[2].as.i64 < 0) {
    return zlib_class_fail(runtime, "ValueError", "bufsize must be non-negative", error);
  }
  const int wbits = argc >= 2 ? static_cast<int>(args[1].as.i64) : MAX_WBITS;
  size_t chunk_size = 16384;
  if (argc >= 3 && args[2].tag == ValueTag::Int64 && args[2].as.i64 > 0) {
    chunk_size = static_cast<size_t>(args[2].as.i64);
  }

  z_stream stream{};
  int rc = inflateInit2(&stream, wbits);
  if (rc != Z_OK) {
    error = "zlib.decompress init failed: " + std::to_string(rc);
    return false;
  }

  std::string decompressed;
  std::string chunk;
  chunk.resize(chunk_size);
  stream.next_in = reinterpret_cast<Bytef*>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());
  do {
    stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
    stream.avail_out = static_cast<uInt>(chunk.size());
    rc = inflate(&stream, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) {
      inflateEnd(&stream);
      return zlib_fail(runtime, "zlib.decompress failed: " + std::to_string(rc), error);
    }
    decompressed.append(chunk.data(), chunk.size() - stream.avail_out);
  } while (rc != Z_STREAM_END);
  inflateEnd(&stream);
  out = Value::bytes(std::move(decompressed));
  return true;
}

bool compress_object_state(const Value& self, ZlibCompressState*& state, std::string& error) {
  state = static_cast<ZlibCompressState*>(instance_get_native_data(self, kCompressObjectNativeType));
  if (state == nullptr) {
    error = "invalid zlib Compress object";
    return false;
  }
  return true;
}

bool decompress_object_state(const Value& self, ZlibDecompressState*& state, std::string& error) {
  state = static_cast<ZlibDecompressState*>(instance_get_native_data(self, kDecompressObjectNativeType));
  if (state == nullptr) {
    error = "invalid zlib Decompress object";
    return false;
  }
  return true;
}

bool zlib_compress_object_compress(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Compress.compress() expected data";
    return false;
  }
  ZlibCompressState* state = nullptr;
  if (!compress_object_state(args[0], state, error)) {
    return false;
  }
  if (state->finished) {
    return zlib_fail(runtime, "Error -2 while compressing data: inconsistent stream state", error);
  }
  std::string input;
  if (!zlib_bytes_arg(args[1], "Compress.compress data", input, error)) {
    return false;
  }
  std::string compressed;
  if (!zlib_stream_run(state->stream, input, Z_NO_FLUSH, compressed, deflate, error)) {
    return false;
  }
  out = Value::bytes(std::move(compressed));
  return true;
}

bool zlib_compress_object_flush(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "Compress.flush() expected optional mode";
    return false;
  }
  ZlibCompressState* state = nullptr;
  if (!compress_object_state(args[0], state, error)) {
    return false;
  }
  int mode = Z_FINISH;
  if (!zlib_int_arg(args, argc, 1, Z_FINISH, mode)) {
    error = "Compress.flush() mode must be int";
    return false;
  }
  if (state->finished) {
    return zlib_fail(runtime, "Error -2 while flushing: inconsistent stream state", error);
  }
  std::string compressed;
  if (!zlib_stream_run(state->stream, "", mode, compressed, deflate, error)) {
    return zlib_fail(runtime, error, error);
  }
  if (mode == Z_FINISH) {
    state->finished = true;
  }
  out = Value::bytes(std::move(compressed));
  return true;
}

bool zlib_compressobj_kw(Runtime& runtime, const Value* args, uint32_t argc,
                         const NativeKeywordArg* kwargs, uint32_t kwargc,
                         Value& out, std::string& error, void* user_data) {
  if (argc > 6) return zlib_class_fail(runtime, "TypeError", "zlib.compressobj() takes at most 6 arguments", error);
  std::array<Value, 6> values = {Value::int64(Z_DEFAULT_COMPRESSION), Value::int64(Z_DEFLATED),
      Value::int64(MAX_WBITS), Value::int64(kDefaultMemLevel), Value::int64(Z_DEFAULT_STRATEGY), Value::none()};
  std::array<bool, 6> set{};
  for (uint32_t i = 0; i < argc; ++i) { values[i] = args[i]; set[i] = true; }
  const std::array<std::string_view, 6> names = {"level", "method", "wbits", "memLevel", "strategy", "zdict"};
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) return zlib_class_fail(runtime, "TypeError", "invalid zlib.compressobj keyword", error);
    size_t index = names.size();
    for (size_t j = 0; j < names.size(); ++j) if (names[j] == kwargs[i].name) { index = j; break; }
    if (index == names.size()) return zlib_class_fail(runtime, "TypeError", "zlib.compressobj() got an unexpected keyword argument", error);
    if (set[index]) return zlib_class_fail(runtime, "TypeError", "zlib.compressobj() got multiple values for an argument", error);
    values[index] = *kwargs[i].value; set[index] = true;
  }
  return zlib_compressobj(runtime, values.data(), 6, out, error, user_data);
}

bool zlib_decompressobj_kw(Runtime& runtime, const Value* args, uint32_t argc,
                           const NativeKeywordArg* kwargs, uint32_t kwargc,
                           Value& out, std::string& error, void* user_data) {
  if (argc > 2) return zlib_class_fail(runtime, "TypeError", "zlib.decompressobj() takes at most 2 arguments", error);
  std::array<Value, 2> values = {Value::int64(MAX_WBITS), Value::none()};
  std::array<bool, 2> set{};
  for (uint32_t i = 0; i < argc; ++i) { values[i] = args[i]; set[i] = true; }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) return zlib_class_fail(runtime, "TypeError", "invalid zlib.decompressobj keyword", error);
    const std::string_view name(kwargs[i].name);
    const size_t index = name == "wbits" ? 0 : name == "zdict" ? 1 : 2;
    if (index == 2) return zlib_class_fail(runtime, "TypeError", "zlib.decompressobj() got an unexpected keyword argument", error);
    if (set[index]) return zlib_class_fail(runtime, "TypeError", "zlib.decompressobj() got multiple values for an argument", error);
    values[index] = *kwargs[i].value; set[index] = true;
  }
  return zlib_decompressobj(runtime, values.data(), 2, out, error, user_data);
}

bool zlib_compress_object_copy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) return zlib_class_fail(runtime, "TypeError", "Compress.copy() expected no arguments", error);
  ZlibCompressState* state = nullptr;
  if (!compress_object_state(args[0], state, error)) return false;
  if (state->finished) return zlib_class_fail(runtime, "ValueError", "inconsistent stream state", error);
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) return zlib_class_fail(runtime, "TypeError", "invalid zlib Compress object", error);
  auto* copied = new ZlibCompressState();
  if (deflateCopy(&copied->stream, &state->stream) != Z_OK) {
    delete copied;
    return zlib_fail(runtime, "zlib compressor copy failed", error);
  }
  copied->finished = state->finished;
  out = Value::instance(instance->klass);
  if (!instance_set_native_data(out, kCompressObjectNativeType, copied, zlib_compress_cleanup, error)) {
    zlib_compress_cleanup(copied);
    return false;
  }
  return true;
}

bool zlib_compress_object_deepcopy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) return zlib_class_fail(runtime, "TypeError", "Compress.__deepcopy__() expected memo", error);
  return zlib_compress_object_copy(runtime, args, 1, out, error, nullptr);
}

bool zlib_decompress_object_decompress(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "Decompress.decompress() expected data and optional max_length";
    return false;
  }
  ZlibDecompressState* state = nullptr;
  if (!decompress_object_state(args[0], state, error)) {
    return false;
  }
  std::string input;
  if (!zlib_bytes_arg(args[1], "Decompress.decompress data", input, error)) {
    return false;
  }
  int max_length = 0;
  if (!zlib_int_arg(args, argc, 2, 0, max_length)) {
    error = "Decompress.decompress() max_length must be int";
    return false;
  }
  if (max_length < 0) {
    error = "max_length must be non-negative";
    return false;
  }

  if (state->finished) {
    state->unused_data.append(input);
    std::string ignored;
    Value self = args[0];
    object_set_attr(self, "unused_data", Value::bytes(state->unused_data), ignored);
    object_set_attr(self, "unconsumed_tail", Value::bytes(""), ignored);
    object_set_attr(self, "eof", Value::boolean(true), ignored);
    out = Value::bytes("");
    return true;
  }

  constexpr size_t kChunkSize = 16384;
  char chunk[kChunkSize];
  std::string decompressed;
  state->pending_input.clear();
  state->stream.next_in = reinterpret_cast<Bytef*>(input.data());
  state->stream.avail_in = static_cast<uInt>(input.size());
  do {
    const size_t requested = max_length > 0
        ? std::min<size_t>(kChunkSize, static_cast<size_t>(max_length) - decompressed.size())
        : kChunkSize;
    if (requested == 0) {
      break;
    }
    state->stream.next_out = reinterpret_cast<Bytef*>(chunk);
    state->stream.avail_out = static_cast<uInt>(requested);
    int rc = inflate(&state->stream, Z_NO_FLUSH);
    if (rc == Z_NEED_DICT && !state->dictionary.empty()) {
      if (inflateSetDictionary(&state->stream,
                               reinterpret_cast<const Bytef*>(state->dictionary.data()),
                               static_cast<uInt>(state->dictionary.size())) != Z_OK) {
        error = "zlib decompressor dictionary setup failed";
        return false;
      }
      rc = inflate(&state->stream, Z_NO_FLUSH);
    }
    if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
      return zlib_fail(runtime, "zlib decompressor failed: " + std::to_string(rc), error);
    }
    decompressed.append(chunk, requested - state->stream.avail_out);
    if (rc == Z_STREAM_END) {
      state->finished = true;
      if (state->stream.avail_in > 0) {
        state->unused_data.append(
            reinterpret_cast<const char*>(state->stream.next_in),
            state->stream.avail_in);
      }
      break;
    }
    if (rc == Z_BUF_ERROR || state->stream.avail_in == 0 || (max_length > 0 && decompressed.size() >= static_cast<size_t>(max_length))) {
      break;
    }
  } while (true);

  state->unconsumed_tail.clear();
  if (!state->finished && state->stream.avail_in > 0) {
    state->unconsumed_tail.assign(
        reinterpret_cast<const char*>(state->stream.next_in), state->stream.avail_in);
    state->pending_input = state->unconsumed_tail;
    state->stream.next_in = reinterpret_cast<Bytef*>(state->pending_input.data());
    state->stream.avail_in = static_cast<uInt>(state->pending_input.size());
  }
  std::string ignored;
  Value self = args[0];
  object_set_attr(self, "unused_data", Value::bytes(state->unused_data), ignored);
  object_set_attr(self, "unconsumed_tail", Value::bytes(state->unconsumed_tail), ignored);
  object_set_attr(self, "eof", Value::boolean(state->finished), ignored);
  out = Value::bytes(std::move(decompressed));
  return true;
}

bool zlib_decompress_object_flush(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) { error = "Decompress.flush() expected optional length"; return false; }
  ZlibDecompressState* state = nullptr;
  if (!decompress_object_state(args[0], state, error)) return false;
  int length = 16384;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64 || args[1].as.i64 <= 0) return zlib_class_fail(runtime, "ValueError", "length must be greater than zero", error);
    length = static_cast<int>(std::min<int64_t>(args[1].as.i64, 1 << 20));
  }
  if (state->finished) { out = Value::bytes(""); return true; }

  std::string decoded;
  std::vector<char> chunk(static_cast<size_t>(std::max(length, 16384)));
  do {
    state->stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
    state->stream.avail_out = static_cast<uInt>(chunk.size());
    const int rc = inflate(&state->stream, Z_FINISH);
    if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
      return zlib_fail(runtime, "zlib decompressor flush failed: " + std::to_string(rc), error);
    }
    decoded.append(chunk.data(), chunk.size() - state->stream.avail_out);
    if (rc == Z_STREAM_END) { state->finished = true; break; }
    if (rc == Z_BUF_ERROR || state->stream.avail_out != 0) break;
  } while (true);

  if (state->finished && state->stream.avail_in > 0) {
    state->unused_data.append(reinterpret_cast<const char*>(state->stream.next_in), state->stream.avail_in);
  }
  state->pending_input.clear();
  state->unconsumed_tail.clear();
  std::string ignored;
  Value self = args[0];
  object_set_attr(self, "unused_data", Value::bytes(state->unused_data), ignored);
  object_set_attr(self, "unconsumed_tail", Value::bytes(""), ignored);
  object_set_attr(self, "eof", Value::boolean(state->finished), ignored);
  out = Value::bytes(std::move(decoded));
  return true;
}

bool zlib_decompress_object_copy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) return zlib_class_fail(runtime, "TypeError", "Decompress.copy() expected no arguments", error);
  ZlibDecompressState* state = nullptr;
  if (!decompress_object_state(args[0], state, error)) return false;
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) return zlib_class_fail(runtime, "TypeError", "invalid zlib Decompress object", error);
  auto* copied = new ZlibDecompressState();
  if (inflateCopy(&copied->stream, &state->stream) != Z_OK) {
    delete copied;
    return zlib_fail(runtime, "zlib decompressor copy failed", error);
  }
  copied->finished = state->finished;
  copied->unused_data = state->unused_data;
  copied->unconsumed_tail = state->unconsumed_tail;
  copied->pending_input = state->pending_input;
  if (!copied->pending_input.empty()) {
    copied->stream.next_in = reinterpret_cast<Bytef*>(copied->pending_input.data());
    copied->stream.avail_in = static_cast<uInt>(copied->pending_input.size());
  }
  copied->dictionary = state->dictionary;
  out = Value::instance(instance->klass);
  if (!instance_set_native_data(out, kDecompressObjectNativeType, copied, zlib_decompress_cleanup, error)) {
    zlib_decompress_cleanup(copied);
    return false;
  }
  object_set_attr(out, "unused_data", Value::bytes(copied->unused_data), error);
  object_set_attr(out, "unconsumed_tail", Value::bytes(copied->unconsumed_tail), error);
  object_set_attr(out, "eof", Value::boolean(copied->finished), error);
  return true;
}

bool zlib_decompress_object_deepcopy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) return zlib_class_fail(runtime, "TypeError", "Decompress.__deepcopy__() expected memo", error);
  return zlib_decompress_object_copy(runtime, args, 1, out, error, nullptr);
}

bool zlib_crc32(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    return zlib_class_fail(runtime, "TypeError", "zlib.crc32() expected data and optional value", error);
  }
  std::string input;
  if (!zlib_bytes_arg(args[0], "zlib.crc32 data", input, error)) {
    return zlib_class_fail(runtime, "TypeError", error, error);
  }
  uLong seed = 0;
  if (argc == 2 && args[1].tag == ValueTag::Int64) {
    seed = static_cast<uLong>(args[1].as.i64);
  }
  value_set_int64(out, static_cast<int64_t>(crc32(seed, reinterpret_cast<const Bytef*>(input.data()), static_cast<uInt>(input.size()))));
  return true;
}

bool zlib_adler32(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    return zlib_class_fail(runtime, "TypeError", "zlib.adler32() expected data and optional value", error);
  }
  std::string input;
  if (!zlib_bytes_arg(args[0], "zlib.adler32 data", input, error)) {
    return zlib_class_fail(runtime, "TypeError", error, error);
  }
  uLong seed = 1;
  if (argc == 2 && args[1].tag == ValueTag::Int64) {
    seed = static_cast<uLong>(args[1].as.i64);
  }
  value_set_int64(out, static_cast<int64_t>(adler32(seed, reinterpret_cast<const Bytef*>(input.data()), static_cast<uInt>(input.size()))));
  return true;
}

Value make_compress_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zlib")});
  attrs.push_back({"compress", runtime.make_native_function("zlib.Compress.compress", zlib_compress_object_compress)});
  attrs.push_back({"flush", runtime.make_native_function("zlib.Compress.flush", zlib_compress_object_flush)});
  attrs.push_back({"copy", runtime.make_native_function("zlib.Compress.copy", zlib_compress_object_copy)});
  attrs.push_back({"__copy__", runtime.make_native_function("zlib.Compress.__copy__", zlib_compress_object_copy)});
  attrs.push_back({"__deepcopy__", runtime.make_native_function("zlib.Compress.__deepcopy__", zlib_compress_object_deepcopy)});
  return Value::class_object("Compress", std::move(attrs));
}

Value make_decompress_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zlib")});
  attrs.push_back({"decompress", runtime.make_native_function("zlib.Decompress.decompress", zlib_decompress_object_decompress)});
  attrs.push_back({"flush", runtime.make_native_function("zlib.Decompress.flush", zlib_decompress_object_flush)});
  attrs.push_back({"copy", runtime.make_native_function("zlib.Decompress.copy", zlib_decompress_object_copy)});
  attrs.push_back({"__copy__", runtime.make_native_function("zlib.Decompress.__copy__", zlib_decompress_object_copy)});
  attrs.push_back({"__deepcopy__", runtime.make_native_function("zlib.Decompress.__deepcopy__", zlib_decompress_object_deepcopy)});
  return Value::class_object("Decompress", std::move(attrs));
}

} // namespace

void register_zlib_module(Runtime& runtime) {
  g_zlib_error_class = Value::class_object(
      "error",
      {
          {"__module__", Value::string("zlib")},
          {"__qualname__", Value::string("error")},
      },
      runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::invalid());
  Value compress_class = make_compress_class(runtime);
  Value decompress_class = make_decompress_class(runtime);
  auto* compress_class_slot = new Value(compress_class);
  auto* decompress_class_slot = new Value(decompress_class);
  NativeModuleBuilder builder(runtime, "zlib");
  builder.function("compress", zlib_compress)
      .function("decompress", zlib_decompress)
      .value(
          "compressobj",
          runtime.make_native_function(
              "zlib.compressobj",
              zlib_compressobj,
              compress_class_slot,
              [](void* data) { delete static_cast<Value*>(data); }, nullptr, false, zlib_compressobj_kw))
      .value(
          "decompressobj",
          runtime.make_native_function(
              "zlib.decompressobj",
              zlib_decompressobj,
              decompress_class_slot,
              [](void* data) { delete static_cast<Value*>(data); }, nullptr, false, zlib_decompressobj_kw))
      .function("crc32", zlib_crc32)
      .function("adler32", zlib_adler32)
      .value("Compress", compress_class)
      .value("Decompress", decompress_class)
      .value("error", g_zlib_error_class)
      .value("Z_DEFAULT_COMPRESSION", Value::int64(Z_DEFAULT_COMPRESSION))
      .value("Z_BEST_SPEED", Value::int64(Z_BEST_SPEED))
      .value("Z_BEST_COMPRESSION", Value::int64(Z_BEST_COMPRESSION))
      .value("Z_NO_COMPRESSION", Value::int64(Z_NO_COMPRESSION))
      .value("Z_NO_FLUSH", Value::int64(Z_NO_FLUSH))
      .value("Z_PARTIAL_FLUSH", Value::int64(Z_PARTIAL_FLUSH))
      .value("Z_SYNC_FLUSH", Value::int64(Z_SYNC_FLUSH))
      .value("Z_FULL_FLUSH", Value::int64(Z_FULL_FLUSH))
      .value("Z_FINISH", Value::int64(Z_FINISH))
      .value("Z_BLOCK", Value::int64(Z_BLOCK))
      .value("Z_TREES", Value::int64(Z_TREES))
      .value("Z_DEFAULT_STRATEGY", Value::int64(Z_DEFAULT_STRATEGY))
      .value("Z_FILTERED", Value::int64(Z_FILTERED))
      .value("Z_HUFFMAN_ONLY", Value::int64(Z_HUFFMAN_ONLY))
      .value("Z_RLE", Value::int64(Z_RLE))
      .value("Z_FIXED", Value::int64(Z_FIXED))
      .value("DEF_MEM_LEVEL", Value::int64(kDefaultMemLevel))
      .value("MAX_WBITS", Value::int64(MAX_WBITS))
      .value("DEFLATED", Value::int64(Z_DEFLATED))
      .value("ZLIB_VERSION", Value::string(ZLIB_VERSION))
      // CPython exposes the version reported by the linked runtime separately
      // from the build-time header version.
      .value("ZLIB_RUNTIME_VERSION", Value::string(zlibVersion()));
  runtime.register_module("zlib", builder.finish());
}

} // namespace xlang3
