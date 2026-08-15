#ifndef XLANG3_XOBJECT_H
#define XLANG3_XOBJECT_H

#include "xlang3/xvalue.h"

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
