/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#ifndef XLANG3_ABI_STREAM_H
#define XLANG3_ABI_STREAM_H
#include "xlang3/abi/xapi.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct X3Stream X3Stream;
/* Callbacks exchange ordinary values as native state. The graph serializer
   preserves references in that state. Callback arguments are borrowed; encode
   returns an owned value. Decode fills an already allocated instance without
   invoking its constructor. Callbacks must not execute the restoring graph. */
typedef X3Status (*X3NativeEncode)(X3Runtime*, X3Value instance, void* user_data, X3Value* state);
typedef X3Status (*X3NativeDecode)(X3Runtime*, X3Value instance, X3Value state, void* user_data);
typedef struct X3NativeSerializerDef {
  uint32_t size;
  const char* type_id;
  const char* native_type;
  uint32_t version;
  X3NativeEncode encode;
  X3NativeDecode decode;
  void* user_data;
  void (*cleanup)(void*);
} X3NativeSerializerDef;
/* type_id is portable; native_type matches instance_set_native_data locally.
   user_data ownership transfers only on success; duplicate registrations fail. */
X3_API X3Status x3_register_native_serializer(X3Runtime*, const X3NativeSerializerDef*);
X3_API X3Status x3_runtime_collect_serialized_objects(X3Runtime*, uint64_t* reclaimed);
typedef struct X3StreamBlock {
  const void* data;
  uint64_t size;
} X3StreamBlock;
/* Output blocks must remain writable and stable until stream destruction.
   The caller owns them and context. Callbacks must not throw or reenter the stream. */
typedef X3Status (*X3StreamAllocate)(void* context, void** data, uint64_t* capacity);
X3_API X3Status x3_stream_create(X3Runtime* runtime, X3Stream** result);
/* Input blocks are borrowed, immutable, and must outlive the stream. */
X3_API X3Status x3_stream_from_blocks(X3Runtime* runtime,
    const X3StreamBlock* blocks, uint32_t count, X3Stream** result);
X3_API X3Status x3_stream_create_provider(X3Runtime* runtime,
    X3StreamAllocate allocate, void* context, X3Stream** result);
X3_API void x3_stream_destroy(X3Stream* stream);
X3_API uint64_t x3_stream_size(const X3Stream* stream);
/* Rewind switches an output stream to read-only. Failed streams cannot be reused. */
X3_API X3Status x3_stream_rewind(X3Stream* stream);
X3_API X3Status x3_stream_write(X3Stream* stream, const void* data, uint64_t size);
X3_API X3Status x3_stream_read(X3Stream* stream, void* data, uint64_t size);
X3_API X3Status x3_stream_copy(const X3Stream* stream, void* data, uint64_t capacity);
X3_API X3Status x3_value_to_stream(X3Runtime* runtime, X3Value value, X3Stream* stream);
X3_API X3Status x3_value_from_stream(X3Runtime* runtime, X3Stream* stream, X3Value* result);

#ifdef __cplusplus
}
#endif
#endif
