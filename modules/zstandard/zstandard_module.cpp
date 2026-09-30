/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <zstd.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr const char* kCompressorType = "zstandard.backend_c.ZstdCompressor";
constexpr const char* kDecompressorType = "zstandard.backend_c.ZstdDecompressor";
constexpr const char* kDecompressObjType = "zstandard.backend_c.ZstdDecompressionObj";

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value error_class = x3_value_invalid();
  X3Value compressor_class = x3_value_invalid();
  X3Value decompressor_class = x3_value_invalid();
  X3Value decompress_obj_class = x3_value_invalid();
};

struct CompressorData {
  int level = ZSTD_CLEVEL_DEFAULT;
  bool write_content_size = true;
};
struct DecompressorData {};
struct DecompressObjData {
  ZSTD_DCtx* context = nullptr;
  bool eof = false;
  std::vector<unsigned char> unused_data;
  ~DecompressObjData() { ZSTD_freeDCtx(context); }
};

void cleanup_compressor(void* data) { delete static_cast<CompressorData*>(data); }
void cleanup_decompressor(void* data) { delete static_cast<DecompressorData*>(data); }
void cleanup_decompress_obj(void* data) { delete static_cast<DecompressObjData*>(data); }
void cleanup_package(void* data) {
  auto* state = static_cast<PackageState*>(data);
  if (!state) return;
  for (X3Value value : {state->error_class, state->compressor_class,
                        state->decompressor_class, state->decompress_obj_class})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  delete state;
}

X3Status type_error(PackageState* state, X3CallContext* call,
                    const char* message) {
  return state->host->raise_class_error(call, "TypeError", message);
}

X3Status zstd_error(PackageState* state, X3CallContext* call,
                    const std::string& message) {
  return state->host->raise_error(call, state->error_class, message.c_str());
}

bool integer_value(X3Value value, int64_t* result) {
  if (value.tag == X3_TAG_INT64) {
    *result = value.as.i64;
    return true;
  }
  if (value.tag == X3_TAG_UINT64 &&
      value.as.u64 <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    *result = static_cast<int64_t>(value.as.u64);
    return true;
  }
  return false;
}

X3Status compress_bytes(PackageState* state, X3CallContext* call,
                        X3Runtime* runtime, X3Value input, int level,
                        bool write_content_size, X3Value* result) {
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, input, 0, &buffer, &info) !=
      X3_STATUS_OK)
    return type_error(state, call, "a bytes-like object is required");
  if (info.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    state->host->buffer_release(buffer);
    return zstd_error(state, call, "input is too large");
  }
  const size_t bound = ZSTD_compressBound(static_cast<size_t>(info.size));
  if (ZSTD_isError(bound)) {
    state->host->buffer_release(buffer);
    return zstd_error(state, call, ZSTD_getErrorName(bound));
  }
  try {
    std::vector<unsigned char> output(bound);
    std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> context(
        ZSTD_createCCtx(), ZSTD_freeCCtx);
    if (!context) {
      state->host->buffer_release(buffer);
      return state->host->raise_class_error(call, "MemoryError",
                                             "zstd compressor allocation failed");
    }
    const size_t level_status = ZSTD_CCtx_setParameter(
        context.get(), ZSTD_c_compressionLevel, level);
    const size_t size_status = ZSTD_CCtx_setParameter(
        context.get(), ZSTD_c_contentSizeFlag, write_content_size ? 1 : 0);
    if (ZSTD_isError(level_status) || ZSTD_isError(size_status)) {
      state->host->buffer_release(buffer);
      return zstd_error(state, call, ZSTD_getErrorName(
          ZSTD_isError(level_status) ? level_status : size_status));
    }
    const size_t written = ZSTD_compress2(context.get(), output.data(),
        output.size(), info.data, static_cast<size_t>(info.size));
    state->host->buffer_release(buffer);
    if (ZSTD_isError(written))
      return zstd_error(state, call, ZSTD_getErrorName(written));
    *result = state->host->value_bytes(runtime, output.data(), written);
    return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
  } catch (const std::bad_alloc&) {
    state->host->buffer_release(buffer);
    return state->host->raise_class_error(call, "MemoryError",
                                           "zstd output allocation failed");
  }
}

X3Status decompress_bytes(PackageState* state, X3CallContext* call,
                          X3Runtime* runtime, X3Value input,
                          uint64_t max_output_size, X3Value* result) {
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, input, 0, &buffer, &info) !=
      X3_STATUS_OK)
    return type_error(state, call, "a bytes-like object is required");
  if (info.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    state->host->buffer_release(buffer);
    return zstd_error(state, call, "input is too large");
  }
  const size_t size = static_cast<size_t>(info.size);
  const unsigned long long content_size =
      ZSTD_getFrameContentSize(info.data, size);
  if (content_size == ZSTD_CONTENTSIZE_ERROR) {
    state->host->buffer_release(buffer);
    return zstd_error(state, call,
                      "error determining content size from frame header");
  }
  if (content_size == ZSTD_CONTENTSIZE_UNKNOWN && max_output_size == 0) {
    state->host->buffer_release(buffer);
    return zstd_error(state, call,
                      "could not determine content size in frame header");
  }
  const size_t frame_size = ZSTD_findFrameCompressedSize(info.data, size);
  if (ZSTD_isError(frame_size)) {
    state->host->buffer_release(buffer);
    return zstd_error(state, call,
                      "decompression error: did not decompress full frame");
  }
  const uint64_t capacity = content_size == ZSTD_CONTENTSIZE_UNKNOWN
      ? max_output_size : content_size;
  if (capacity > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    state->host->buffer_release(buffer);
    return zstd_error(state, call, "decompressed data is too large");
  }
  try {
    std::vector<unsigned char> output(static_cast<size_t>(capacity));
    const size_t written = ZSTD_decompress(output.data(), output.size(),
                                           info.data, frame_size);
    state->host->buffer_release(buffer);
    if (ZSTD_isError(written))
      return zstd_error(state, call,
                        "decompression error: did not decompress full frame");
    *result = state->host->value_bytes(runtime, output.data(), written);
    return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
  } catch (const std::bad_alloc&) {
    state->host->buffer_release(buffer);
    return state->host->raise_class_error(call, "MemoryError",
                                           "zstd output allocation failed");
  }
}

X3Status compress_function(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 2)
    return type_error(state, call, "compress() expects data and optional level");
  int64_t level = ZSTD_CLEVEL_DEFAULT;
  if (argc == 2 && !integer_value(args[1], &level))
    return type_error(state, call, "level must be an integer");
  if (level < std::numeric_limits<int>::min() ||
      level > std::numeric_limits<int>::max())
    return type_error(state, call, "level is out of range");
  return compress_bytes(state, call, runtime, args[0],
                        static_cast<int>(level), true, result);
}

X3Status compress_function_kw(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, const X3KeywordArg* kwargs,
                              uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc > 2) return type_error(state, call, "compress() takes at most 2 arguments");
  X3Value data = argc >= 1 ? args[0] : x3_value_invalid();
  X3Value level = argc >= 2 ? args[1] : x3_value_int64(ZSTD_CLEVEL_DEFAULT);
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name ? kwargs[i].name : "";
    if (name == "data" && data.tag == X3_TAG_INVALID)
      data = kwargs[i].value;
    else if (name == "level" && argc < 2)
      level = kwargs[i].value;
    else
      return type_error(state, call, "unexpected or duplicate compress argument");
  }
  if (data.tag == X3_TAG_INVALID)
    return type_error(state, call, "compress() missing data");
  const X3Value positional[] = {data, level};
  return compress_function(call, runtime, user_data, positional, 2, result);
}

X3Status decompress_function(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 2)
    return type_error(state, call,
                      "decompress() expects data and optional max_output_size");
  int64_t limit = 0;
  if (argc == 2 && (!integer_value(args[1], &limit) || limit < 0))
    return type_error(state, call,
                      "max_output_size must be a non-negative integer");
  return decompress_bytes(state, call, runtime, args[0],
                          static_cast<uint64_t>(limit), result);
}

X3Status decompress_function_kw(X3CallContext* call, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, const X3KeywordArg* kwargs,
                                uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc > 2) return type_error(state, call, "decompress() takes at most 2 arguments");
  X3Value data = argc >= 1 ? args[0] : x3_value_invalid();
  X3Value limit = argc >= 2 ? args[1] : x3_value_int64(0);
  bool has_limit = argc >= 2;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name ? kwargs[i].name : "";
    if (name == "data" && data.tag == X3_TAG_INVALID)
      data = kwargs[i].value;
    else if (name == "max_output_size" && !has_limit)
      limit = kwargs[i].value, has_limit = true;
    else
      return type_error(state, call, "unexpected or duplicate decompress argument");
  }
  if (data.tag == X3_TAG_INVALID)
    return type_error(state, call, "decompress() missing data");
  const X3Value positional[] = {data, limit};
  return decompress_function(call, runtime, user_data, positional, 2, result);
}

X3Status compressor_init(X3CallContext* call, X3Runtime*, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 2)
    return type_error(state, call, "ZstdCompressor accepts optional level");
  int64_t level = ZSTD_CLEVEL_DEFAULT;
  if (argc == 2 && !integer_value(args[1], &level))
    return type_error(state, call, "level must be an integer");
  if (level < std::numeric_limits<int>::min() ||
      level > std::numeric_limits<int>::max())
    return type_error(state, call, "level is out of range");
  auto data = std::make_unique<CompressorData>();
  data->level = static_cast<int>(level);
  if (state->host->instance_set_native_data(
          args[0], kCompressorType, data.get(), cleanup_compressor) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status compressor_init_kw(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, const X3KeywordArg* kwargs,
                            uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 2)
    return type_error(state, call, "ZstdCompressor accepts optional level");
  X3Value level = argc == 2 ? args[1] : x3_value_int64(ZSTD_CLEVEL_DEFAULT);
  bool has_level = argc == 2;
  bool write_content_size = true;
  bool has_content_size = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name ? kwargs[i].name : "";
    if (name == "level" && !has_level)
      level = kwargs[i].value, has_level = true;
    else if (name == "write_content_size" && !has_content_size &&
             kwargs[i].value.tag == X3_TAG_BOOL) {
      write_content_size = kwargs[i].value.as.b != 0;
      has_content_size = true;
    } else
      return type_error(state, call,
                        "unsupported or duplicate ZstdCompressor option");
  }
  const X3Value positional[] = {args[0], level};
  X3Status status = compressor_init(call, runtime, user_data,
                                    positional, 2, result);
  if (status == X3_STATUS_OK) {
    auto* data = static_cast<CompressorData*>(
        state->host->instance_get_native_data(args[0], kCompressorType));
    if (data) data->write_content_size = write_content_size;
  }
  return status;
}

X3Status compressor_compress(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2)
    return type_error(state, call, "ZstdCompressor.compress() expects data");
  auto* data = static_cast<CompressorData*>(state->host->instance_get_native_data(
      args[0], kCompressorType));
  if (!data) return type_error(state, call, "expected ZstdCompressor");
  return compress_bytes(state, call, runtime, args[1], data->level,
                        data->write_content_size, result);
}

X3Status decompressor_init(X3CallContext* call, X3Runtime*, void* user_data,
                           const X3Value* args, uint32_t argc,
                           X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1)
    return type_error(state, call, "ZstdDecompressor accepts no arguments");
  auto data = std::make_unique<DecompressorData>();
  if (state->host->instance_set_native_data(
          args[0], kDecompressorType, data.get(), cleanup_decompressor) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status decompressor_decompress(X3CallContext* call, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 2 || argc > 3)
    return type_error(state, call,
                      "ZstdDecompressor.decompress() expects data");
  if (!state->host->instance_get_native_data(args[0], kDecompressorType))
    return type_error(state, call, "expected ZstdDecompressor");
  int64_t limit = 0;
  if (argc == 3 && (!integer_value(args[2], &limit) || limit < 0))
    return type_error(state, call,
                      "max_output_size must be a non-negative integer");
  return decompress_bytes(state, call, runtime, args[1],
                          static_cast<uint64_t>(limit), result);
}

X3Status decompressor_decompress_kw(X3CallContext* call, X3Runtime* runtime,
                                    void* user_data, const X3Value* args,
                                    uint32_t argc, const X3KeywordArg* kwargs,
                                    uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 3)
    return type_error(state, call, "ZstdDecompressor.decompress() arguments");
  X3Value data = argc >= 2 ? args[1] : x3_value_invalid();
  X3Value limit = argc >= 3 ? args[2] : x3_value_int64(0);
  bool has_limit = argc >= 3;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name ? kwargs[i].name : "";
    if (name == "data" && data.tag == X3_TAG_INVALID)
      data = kwargs[i].value;
    else if (name == "max_output_size" && !has_limit)
      limit = kwargs[i].value, has_limit = true;
    else
      return type_error(state, call,
                        "unexpected or duplicate decompress argument");
  }
  if (data.tag == X3_TAG_INVALID)
    return type_error(state, call, "decompress() missing data");
  const X3Value positional[] = {args[0], data, limit};
  return decompressor_decompress(call, runtime, user_data,
                                 positional, 3, result);
}

X3Status decompressor_decompressobj(X3CallContext* call, X3Runtime* runtime,
                                    void* user_data, const X3Value* args,
                                    uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1 || !state->host->instance_get_native_data(args[0], kDecompressorType))
    return type_error(state, call, "ZstdDecompressor.decompressobj() expects no arguments");
  auto data = std::make_unique<DecompressObjData>();
  data->context = ZSTD_createDCtx();
  if (!data->context)
    return state->host->raise_class_error(call, "MemoryError", "zstd decompressor allocation failed");
  X3Value instance = state->host->value_instance(runtime, state->decompress_obj_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(instance, kDecompressObjType,
                                             data.get(), cleanup_decompress_obj) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status decompress_obj_decompress(X3CallContext* call, X3Runtime* runtime,
                                   void* user_data, const X3Value* args,
                                   uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(state, call, "decompress() expects data");
  auto* data = static_cast<DecompressObjData*>(
      state->host->instance_get_native_data(args[0], kDecompressObjType));
  if (!data) return type_error(state, call, "expected ZstdDecompressionObj");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &buffer, &info) != X3_STATUS_OK)
    return type_error(state, call, "a bytes-like object is required");
  if (info.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    state->host->buffer_release(buffer);
    return zstd_error(state, call, "input is too large");
  }
  try {
    if (data->eof) {
      const auto* start = static_cast<const unsigned char*>(info.data);
      data->unused_data.insert(data->unused_data.end(), start, start + info.size);
      state->host->buffer_release(buffer);
      *result = state->host->value_bytes(runtime, nullptr, 0);
      return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
    }
    ZSTD_inBuffer input{info.data, static_cast<size_t>(info.size), 0};
    std::vector<unsigned char> output;
    unsigned char chunk[65536];
    while (input.pos < input.size) {
      ZSTD_outBuffer out{chunk, sizeof(chunk), 0};
      const size_t status = ZSTD_decompressStream(data->context, &out, &input);
      if (ZSTD_isError(status)) {
        state->host->buffer_release(buffer);
        return zstd_error(state, call,
                          std::string("zstd decompressor error: ") + ZSTD_getErrorName(status));
      }
      output.insert(output.end(), chunk, chunk + out.pos);
      if (status == 0) {
        data->eof = true;
        const auto* start = static_cast<const unsigned char*>(info.data);
        data->unused_data.assign(start + input.pos, start + input.size);
        break;
      }
      if (out.pos == 0 && input.pos == 0) break;
    }
    state->host->buffer_release(buffer);
    *result = state->host->value_bytes(runtime, output.data(), output.size());
    return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
  } catch (const std::bad_alloc&) {
    state->host->buffer_release(buffer);
    return state->host->raise_class_error(call, "MemoryError", "zstd output allocation failed");
  }
}

X3Status decompress_obj_flush(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1 || !state->host->instance_get_native_data(args[0], kDecompressObjType))
    return type_error(state, call, "flush() expects no arguments");
  *result = state->host->value_bytes(runtime, nullptr, 0);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status decompress_obj_eof(X3CallContext* call, X3Runtime*,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, call, "eof getter arguments");
  auto* data = static_cast<DecompressObjData*>(
      state->host->instance_get_native_data(args[0], kDecompressObjType));
  if (!data) return type_error(state, call, "expected ZstdDecompressionObj");
  *result = x3_value_bool(data->eof);
  return X3_STATUS_OK;
}

X3Status decompress_obj_unused_data(X3CallContext* call, X3Runtime* runtime,
                                    void* user_data, const X3Value* args,
                                    uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, call, "unused_data getter arguments");
  auto* data = static_cast<DecompressObjData*>(
      state->host->instance_get_native_data(args[0], kDecompressObjType));
  if (!data) return type_error(state, call, "expected ZstdDecompressionObj");
  *result = state->host->value_bytes(runtime, data->unused_data.data(),
                                     data->unused_data.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "zstandard.backend_c", &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value exception_base = x3_value_invalid();
  if (host->builtin_value(host, "Exception", &exception_base) != X3_STATUS_OK ||
      host->create_class(host, "ZstdError", nullptr, 0,
                         &state->error_class) != X3_STATUS_OK ||
      host->class_set_base(state->error_class, exception_base) != X3_STATUS_OK ||
      host->module_add_value(module, "ZstdError", state->error_class) !=
          X3_STATUS_OK)
    return X3_STATUS_ERROR;
  host->value_release(exception_base);
  const X3NativeFunctionDef compressor_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", compressor_init,
       state, 1, 2, 0, compressor_init_kw},
      {sizeof(X3NativeFunctionDef), "compress", compressor_compress,
       state, 2, 2, 0, nullptr},
  };
  const X3NativeFunctionDef decompressor_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", decompressor_init,
       state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "decompress", decompressor_decompress,
       state, 2, 3, 0, decompressor_decompress_kw},
      {sizeof(X3NativeFunctionDef), "decompressobj", decompressor_decompressobj,
       state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef decompress_obj_methods[] = {
      {sizeof(X3NativeFunctionDef), "decompress", decompress_obj_decompress,
       state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "flush", decompress_obj_flush,
       state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(module, "ZstdCompressor", compressor_methods, 2,
                             &state->compressor_class) != X3_STATUS_OK ||
      host->module_add_class(module, "ZstdDecompressor",
                             decompressor_methods, 3,
                             &state->decompressor_class) != X3_STATUS_OK ||
      host->create_class(host, "ZstdDecompressionObj", decompress_obj_methods, 2,
                         &state->decompress_obj_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  for (const auto& property : {
           std::pair<const char*, X3NativeFn>{"eof", decompress_obj_eof},
           {"unused_data", decompress_obj_unused_data}}) {
    X3Value descriptor = x3_value_invalid();
    if (host->property_create(host->runtime, property.first, property.second,
                              nullptr, state, &descriptor) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(
        state->decompress_obj_class, property.first, descriptor);
    host->value_release(descriptor);
    if (status != X3_STATUS_OK) return X3_STATUS_ERROR;
  }
  const X3NativeFunctionDef functions[] = {
      {sizeof(X3NativeFunctionDef), "compress", compress_function,
       state, 0, 2, 0, compress_function_kw},
      {sizeof(X3NativeFunctionDef), "decompress", decompress_function,
       state, 0, 2, 0, decompress_function_kw},
  };
  for (const auto& function : functions)
    if (host->module_add_function(module, &function) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  X3Value version = host->value_string(host->runtime, "0.25.0");
  const X3Status version_status = host->module_add_value(module, "__version__",
                                                          version);
  host->value_release(version);
  return version_status;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version =
    X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "zstandard.backend_c");
  host->package_set_metadata(host, "version", "0.25.0");
  return register_module(host);
}
