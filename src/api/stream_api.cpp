/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "stream_internal.h"
#include "xlang3/runtime.h"
#include <exception>
#include <memory>

namespace {
X3Status fail(X3Runtime* runtime, const char* error) {
  if (runtime) reinterpret_cast<xlang3::Runtime*>(runtime)->set_last_error(error);
  return X3_STATUS_ERROR;
}
X3Status fail(X3Stream* stream, const char* error) {
  if (stream) stream->failed = true;
  return fail(stream ? stream->runtime : nullptr, error);
}
}

X3Status x3_stream_create(X3Runtime* runtime, X3Stream** result) {
  if (!runtime || !result) return fail(runtime, "null stream creation argument");
  *result = nullptr;
  try {
    auto stream = std::make_unique<X3Stream>();
    stream->runtime = runtime;
    *result = stream.release();
    return X3_STATUS_OK;
  } catch (const std::exception& e) { return fail(runtime, e.what()); }
}

X3Status x3_stream_from_blocks(X3Runtime* runtime, const X3StreamBlock* blocks,
    uint32_t count, X3Stream** result) {
  if (!result) return fail(runtime, "null stream result");
  *result = nullptr;
  if (count && !blocks) return fail(runtime, "null stream blocks");
  X3Stream* raw = nullptr;
  if (x3_stream_create(runtime, &raw) != X3_STATUS_OK) return X3_STATUS_ERROR;
  std::unique_ptr<X3Stream> stream(raw);
  try {
    for (uint32_t i = 0; i < count; ++i) {
      if (blocks[i].size > INT64_MAX - stream->size ||
          (blocks[i].size && !blocks[i].data)) return fail(runtime, "invalid stream block");
      if (!blocks[i].size) continue;
      stream->storage.AddBlock(static_cast<char*>(const_cast<void*>(blocks[i].data)),
          static_cast<int64_t>(blocks[i].size), static_cast<int64_t>(blocks[i].size));
      stream->size += blocks[i].size;
    }
    stream->reading = true;
    *result = stream.release();
    return X3_STATUS_OK;
  } catch (const std::exception& e) { return fail(runtime, e.what()); }
}

X3Status x3_stream_create_provider(X3Runtime* runtime, X3StreamAllocate allocate,
    void* context, X3Stream** result) {
  if (result) *result = nullptr;
  if (!allocate) return fail(runtime, "null stream allocator");
  if (x3_stream_create(runtime, result) != X3_STATUS_OK) return X3_STATUS_ERROR;
  (*result)->storage.allocate = allocate;
  (*result)->storage.context = context;
  return X3_STATUS_OK;
}

void x3_stream_destroy(X3Stream* stream) { delete stream; }
uint64_t x3_stream_size(const X3Stream* stream) { return stream ? stream->size : 0; }
X3Status x3_stream_rewind(X3Stream* stream) {
  if (!stream || stream->failed) return fail(stream, "invalid or failed stream");
  stream->storage.ResetPos();
  stream->reading = true;
  stream->position = 0;
  return X3_STATUS_OK;
}
X3Status x3_stream_write(X3Stream* stream, const void* data, uint64_t size) {
  if (!stream || stream->failed || stream->reading || (size && !data) ||
      size > INT64_MAX - stream->size) return fail(stream, "invalid stream write");
  try {
    if (!stream->storage.append(data, static_cast<int64_t>(size)))
      return fail(stream, "stream allocation failed");
    stream->size += size;
    stream->position += size;
    return X3_STATUS_OK;
  } catch (const std::exception& e) { return fail(stream, e.what()); }
}
X3Status x3_stream_read(X3Stream* stream, void* data, uint64_t size) {
  if (!stream || stream->failed || !stream->reading || (size && !data) ||
      size > stream->size - stream->position) return fail(stream, "invalid or truncated stream read");
  if (!stream->storage.CopyTo(static_cast<char*>(data), static_cast<int64_t>(size)))
    return fail(stream, "stream read failed");
  stream->position += size;
  return X3_STATUS_OK;
}
X3Status x3_stream_copy(const X3Stream* stream, void* data, uint64_t capacity) {
  if (!stream || stream->failed || capacity < stream->size || (stream->size && !data))
    return fail(stream ? stream->runtime : nullptr, "invalid stream copy");
  // FullCopyTo only reads the blocks and does not change the cursor.
  if (!const_cast<X3Stream*>(stream)->storage.FullCopyTo(static_cast<char*>(data),
      static_cast<int64_t>(stream->size))) return fail(stream->runtime, "stream copy failed");
  return X3_STATUS_OK;
}
