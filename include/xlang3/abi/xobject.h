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
#ifndef XLANG3_ABI_OBJECT_H
#define XLANG3_ABI_OBJECT_H

#include "xlang3/abi/xvalue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct X3Object {
  X3Type* type;
  uint32_t refcnt;
  uint32_t flags;
} X3Object;

typedef struct X3TypeSlots {
  void (*destroy)(X3Object* obj);
  int (*truth)(X3Object* obj);
  int (*to_string)(X3Object* obj, X3Value* out);
  int (*binary_add)(X3Object* lhs, X3Value rhs, X3Value* out);
} X3TypeSlots;

typedef struct X3Type {
  const char* name;
  uint32_t flags;
  uint32_t version;
  X3TypeSlots slots;
} X3Type;

#ifdef __cplusplus
}
#endif

#endif
