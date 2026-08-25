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

#include <limits>
#include <zlib.h>

namespace xlang3 {

namespace {

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

} // namespace

void register_zlib_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "zlib");
  builder.function("compress", zlib_compress)
      .function("decompress", zlib_decompress)
      .function("crc32", zlib_crc32)
      .function("adler32", zlib_adler32)
      .value("Z_DEFAULT_COMPRESSION", Value::int64(Z_DEFAULT_COMPRESSION))
      .value("Z_BEST_SPEED", Value::int64(Z_BEST_SPEED))
      .value("Z_BEST_COMPRESSION", Value::int64(Z_BEST_COMPRESSION))
      .value("Z_NO_COMPRESSION", Value::int64(Z_NO_COMPRESSION))
      .value("MAX_WBITS", Value::int64(MAX_WBITS))
      .value("DEFLATED", Value::int64(Z_DEFLATED))
      .value("ZLIB_VERSION", Value::string(ZLIB_VERSION));
  runtime.register_module("zlib", builder.finish());
}

} // namespace xlang3
