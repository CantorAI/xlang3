/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <brotli/decode.h>
#include <brotli/encode.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr const char* kCompressorType = "_brotli.Compressor";
constexpr const char* kDecompressorType = "_brotli.Decompressor";

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value error_class = x3_value_invalid();
  X3Value compressor_class = x3_value_invalid();
  X3Value decompressor_class = x3_value_invalid();
};

struct CompressorData {
  BrotliEncoderState* state = nullptr;
  bool finished = false;
  ~CompressorData() { if (state) BrotliEncoderDestroyInstance(state); }
};

struct DecompressorData {
  BrotliDecoderState* state = nullptr;
  bool finished = false;
  ~DecompressorData() { if (state) BrotliDecoderDestroyInstance(state); }
};

void cleanup_compressor(void* data) { delete static_cast<CompressorData*>(data); }
void cleanup_decompressor(void* data) { delete static_cast<DecompressorData*>(data); }
void cleanup_package(void* data) {
  auto* state = static_cast<PackageState*>(data);
  if (!state) return;
  for (X3Value value : {state->error_class, state->compressor_class,
                        state->decompressor_class})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  delete state;
}

X3Status type_error(PackageState* state, X3CallContext* call,
                    const char* message) {
  return state->host->raise_class_error(call, "TypeError", message);
}

X3Status brotli_error(PackageState* state, X3CallContext* call,
                      const char* message) {
  return state->host->raise_error(call, state->error_class, message);
}

bool as_integer(X3Value value, int* result) {
  if (value.tag == X3_TAG_INT64 &&
      value.as.i64 >= std::numeric_limits<int>::min() &&
      value.as.i64 <= std::numeric_limits<int>::max()) {
    *result = static_cast<int>(value.as.i64);
    return true;
  }
  return false;
}

X3Status bytes_result(PackageState* state, X3Runtime* runtime,
                      const std::vector<uint8_t>& data, X3Value* result) {
  *result = state->host->value_bytes(runtime, data.data(), data.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status encode(PackageState* package, X3CallContext* call,
                X3Runtime* runtime, CompressorData* compressor,
                X3Value input, BrotliEncoderOperation operation,
                X3Value* result) {
  if (compressor->finished)
    return brotli_error(package, call, "Compressor has finished");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (input.tag != X3_TAG_INVALID &&
      package->host->buffer_acquire(runtime, input, 0, &buffer, &info) !=
          X3_STATUS_OK)
    return type_error(package, call, "a bytes-like object is required");
  if (info.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    if (buffer) package->host->buffer_release(buffer);
    return brotli_error(package, call, "input is too large");
  }
  size_t available_in = static_cast<size_t>(info.size);
  const uint8_t* next_in = static_cast<const uint8_t*>(info.data);
  std::vector<uint8_t> output;
  BROTLI_BOOL ok = BROTLI_TRUE;
  do {
    std::array<uint8_t, 65536> chunk{};
    size_t available_out = chunk.size();
    uint8_t* next_out = chunk.data();
    ok = BrotliEncoderCompressStream(compressor->state, operation,
                                     &available_in, &next_in,
                                     &available_out, &next_out, nullptr);
    output.insert(output.end(), chunk.data(), next_out);
    if (!ok) break;
    if (operation == BROTLI_OPERATION_FINISH &&
        BrotliEncoderIsFinished(compressor->state)) break;
    if (operation != BROTLI_OPERATION_FINISH && available_in == 0 &&
        !BrotliEncoderHasMoreOutput(compressor->state)) break;
  } while (true);
  if (buffer) package->host->buffer_release(buffer);
  if (!ok) return brotli_error(package, call, "Brotli compression failed");
  if (operation == BROTLI_OPERATION_FINISH) compressor->finished = true;
  return bytes_result(package, runtime, output, result);
}

X3Status decode(PackageState* package, X3CallContext* call,
                X3Runtime* runtime, DecompressorData* decompressor,
                X3Value input, bool require_complete, X3Value* result) {
  if (decompressor->finished)
    return brotli_error(package, call, "Decompressor has finished");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (package->host->buffer_acquire(runtime, input, 0, &buffer, &info) !=
      X3_STATUS_OK)
    return type_error(package, call, "a bytes-like object is required");
  if (info.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    package->host->buffer_release(buffer);
    return brotli_error(package, call, "input is too large");
  }
  size_t available_in = static_cast<size_t>(info.size);
  const uint8_t* next_in = static_cast<const uint8_t*>(info.data);
  std::vector<uint8_t> output;
  BrotliDecoderResult status = BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT;
  do {
    std::array<uint8_t, 65536> chunk{};
    size_t available_out = chunk.size();
    uint8_t* next_out = chunk.data();
    status = BrotliDecoderDecompressStream(decompressor->state,
                                           &available_in, &next_in,
                                           &available_out, &next_out, nullptr);
    output.insert(output.end(), chunk.data(), next_out);
    if (status != BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) break;
  } while (true);
  package->host->buffer_release(buffer);
  if (status == BROTLI_DECODER_RESULT_ERROR ||
      (require_complete && status != BROTLI_DECODER_RESULT_SUCCESS))
    return brotli_error(package, call, "brotli: decoder failed");
  decompressor->finished = status == BROTLI_DECODER_RESULT_SUCCESS;
  return bytes_result(package, runtime, output, result);
}

X3Status compressor_init_kw(X3CallContext* call, X3Runtime*, void* user_data,
                            const X3Value* args, uint32_t argc,
                            const X3KeywordArg* kwargs, uint32_t kwargc,
                            X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 5)
    return type_error(package, call, "Compressor expects up to four options");
  constexpr const char* names[] = {"mode", "quality", "lgwin", "lgblock"};
  int options[] = {BROTLI_MODE_GENERIC, 11, 22, 0};
  bool seen[] = {false, false, false, false};
  for (uint32_t i = 1; i < argc; ++i) {
    if (!as_integer(args[i], &options[i - 1]))
      return type_error(package, call, "Compressor options must be integers");
    seen[i - 1] = true;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name ? kwargs[i].name : "";
    int option = -1;
    for (int j = 0; j < 4; ++j)
      if (name == names[j]) option = j;
    if (option < 0 || seen[option] ||
        !as_integer(kwargs[i].value, &options[option]))
      return type_error(package, call, "invalid or duplicate Compressor option");
    seen[option] = true;
  }
  if (options[0] < 0 || options[0] > 2 || options[1] < 0 || options[1] > 11 ||
      options[2] < 10 || options[2] > 24 ||
      (options[3] != 0 && (options[3] < 16 || options[3] > 24)))
    return brotli_error(package, call, "Invalid compression parameters");
  auto data = std::make_unique<CompressorData>();
  data->state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
  if (!data->state)
    return package->host->raise_class_error(call, "MemoryError",
                                             "Brotli encoder allocation failed");
  const BROTLI_BOOL ok =
      BrotliEncoderSetParameter(data->state, BROTLI_PARAM_MODE, options[0]) &&
      BrotliEncoderSetParameter(data->state, BROTLI_PARAM_QUALITY, options[1]) &&
      BrotliEncoderSetParameter(data->state, BROTLI_PARAM_LGWIN, options[2]) &&
      BrotliEncoderSetParameter(data->state, BROTLI_PARAM_LGBLOCK, options[3]);
  if (!ok) return brotli_error(package, call, "Invalid compression parameters");
  if (package->host->instance_set_native_data(args[0], kCompressorType,
          data.get(), cleanup_compressor) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status compressor_init(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result) {
  return compressor_init_kw(call, runtime, user_data, args, argc,
                            nullptr, 0, result);
}

X3Status compressor_process(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(package, call, "process() expects data");
  auto* data = static_cast<CompressorData*>(package->host->instance_get_native_data(
      args[0], kCompressorType));
  if (!data) return type_error(package, call, "expected Compressor");
  return encode(package, call, runtime, data, args[1],
                BROTLI_OPERATION_PROCESS, result);
}

X3Status compressor_finish(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, call, "finish() takes no arguments");
  auto* data = static_cast<CompressorData*>(package->host->instance_get_native_data(
      args[0], kCompressorType));
  if (!data) return type_error(package, call, "expected Compressor");
  return encode(package, call, runtime, data, x3_value_invalid(),
                BROTLI_OPERATION_FINISH, result);
}

X3Status decompressor_init(X3CallContext* call, X3Runtime*,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, call, "Decompressor takes no arguments");
  auto data = std::make_unique<DecompressorData>();
  data->state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
  if (!data->state)
    return package->host->raise_class_error(call, "MemoryError",
                                             "Brotli decoder allocation failed");
  if (package->host->instance_set_native_data(args[0], kDecompressorType,
          data.get(), cleanup_decompressor) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status decompressor_process(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(package, call, "process() expects data");
  auto* data = static_cast<DecompressorData*>(package->host->instance_get_native_data(
      args[0], kDecompressorType));
  if (!data) return type_error(package, call, "expected Decompressor");
  return decode(package, call, runtime, data, args[1], false, result);
}

X3Status decompressor_finished(X3CallContext* call, X3Runtime*,
                               void* user_data, const X3Value* args,
                               uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, call, "is_finished() takes no arguments");
  auto* data = static_cast<DecompressorData*>(package->host->instance_get_native_data(
      args[0], kDecompressorType));
  if (!data) return type_error(package, call, "expected Decompressor");
  *result = x3_value_bool(data->finished);
  return X3_STATUS_OK;
}

X3Status decompress_function(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, call, "decompress() expects data");
  DecompressorData data;
  data.state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
  if (!data.state)
    return package->host->raise_class_error(call, "MemoryError",
                                             "Brotli decoder allocation failed");
  return decode(package, call, runtime, &data, args[0], true, result);
}

X3Status register_module(X3PackageHost* host) {
  auto* package = new PackageState();
  package->host = host;
  if (host->package_set_cleanup(host, package, cleanup_package) != X3_STATUS_OK) {
    delete package;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "_brotli", &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value base = x3_value_invalid();
  if (host->builtin_value(host, "Exception", &base) != X3_STATUS_OK ||
      host->create_class(host, "error", nullptr, 0,
                         &package->error_class) != X3_STATUS_OK ||
      host->class_set_base(package->error_class, base) != X3_STATUS_OK ||
      host->module_add_value(module, "error", package->error_class) !=
          X3_STATUS_OK)
    return X3_STATUS_ERROR;
  host->value_release(base);
  const X3NativeFunctionDef compressor_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", compressor_init,
       package, 1, 5, 0, compressor_init_kw},
      {sizeof(X3NativeFunctionDef), "process", compressor_process,
       package, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "finish", compressor_finish,
       package, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef decompressor_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", decompressor_init,
       package, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "process", decompressor_process,
       package, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "is_finished", decompressor_finished,
       package, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(module, "Compressor", compressor_methods, 3,
                             &package->compressor_class) != X3_STATUS_OK ||
      host->module_add_class(module, "Decompressor", decompressor_methods, 3,
                             &package->decompressor_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef decompress_def = {
      sizeof(X3NativeFunctionDef), "decompress", decompress_function,
      package, 1, 1, 0, nullptr};
  if (host->module_add_function(module, &decompress_def) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  struct Constant { const char* name; int value; };
  constexpr Constant constants[] = {{"MODE_GENERIC", BROTLI_MODE_GENERIC},
                                    {"MODE_TEXT", BROTLI_MODE_TEXT},
                                    {"MODE_FONT", BROTLI_MODE_FONT}};
  for (const auto& constant : constants)
    if (host->module_add_value(module, constant.name,
                               x3_value_int64(constant.value)) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  X3Value version = host->value_string(host->runtime, "1.2.0");
  const X3Status status = host->module_add_value(module, "__version__", version);
  host->value_release(version);
  return status;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version =
    X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "_brotli");
  host->package_set_metadata(host, "version", "1.2.0");
  return register_module(host);
}
