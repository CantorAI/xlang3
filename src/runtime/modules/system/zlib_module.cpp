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
#include <zlib.h>

namespace xlang3 {

namespace {

constexpr const char* kCompressObjectNativeType = "zlib.Compress";
constexpr const char* kDecompressObjectNativeType = "zlib.Decompress";
constexpr int kDefaultMemLevel = 8;

struct ZlibCompressState {
  z_stream stream{};
  bool finished = false;
};

struct ZlibDecompressState {
  z_stream stream{};
  bool finished = false;
  std::string unused_data;
  std::string unconsumed_tail;
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
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
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

bool zlib_compress(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "zlib.compress() expected data and optional level";
    return false;
  }
  std::string input;
  if (!zlib_bytes_arg(args[0], "zlib.compress data", input, error)) {
    return false;
  }
  if (input.size() > std::numeric_limits<uLong>::max()) {
    error = "zlib input too large";
    return false;
  }

  const int level = zlib_level_arg(args, argc, 1, Z_DEFAULT_COMPRESSION);
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
    error = "zlib.compress failed: " + std::to_string(rc);
    return false;
  }
  compressed.resize(static_cast<size_t>(capacity));
  out = Value::bytes(std::move(compressed));
  return true;
}

bool zlib_compressobj(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* compress_class_ptr) {
  if (argc > 5) {
    error = "zlib.compressobj() expected optional level, method, wbits, memLevel, and strategy";
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
    error = "zlib.compressobj() arguments must be integers";
    return false;
  }
  if (method != Z_DEFLATED) {
    error = "zlib.compressobj() only supports DEFLATED";
    return false;
  }
  auto* state = new ZlibCompressState();
  const int rc = deflateInit2(&state->stream, level, method, wbits, mem_level, strategy);
  if (rc != Z_OK) {
    delete state;
    error = "zlib.compressobj init failed: " + std::to_string(rc);
    return false;
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
    error = "zlib.decompressobj() wbits must be int";
    return false;
  }
  auto* state = new ZlibDecompressState();
  const int rc = inflateInit2(&state->stream, wbits);
  if (rc != Z_OK) {
    delete state;
    error = "zlib.decompressobj init failed: " + std::to_string(rc);
    return false;
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

bool zlib_decompress(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "zlib.decompress() expected data, optional wbits, and optional bufsize";
    return false;
  }
  std::string input;
  if (!zlib_bytes_arg(args[0], "zlib.decompress data", input, error)) {
    return false;
  }
  const int wbits = argc >= 2 && args[1].tag == ValueTag::Int64 ? static_cast<int>(args[1].as.i64) : MAX_WBITS;
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
      error = "zlib.decompress failed: " + std::to_string(rc);
      return false;
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

bool zlib_compress_object_compress(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Compress.compress() expected data";
    return false;
  }
  ZlibCompressState* state = nullptr;
  if (!compress_object_state(args[0], state, error)) {
    return false;
  }
  if (state->finished) {
    error = "compressor object already flushed";
    return false;
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

bool zlib_compress_object_flush(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
    out = Value::bytes("");
    return true;
  }
  std::string compressed;
  if (!zlib_stream_run(state->stream, "", mode, compressed, deflate, error)) {
    return false;
  }
  if (mode == Z_FINISH) {
    state->finished = true;
  }
  out = Value::bytes(std::move(compressed));
  return true;
}

bool zlib_decompress_object_decompress(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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

  constexpr size_t kChunkSize = 16384;
  char chunk[kChunkSize];
  std::string decompressed;
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
    const int rc = inflate(&state->stream, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
      error = "zlib decompressor failed: " + std::to_string(rc);
      return false;
    }
    decompressed.append(chunk, requested - state->stream.avail_out);
    if (rc == Z_STREAM_END) {
      state->finished = true;
      if (state->stream.avail_in > 0) {
        state->unused_data.assign(
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
        reinterpret_cast<const char*>(state->stream.next_in),
        state->stream.avail_in);
  }
  std::string ignored;
  Value self = args[0];
  object_set_attr(self, "unused_data", Value::bytes(state->unused_data), ignored);
  object_set_attr(self, "unconsumed_tail", Value::bytes(state->unconsumed_tail), ignored);
  object_set_attr(self, "eof", Value::boolean(state->finished), ignored);
  out = Value::bytes(std::move(decompressed));
  return true;
}

bool zlib_decompress_object_flush(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "Decompress.flush() expected optional length";
    return false;
  }
  ZlibDecompressState* state = nullptr;
  if (!decompress_object_state(args[0], state, error)) {
    return false;
  }
  out = Value::bytes("");
  return true;
}

bool zlib_crc32(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "zlib.crc32() expected data and optional value";
    return false;
  }
  std::string input;
  if (!zlib_bytes_arg(args[0], "zlib.crc32 data", input, error)) {
    return false;
  }
  uLong seed = 0;
  if (argc == 2 && args[1].tag == ValueTag::Int64) {
    seed = static_cast<uLong>(args[1].as.i64);
  }
  value_set_int64(out, static_cast<int64_t>(crc32(seed, reinterpret_cast<const Bytef*>(input.data()), static_cast<uInt>(input.size()))));
  return true;
}

bool zlib_adler32(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "zlib.adler32() expected data and optional value";
    return false;
  }
  std::string input;
  if (!zlib_bytes_arg(args[0], "zlib.adler32 data", input, error)) {
    return false;
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
  return Value::class_object("Compress", std::move(attrs));
}

Value make_decompress_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zlib")});
  attrs.push_back({"decompress", runtime.make_native_function("zlib.Decompress.decompress", zlib_decompress_object_decompress)});
  attrs.push_back({"flush", runtime.make_native_function("zlib.Decompress.flush", zlib_decompress_object_flush)});
  return Value::class_object("Decompress", std::move(attrs));
}

} // namespace

void register_zlib_module(Runtime& runtime) {
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
              [](void* data) { delete static_cast<Value*>(data); }))
      .value(
          "decompressobj",
          runtime.make_native_function(
              "zlib.decompressobj",
              zlib_decompressobj,
              decompress_class_slot,
              [](void* data) { delete static_cast<Value*>(data); }))
      .function("crc32", zlib_crc32)
      .function("adler32", zlib_adler32)
      .value("Compress", compress_class)
      .value("Decompress", decompress_class)
      .value("Z_DEFAULT_COMPRESSION", Value::int64(Z_DEFAULT_COMPRESSION))
      .value("Z_BEST_SPEED", Value::int64(Z_BEST_SPEED))
      .value("Z_BEST_COMPRESSION", Value::int64(Z_BEST_COMPRESSION))
      .value("Z_NO_COMPRESSION", Value::int64(Z_NO_COMPRESSION))
      .value("Z_NO_FLUSH", Value::int64(Z_NO_FLUSH))
      .value("Z_SYNC_FLUSH", Value::int64(Z_SYNC_FLUSH))
      .value("Z_FULL_FLUSH", Value::int64(Z_FULL_FLUSH))
      .value("Z_FINISH", Value::int64(Z_FINISH))
      .value("Z_DEFAULT_STRATEGY", Value::int64(Z_DEFAULT_STRATEGY))
      .value("DEF_MEM_LEVEL", Value::int64(kDefaultMemLevel))
      .value("MAX_WBITS", Value::int64(MAX_WBITS))
      .value("DEFLATED", Value::int64(Z_DEFLATED))
      .value("ZLIB_VERSION", Value::string(ZLIB_VERSION));
  runtime.register_module("zlib", builder.finish());
}

} // namespace xlang3
