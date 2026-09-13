#include "xlang3/builtins.h"

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <bzlib.h>

#include <algorithm>
#include <string>
#include <vector>

namespace xlang3 {
namespace {

constexpr const char* kCompressorType = "_bz2.BZ2Compressor";
constexpr const char* kDecompressorType = "_bz2.BZ2Decompressor";

struct CompressorState { bz_stream stream{}; bool finished = false; };
struct DecompressorState {
  bz_stream stream{};
  bool eof = false;
  std::string unused_data;
  std::string pending_input;
};

bool fail(Runtime& runtime, const char* type, std::string message, std::string& error) {
  error = std::move(message);
  runtime.raise_class_error(type, error);
  return false;
}

bool bytes_arg(const Value& value, std::string& output) {
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    output.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytes = value_as_bytearray(value)) { output = bytes->value; return true; }
  if (auto* view = value_as_memoryview(value)) {
    const auto data = memoryview_object_view(*view);
    if (!view->released && data.data()) { output.assign(data.data(), data.size()); return true; }
  }
  return false;
}

void compressor_cleanup(void* pointer) {
  auto* state = static_cast<CompressorState*>(pointer);
  if (state) BZ2_bzCompressEnd(&state->stream);
  delete state;
}
void decompressor_cleanup(void* pointer) {
  auto* state = static_cast<DecompressorState*>(pointer);
  if (state) BZ2_bzDecompressEnd(&state->stream);
  delete state;
}

bool compressor_new(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                    std::string& error, void* class_pointer) {
  if (argc > 1 || (argc == 1 && args[0].tag != ValueTag::Int64))
    return fail(runtime, "TypeError", "BZ2Compressor() expected optional compresslevel", error);
  const int level = argc == 0 ? 9 : static_cast<int>(args[0].as.i64);
  if (level < 1 || level > 9) return fail(runtime, "ValueError", "compresslevel must be between 1 and 9", error);
  auto* state = new CompressorState();
  if (BZ2_bzCompressInit(&state->stream, level, 0, 0) != BZ_OK) {
    delete state;
    return fail(runtime, "OSError", "bzip2 compressor initialization failed", error);
  }
  out = Value::instance(*static_cast<Value*>(class_pointer));
  if (!instance_set_native_data(out, kCompressorType, state, compressor_cleanup, error)) {
    compressor_cleanup(state); return false;
  }
  return true;
}

bool compressor_run(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                    std::string& error, void*) {
  if (argc != 2) return fail(runtime, "TypeError", "BZ2Compressor.compress() expected data", error);
  auto* state = static_cast<CompressorState*>(instance_get_native_data(args[0], kCompressorType));
  if (!state) return fail(runtime, "TypeError", "invalid BZ2Compressor object", error);
  if (state->finished) return fail(runtime, "ValueError", "Compressor has been flushed", error);
  std::string input;
  if (!bytes_arg(args[1], input)) return fail(runtime, "TypeError", "a bytes-like object is required", error);
  state->stream.next_in = input.empty() ? nullptr : const_cast<char*>(input.data());
  state->stream.avail_in = static_cast<unsigned int>(input.size());
  std::string result;
  char buffer[16384];
  do {
    state->stream.next_out = buffer;
    state->stream.avail_out = sizeof(buffer);
    const int status = BZ2_bzCompress(&state->stream, BZ_RUN);
    if (status != BZ_RUN_OK) return fail(runtime, "OSError", "bzip2 compression failed", error);
    result.append(buffer, sizeof(buffer) - state->stream.avail_out);
  } while (state->stream.avail_in != 0 || state->stream.avail_out == 0);
  out = Value::bytes(std::move(result));
  return true;
}

bool compressor_flush(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc != 1) return fail(runtime, "TypeError", "BZ2Compressor.flush() expected no arguments", error);
  auto* state = static_cast<CompressorState*>(instance_get_native_data(args[0], kCompressorType));
  if (!state) return fail(runtime, "TypeError", "invalid BZ2Compressor object", error);
  if (state->finished) return fail(runtime, "ValueError", "Repeated call to flush()", error);
  state->stream.next_in = nullptr;
  state->stream.avail_in = 0;
  std::string result;
  char buffer[16384];
  int status = BZ_FINISH_OK;
  while (status == BZ_FINISH_OK) {
    state->stream.next_out = buffer;
    state->stream.avail_out = sizeof(buffer);
    status = BZ2_bzCompress(&state->stream, BZ_FINISH);
    result.append(buffer, sizeof(buffer) - state->stream.avail_out);
  }
  if (status != BZ_STREAM_END) return fail(runtime, "OSError", "bzip2 flush failed", error);
  state->finished = true;
  out = Value::bytes(std::move(result));
  return true;
}

bool decompressor_new(Runtime& runtime, const Value*, uint32_t argc, Value& out,
                      std::string& error, void* class_pointer) {
  if (argc != 0) return fail(runtime, "TypeError", "BZ2Decompressor() takes no arguments", error);
  auto* state = new DecompressorState();
  if (BZ2_bzDecompressInit(&state->stream, 0, 0) != BZ_OK) {
    delete state;
    return fail(runtime, "OSError", "bzip2 decompressor initialization failed", error);
  }
  out = Value::instance(*static_cast<Value*>(class_pointer));
  if (!instance_set_native_data(out, kDecompressorType, state, decompressor_cleanup, error)) {
    decompressor_cleanup(state); return false;
  }
  object_set_attr(out, "eof", Value::boolean(false), error);
  object_set_attr(out, "needs_input", Value::boolean(true), error);
  object_set_attr(out, "unused_data", Value::bytes(""), error);
  return true;
}

bool decompressor_run(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc < 2 || argc > 3) return fail(runtime, "TypeError", "BZ2Decompressor.decompress() expected data and optional max_length", error);
  auto* state = static_cast<DecompressorState*>(instance_get_native_data(args[0], kDecompressorType));
  if (!state) return fail(runtime, "TypeError", "invalid BZ2Decompressor object", error);
  if (state->eof) return fail(runtime, "EOFError", "End of stream already reached", error);
  std::string input;
  if (!bytes_arg(args[1], input)) return fail(runtime, "TypeError", "a bytes-like object is required", error);
  if (!state->pending_input.empty()) { input.insert(0, state->pending_input); state->pending_input.clear(); }
  int64_t max_length = -1;
  if (argc == 3) {
    if (args[2].tag != ValueTag::Int64) return fail(runtime, "TypeError", "max_length must be int", error);
    max_length = args[2].as.i64;
    if (max_length < -1) return fail(runtime, "ValueError", "max_length must be non-negative", error);
  }
  state->stream.next_in = input.empty() ? nullptr : input.data();
  state->stream.avail_in = static_cast<unsigned int>(input.size());
  std::string result;
  char buffer[16384];
  int status = BZ_OK;
  do {
    size_t capacity = sizeof(buffer);
    if (max_length >= 0) capacity = std::min<size_t>(capacity, static_cast<size_t>(max_length) - result.size());
    if (capacity == 0) break;
    state->stream.next_out = buffer;
    state->stream.avail_out = static_cast<unsigned int>(capacity);
    status = BZ2_bzDecompress(&state->stream);
    if (status != BZ_OK && status != BZ_STREAM_END)
      return fail(runtime, "OSError", "Invalid data stream", error);
    result.append(buffer, capacity - state->stream.avail_out);
    if (status == BZ_STREAM_END) break;
  } while (state->stream.avail_in != 0 || state->stream.avail_out == 0);
  if (status == BZ_STREAM_END) {
    state->eof = true;
    if (state->stream.avail_in) state->unused_data.assign(state->stream.next_in, state->stream.avail_in);
  } else if (state->stream.avail_in) {
    state->pending_input.assign(state->stream.next_in, state->stream.avail_in);
  }
  Value self = args[0];
  object_set_attr(self, "eof", Value::boolean(state->eof), error);
  object_set_attr(self, "needs_input", Value::boolean(!state->eof && state->pending_input.empty()), error);
  object_set_attr(self, "unused_data", Value::bytes(state->unused_data), error);
  out = Value::bytes(std::move(result));
  return true;
}

Value compressor_class(Runtime& runtime) {
  return Value::class_object("BZ2Compressor", {{"__module__",Value::string("_bz2")},
      {"compress",runtime.make_native_function("_bz2.BZ2Compressor.compress",compressor_run)},
      {"flush",runtime.make_native_function("_bz2.BZ2Compressor.flush",compressor_flush)}});
}
Value decompressor_class(Runtime& runtime) {
  return Value::class_object("BZ2Decompressor", {{"__module__",Value::string("_bz2")},
      {"decompress",runtime.make_native_function("_bz2.BZ2Decompressor.decompress",decompressor_run)}});
}

}  // namespace

void register_bz2_module(Runtime& runtime) {
  Value compressor = compressor_class(runtime);
  Value decompressor = decompressor_class(runtime);
  NativeModuleBuilder builder(runtime,"_bz2");
  builder.value("BZ2Compressor",runtime.make_native_function("_bz2.BZ2Compressor",compressor_new,
          new Value(compressor),[](void* value){delete static_cast<Value*>(value);}))
      .value("BZ2Decompressor",runtime.make_native_function("_bz2.BZ2Decompressor",decompressor_new,
          new Value(decompressor),[](void* value){delete static_cast<Value*>(value);}));
  runtime.register_module("_bz2",builder.finish());
}

}  // namespace xlang3
