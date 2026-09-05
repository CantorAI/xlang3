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
#ifndef XLANG3_ABI_VALUE_H
#define XLANG3_ABI_VALUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct X3Object X3Object;
typedef struct X3Type X3Type;

typedef enum X3ValueTag {
  X3_TAG_INVALID = 0,
  X3_TAG_NONE = 1,
  X3_TAG_BOOL = 2,
  X3_TAG_INT64 = 3,
  X3_TAG_UINT64 = 4,
  X3_TAG_DOUBLE = 5,
  X3_TAG_OBJECT = 6
} X3ValueTag;

typedef struct X3Value {
  uint32_t tag;
  uint32_t flags;
  union {
    int32_t b;
    int64_t i64;
    uint64_t u64;
    double f64;
    X3Object* obj;
  } as;
} X3Value;

typedef enum X3ObjectKind {
  X3_OBJECT_KIND_UNKNOWN = 0,
  X3_OBJECT_KIND_STRING = 1,
  X3_OBJECT_KIND_TUPLE = 2,
  X3_OBJECT_KIND_LIST = 3,
  X3_OBJECT_KIND_DICT = 4,
  X3_OBJECT_KIND_INSTANCE = 5,
  X3_OBJECT_KIND_BYTES = 6,
  X3_OBJECT_KIND_BYTEARRAY = 7,
  X3_OBJECT_KIND_MEMORYVIEW = 8,
  X3_OBJECT_KIND_EVENT = 9,
  X3_OBJECT_KIND_EXPRESSION = 10
} X3ObjectKind;

static inline X3Value x3_value_invalid(void) {
  X3Value value;
  value.tag = X3_TAG_INVALID;
  value.flags = 0;
  value.as.u64 = 0;
  return value;
}

static inline X3Value x3_value_none(void) {
  X3Value value;
  value.tag = X3_TAG_NONE;
  value.flags = 0;
  value.as.u64 = 0;
  return value;
}

static inline X3Value x3_value_bool(int32_t b) {
  X3Value value;
  value.tag = X3_TAG_BOOL;
  value.flags = 0;
  value.as.b = b ? 1 : 0;
  return value;
}

static inline X3Value x3_value_int64(int64_t i64) {
  X3Value value;
  value.tag = X3_TAG_INT64;
  value.flags = 0;
  value.as.i64 = i64;
  return value;
}

static inline X3Value x3_value_double(double f64) {
  X3Value value;
  value.tag = X3_TAG_DOUBLE;
  value.flags = 0;
  value.as.f64 = f64;
  return value;
}

static inline X3Value x3_value_uint64(uint64_t u64) {
  X3Value value;
  value.tag = X3_TAG_UINT64;
  value.flags = 0;
  value.as.u64 = u64;
  return value;
}

#ifdef __cplusplus
}
#endif

#endif
