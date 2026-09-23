/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <zstd.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kCompressorType = "_zstd.ZstdCompressor";
constexpr const char* kDecompressorType = "_zstd.ZstdDecompressor";
constexpr const char* kDictType = "_zstd.ZstdDict";

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value compressor_class = x3_value_invalid();
  X3Value decompressor_class = x3_value_invalid();
  X3Value dict_class = x3_value_invalid();
  X3Value error_class = x3_value_invalid();
  X3Value compression_parameter_class = x3_value_invalid();
  X3Value decompression_parameter_class = x3_value_invalid();
};

struct CompressorState {
  ZSTD_CCtx* context = nullptr;
  int64_t last_mode = 2;
  ~CompressorState() { ZSTD_freeCCtx(context); }
};

struct DecompressorState {
  ZSTD_DCtx* context = nullptr;
  bool eof = false;
  bool needs_input = true;
  std::string unused_data;
  ~DecompressorState() { ZSTD_freeDCtx(context); }
};

struct DictState {
  std::string content;
  bool is_raw = false;
};

void cleanup_compressor(void* pointer) { delete static_cast<CompressorState*>(pointer); }
void cleanup_decompressor(void* pointer) { delete static_cast<DecompressorState*>(pointer); }
void cleanup_dict(void* pointer) { delete static_cast<DictState*>(pointer); }
void cleanup_package(void* pointer) {
  auto* state = static_cast<PackageState*>(pointer);
  if (!state) return;
  state->host->value_release(state->compressor_class);
  state->host->value_release(state->decompressor_class);
  state->host->value_release(state->dict_class);
  state->host->value_release(state->error_class);
  state->host->value_release(state->compression_parameter_class);
  state->host->value_release(state->decompression_parameter_class);
  delete state;
}

X3Status type_error(PackageState* state, X3CallContext* call, const char* message) {
  return state->host->raise_class_error(call, "TypeError", message);
}

X3Status zstd_error(PackageState* state, X3CallContext* call,
                    const char* operation, size_t code) {
  const std::string message = std::string(operation) + ": " + ZSTD_getErrorName(code);
  return state->host->raise_error(call, state->error_class, message.c_str());
}

bool signed_integer(X3Value value, int64_t& output) {
  if (value.tag == X3_TAG_INT64) { output = value.as.i64; return true; }
  if (value.tag == X3_TAG_UINT64 &&
      value.as.u64 <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    output = static_cast<int64_t>(value.as.u64);
    return true;
  }
  return false;
}

bool bytes_view(PackageState* state, X3Runtime* runtime, X3Value value,
                const void*& data, size_t& size) {
  uint64_t count = 0;
  if (state->host->value_bytes_data(runtime, value, &data, &count) != X3_STATUS_OK ||
      count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return false;
  size = static_cast<size_t>(count);
  return true;
}

bool parameter_value(const X3KeywordArg* kwargs, uint32_t kwargc,
                     const char* name, X3Value& value) {
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (kwargs[index].name != nullptr && std::string(kwargs[index].name) == name) {
      value = kwargs[index].value;
      return true;
    }
  }
  return false;
}

X3Status apply_compression_options(PackageState* state, X3CallContext* call,
                                   X3Runtime* runtime, ZSTD_CCtx* context,
                                   X3Value options) {
  if (options.tag == X3_TAG_NONE || options.tag == X3_TAG_INVALID) return X3_STATUS_OK;
  uint64_t count = 0;
  if (state->host->len(runtime, options, &count) != X3_STATUS_OK)
    return type_error(state, call, "options must be a mapping");
  for (uint64_t index = 0; index < count; ++index) {
    X3Value key = x3_value_invalid();
    X3Value value = x3_value_invalid();
    if (state->host->dict_get_entry(runtime, options, index, &key, &value) != X3_STATUS_OK)
      return type_error(state, call, "options must be a dict");
    int64_t parameter = 0, setting = 0;
    const bool valid = signed_integer(key, parameter) && signed_integer(value, setting) &&
        parameter >= std::numeric_limits<int>::min() && parameter <= std::numeric_limits<int>::max() &&
        setting >= std::numeric_limits<int>::min() && setting <= std::numeric_limits<int>::max();
    state->host->value_release(key);
    state->host->value_release(value);
    if (!valid) return type_error(state, call, "compression options must map integer parameters to integers");
    const size_t code = ZSTD_CCtx_setParameter(
        context, static_cast<ZSTD_cParameter>(parameter), static_cast<int>(setting));
    if (ZSTD_isError(code)) return zstd_error(state, call, "invalid compression option", code);
  }
  return X3_STATUS_OK;
}

X3Status apply_decompression_options(PackageState* state, X3CallContext* call,
                                     X3Runtime* runtime, ZSTD_DCtx* context,
                                     X3Value options) {
  if (options.tag == X3_TAG_NONE || options.tag == X3_TAG_INVALID) return X3_STATUS_OK;
  uint64_t count = 0;
  if (state->host->len(runtime, options, &count) != X3_STATUS_OK)
    return type_error(state, call, "options must be a mapping");
  for (uint64_t index = 0; index < count; ++index) {
    X3Value key = x3_value_invalid();
    X3Value value = x3_value_invalid();
    if (state->host->dict_get_entry(runtime, options, index, &key, &value) != X3_STATUS_OK)
      return type_error(state, call, "options must be a dict");
    int64_t parameter = 0, setting = 0;
    const bool valid = signed_integer(key, parameter) && signed_integer(value, setting) &&
        parameter >= std::numeric_limits<int>::min() && parameter <= std::numeric_limits<int>::max() &&
        setting >= std::numeric_limits<int>::min() && setting <= std::numeric_limits<int>::max();
    state->host->value_release(key);
    state->host->value_release(value);
    if (!valid) return type_error(state, call, "decompression options must map integer parameters to integers");
    const size_t code = ZSTD_DCtx_setParameter(
        context, static_cast<ZSTD_dParameter>(parameter), static_cast<int>(setting));
    if (ZSTD_isError(code)) return zstd_error(state, call, "invalid decompression option", code);
  }
  return X3_STATUS_OK;
}

X3Status dict_init_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                      const X3Value* args, uint32_t argc,
                      const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2 || kwargc > 1) return type_error(state, call, "ZstdDict() expects dictionary bytes");
  const void* data = nullptr;
  size_t size = 0;
  if (!bytes_view(state, runtime, args[1], data, size))
    return type_error(state, call, "ZstdDict content must be bytes-like");
  auto native = std::make_unique<DictState>();
  native->content.assign(static_cast<const char*>(data), size);
  X3Value raw = x3_value_invalid();
  if (parameter_value(kwargs, kwargc, "is_raw", raw)) native->is_raw = raw.tag == X3_TAG_BOOL && raw.as.b;
  if (state->host->instance_set_native_data(args[0], kDictType, native.get(), cleanup_dict) != X3_STATUS_OK)
    return state->host->raise_class_error(call, "MemoryError", "cannot allocate ZstdDict");
  const bool is_raw = native->is_raw;
  native.release();
  if (state->host->set_attr(runtime, args[0], "dict_content", args[1]) != X3_STATUS_OK ||
      state->host->set_attr(runtime, args[0], "is_raw", x3_value_bool(is_raw)) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status dict_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  return dict_init_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status compressor_init_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                            const X3Value* args, uint32_t argc,
                            const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1 || kwargc > 3)
    return type_error(state, call, "ZstdCompressor() accepts level, options, and zstd_dict keywords");
  auto native = std::make_unique<CompressorState>();
  native->context = ZSTD_createCCtx();
  if (!native->context)
    return state->host->raise_class_error(call, "MemoryError", "cannot allocate Zstandard compressor");
  X3Value level = x3_value_invalid();
  if (parameter_value(kwargs, kwargc, "level", level) && level.tag != X3_TAG_NONE) {
    int64_t integer = 0;
    if (!signed_integer(level, integer) || integer < std::numeric_limits<int>::min() ||
        integer > std::numeric_limits<int>::max())
      return type_error(state, call, "compression level must be an integer");
    const size_t code = ZSTD_CCtx_setParameter(
        native->context, ZSTD_c_compressionLevel, static_cast<int>(integer));
    if (ZSTD_isError(code)) return zstd_error(state, call, "invalid compression level", code);
  }
  X3Value options = x3_value_invalid();
  if (parameter_value(kwargs, kwargc, "options", options) &&
      apply_compression_options(state, call, runtime, native->context, options) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value dictionary = x3_value_invalid();
  if (parameter_value(kwargs, kwargc, "zstd_dict", dictionary) && dictionary.tag != X3_TAG_NONE) {
    auto* source = static_cast<DictState*>(state->host->instance_get_native_data(dictionary, kDictType));
    if (!source) return type_error(state, call, "zstd_dict must be a ZstdDict");
    const size_t code = ZSTD_CCtx_loadDictionary(
        native->context, source->content.data(), source->content.size());
    if (ZSTD_isError(code)) return zstd_error(state, call, "invalid dictionary", code);
  }
  if (state->host->instance_set_native_data(args[0], kCompressorType, native.get(), cleanup_compressor) != X3_STATUS_OK)
    return state->host->raise_class_error(call, "MemoryError", "cannot allocate ZstdCompressor");
  native.release();
  if (state->host->set_attr(runtime, args[0], "last_mode", x3_value_int64(2)) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status compressor_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  return compressor_init_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status compressor_stream(PackageState* state, X3CallContext* call,
                           X3Runtime* runtime, X3Value self,
                           const void* data, size_t size, int64_t mode,
                           X3Value* result) {
  auto* native = static_cast<CompressorState*>(state->host->instance_get_native_data(self, kCompressorType));
  if (!native) return type_error(state, call, "invalid ZstdCompressor");
  if (mode < 0 || mode > 2) return type_error(state, call, "invalid compression mode");
  ZSTD_inBuffer input{data, size, 0};
  std::string output;
  const auto directive = static_cast<ZSTD_EndDirective>(mode);
  for (;;) {
    std::string chunk(ZSTD_CStreamOutSize(), '\0');
    ZSTD_outBuffer buffer{chunk.data(), chunk.size(), 0};
    const size_t remaining = ZSTD_compressStream2(native->context, &buffer, &input, directive);
    if (ZSTD_isError(remaining)) return zstd_error(state, call, "cannot compress Zstandard data", remaining);
    output.append(chunk.data(), buffer.pos);
    if (input.pos == input.size &&
        (remaining == 0 || (mode == 0 && buffer.pos < buffer.size))) break;
  }
  native->last_mode = mode;
  if (state->host->set_attr(runtime, self, "last_mode", x3_value_int64(mode)) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  *result = state->host->value_bytes(runtime, output.data(), output.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status compressor_compress_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                                const X3Value* args, uint32_t argc,
                                const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2 || kwargc > 1)
    return type_error(state, call, "ZstdCompressor.compress() expects data and optional mode");
  const void* data = nullptr;
  size_t size = 0;
  if (!bytes_view(state, runtime, args[1], data, size))
    return type_error(state, call, "Zstandard input must be bytes-like");
  int64_t mode = 0;
  X3Value mode_value = x3_value_invalid();
  if (parameter_value(kwargs, kwargc, "mode", mode_value) && !signed_integer(mode_value, mode))
    return type_error(state, call, "compression mode must be an integer");
  return compressor_stream(state, call, runtime, args[0], data, size, mode, result);
}

X3Status compressor_compress(X3CallContext* call, X3Runtime* runtime, void* user_data,
                             const X3Value* args, uint32_t argc, X3Value* result) {
  return compressor_compress_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status compressor_flush_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                             const X3Value* args, uint32_t argc,
                             const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1 || kwargc > 1)
    return type_error(state, call, "ZstdCompressor.flush() accepts optional mode");
  int64_t mode = 2;
  X3Value mode_value = x3_value_invalid();
  if (parameter_value(kwargs, kwargc, "mode", mode_value) && !signed_integer(mode_value, mode))
    return type_error(state, call, "compression mode must be an integer");
  return compressor_stream(state, call, runtime, args[0], nullptr, 0, mode, result);
}

X3Status compressor_flush(X3CallContext* call, X3Runtime* runtime, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  return compressor_flush_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status decompressor_init_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                              const X3Value* args, uint32_t argc,
                              const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1 || kwargc > 2)
    return type_error(state, call, "ZstdDecompressor() accepts options and zstd_dict keywords");
  auto native = std::make_unique<DecompressorState>();
  native->context = ZSTD_createDCtx();
  if (!native->context)
    return state->host->raise_class_error(call, "MemoryError", "cannot allocate Zstandard decompressor");
  X3Value options = x3_value_invalid();
  if (parameter_value(kwargs, kwargc, "options", options) &&
      apply_decompression_options(state, call, runtime, native->context, options) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value dictionary = x3_value_invalid();
  if (parameter_value(kwargs, kwargc, "zstd_dict", dictionary) && dictionary.tag != X3_TAG_NONE) {
    auto* source = static_cast<DictState*>(state->host->instance_get_native_data(dictionary, kDictType));
    if (!source) return type_error(state, call, "zstd_dict must be a ZstdDict");
    const size_t code = ZSTD_DCtx_loadDictionary(
        native->context, source->content.data(), source->content.size());
    if (ZSTD_isError(code)) return zstd_error(state, call, "invalid dictionary", code);
  }
  if (state->host->instance_set_native_data(args[0], kDecompressorType, native.get(), cleanup_decompressor) != X3_STATUS_OK)
    return state->host->raise_class_error(call, "MemoryError", "cannot allocate ZstdDecompressor");
  native.release();
  if (state->host->set_attr(runtime, args[0], "eof", x3_value_bool(0)) != X3_STATUS_OK ||
      state->host->set_attr(runtime, args[0], "needs_input", x3_value_bool(1)) != X3_STATUS_OK ||
      state->host->set_attr(runtime, args[0], "unused_data", state->host->value_bytes(runtime, "", 0)) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status decompressor_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                           const X3Value* args, uint32_t argc, X3Value* result) {
  return decompressor_init_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status decompressor_decompress(X3CallContext* call, X3Runtime* runtime, void* user_data,
                                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2)
    return type_error(state, call, "ZstdDecompressor.decompress() expects bytes");
  auto* native = static_cast<DecompressorState*>(state->host->instance_get_native_data(args[0], kDecompressorType));
  if (!native) return type_error(state, call, "invalid ZstdDecompressor");
  if (native->eof)
    return state->host->raise_class_error(call, "EOFError", "Already at the end of a Zstandard frame.");
  const void* data = nullptr;
  size_t size = 0;
  if (!bytes_view(state, runtime, args[1], data, size))
    return type_error(state, call, "Zstandard input must be bytes-like");
  ZSTD_inBuffer input{data, size, 0};
  std::string output;
  bool attempt = input.size != 0 || !native->needs_input;
  while (attempt) {
    std::string chunk(ZSTD_DStreamOutSize(), '\0');
    ZSTD_outBuffer buffer{chunk.data(), chunk.size(), 0};
    const size_t previous_input = input.pos;
    const size_t remaining = ZSTD_decompressStream(native->context, &buffer, &input);
    if (ZSTD_isError(remaining)) return zstd_error(state, call, "Unable to decompress Zstandard data", remaining);
    output.append(chunk.data(), buffer.pos);
    if (remaining == 0) {
      native->eof = true;
      native->unused_data.assign(size > input.pos
          ? static_cast<const char*>(data) + input.pos : "", size - input.pos);
      break;
    }
    native->needs_input = buffer.pos < buffer.size;
    if (input.pos == input.size && native->needs_input) break;
    if (buffer.pos == 0 && input.pos == previous_input) break;
    attempt = true;
  }
  if (state->host->set_attr(runtime, args[0], "eof", x3_value_bool(native->eof)) != X3_STATUS_OK ||
      state->host->set_attr(runtime, args[0], "needs_input", x3_value_bool(!native->eof && native->needs_input)) != X3_STATUS_OK ||
      state->host->set_attr(runtime, args[0], "unused_data",
          state->host->value_bytes(runtime, native->unused_data.data(), native->unused_data.size())) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  *result = state->host->value_bytes(runtime, output.data(), output.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status set_parameter_types(X3CallContext* call, X3Runtime*, void* user_data,
                             const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(state, call, "set_parameter_types() expects two enum classes");
  state->host->value_release(state->compression_parameter_class);
  state->host->value_release(state->decompression_parameter_class);
  state->compression_parameter_class = args[0];
  state->decompression_parameter_class = args[1];
  state->host->value_retain(args[0]);
  state->host->value_retain(args[1]);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status get_frame_size(X3CallContext* call, X3Runtime* runtime, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, call, "get_frame_size() expects frame bytes");
  const void* data = nullptr;
  size_t size = 0;
  if (!bytes_view(state, runtime, args[0], data, size))
    return type_error(state, call, "frame must be bytes-like");
  const size_t frame_size = ZSTD_findFrameCompressedSize(data, size);
  if (ZSTD_isError(frame_size)) return zstd_error(state, call, "invalid Zstandard frame", frame_size);
  *result = x3_value_int64(static_cast<int64_t>(frame_size));
  return X3_STATUS_OK;
}

void method(X3NativeFunctionDef& definition, const char* name,
            X3NativeFn callback, PackageState* state,
            X3NativeKeywordFn keyword_callback = nullptr) {
  definition = {};
  definition.size = sizeof(definition);
  definition.name = name;
  definition.callback = callback;
  definition.keyword_callback = keyword_callback;
  definition.user_data = state;
}

X3Status add_int(PackageState* state, X3Module* module, const char* name, int64_t value) {
  return state->host->module_add_value(module, name, x3_value_int64(value));
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "_zstd", &module) != X3_STATUS_OK) return X3_STATUS_ERROR;

  X3Value base_exception = x3_value_invalid();
  if (host->builtin_value(host, "Exception", &base_exception) != X3_STATUS_OK ||
      host->create_class(host, "ZstdError", nullptr, 0, &state->error_class) != X3_STATUS_OK ||
      host->class_set_base(state->error_class, base_exception) != X3_STATUS_OK ||
      host->module_add_value(module, "ZstdError", state->error_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  host->value_release(base_exception);

  X3NativeFunctionDef dict_methods[1]{};
  method(dict_methods[0], "__init__", dict_init, state, dict_init_kw);
  if (host->module_add_class(module, "ZstdDict", dict_methods, 1, &state->dict_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;

  X3NativeFunctionDef compressor_methods[3]{};
  method(compressor_methods[0], "__init__", compressor_init, state, compressor_init_kw);
  method(compressor_methods[1], "compress", compressor_compress, state, compressor_compress_kw);
  method(compressor_methods[2], "flush", compressor_flush, state, compressor_flush_kw);
  if (host->module_add_class(module, "ZstdCompressor", compressor_methods, 3,
                             &state->compressor_class) != X3_STATUS_OK ||
      host->class_add_value(state->compressor_class, "CONTINUE", x3_value_int64(0)) != X3_STATUS_OK ||
      host->class_add_value(state->compressor_class, "FLUSH_BLOCK", x3_value_int64(1)) != X3_STATUS_OK ||
      host->class_add_value(state->compressor_class, "FLUSH_FRAME", x3_value_int64(2)) != X3_STATUS_OK)
    return X3_STATUS_ERROR;

  X3NativeFunctionDef decompressor_methods[2]{};
  method(decompressor_methods[0], "__init__", decompressor_init, state, decompressor_init_kw);
  method(decompressor_methods[1], "decompress", decompressor_decompress, state);
  if (host->module_add_class(module, "ZstdDecompressor", decompressor_methods, 2,
                             &state->decompressor_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;

  X3NativeFunctionDef functions[2]{};
  method(functions[0], "set_parameter_types", set_parameter_types, state);
  method(functions[1], "get_frame_size", get_frame_size, state);
  for (const auto& function : functions)
    if (host->module_add_function(module, &function) != X3_STATUS_OK) return X3_STATUS_ERROR;

  struct Constant { const char* name; int64_t value; };
  const Constant constants[] = {
      {"ZSTD_CLEVEL_DEFAULT", ZSTD_CLEVEL_DEFAULT},
      {"ZSTD_DStreamOutSize", static_cast<int64_t>(ZSTD_DStreamOutSize())},
      {"ZSTD_c_compressionLevel", ZSTD_c_compressionLevel},
      {"ZSTD_c_windowLog", ZSTD_c_windowLog},
      {"ZSTD_c_hashLog", ZSTD_c_hashLog},
      {"ZSTD_c_chainLog", ZSTD_c_chainLog},
      {"ZSTD_c_searchLog", ZSTD_c_searchLog},
      {"ZSTD_c_minMatch", ZSTD_c_minMatch},
      {"ZSTD_c_targetLength", ZSTD_c_targetLength},
      {"ZSTD_c_strategy", ZSTD_c_strategy},
      {"ZSTD_c_enableLongDistanceMatching", ZSTD_c_enableLongDistanceMatching},
      {"ZSTD_c_ldmHashLog", ZSTD_c_ldmHashLog},
      {"ZSTD_c_ldmMinMatch", ZSTD_c_ldmMinMatch},
      {"ZSTD_c_ldmBucketSizeLog", ZSTD_c_ldmBucketSizeLog},
      {"ZSTD_c_ldmHashRateLog", ZSTD_c_ldmHashRateLog},
      {"ZSTD_c_contentSizeFlag", ZSTD_c_contentSizeFlag},
      {"ZSTD_c_checksumFlag", ZSTD_c_checksumFlag},
      {"ZSTD_c_dictIDFlag", ZSTD_c_dictIDFlag},
      {"ZSTD_c_nbWorkers", ZSTD_c_nbWorkers},
      {"ZSTD_c_jobSize", ZSTD_c_jobSize},
      {"ZSTD_c_overlapLog", ZSTD_c_overlapLog},
      {"ZSTD_d_windowLogMax", ZSTD_d_windowLogMax},
      {"ZSTD_fast", ZSTD_fast}, {"ZSTD_dfast", ZSTD_dfast},
      {"ZSTD_greedy", ZSTD_greedy}, {"ZSTD_lazy", ZSTD_lazy},
      {"ZSTD_lazy2", ZSTD_lazy2}, {"ZSTD_btlazy2", ZSTD_btlazy2},
      {"ZSTD_btopt", ZSTD_btopt}, {"ZSTD_btultra", ZSTD_btultra},
      {"ZSTD_btultra2", ZSTD_btultra2},
  };
  for (const auto& constant : constants)
    if (add_int(state, module, constant.name, constant.value) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  if (host->module_add_value(module, "zstd_version", host->value_string(host->runtime, ZSTD_versionString())) != X3_STATUS_OK ||
      add_int(state, module, "zstd_version_number", ZSTD_versionNumber()) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}

} // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "_zstd");
  host->package_set_metadata(host, "version", ZSTD_versionString());
  return register_module(host);
}
