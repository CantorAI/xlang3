/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#ifndef XLANG3_ABI_BUFFER_H
#define XLANG3_ABI_BUFFER_H
#include "xlang3/abi/xapi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct X3Buffer X3Buffer;
typedef struct X3BufferInfo {
  void* data;
  uint64_t size;
  uint64_t item_size;
  const char* format;
  int32_t readonly;
} X3BufferInfo;
/* Acquires stable contiguous storage. The handle owns the storage and prevents
   bytearray resizing until release. info pointers remain valid until release.
   A writable request against read-only storage fails. No bytes are copied. */
X3_API X3Status x3_buffer_acquire(X3Runtime*, X3Value, int32_t writable,
    X3Buffer** result, X3BufferInfo* info);
X3_API void x3_buffer_release(X3Buffer* buffer);
#ifdef __cplusplus
}
#endif
#endif
