#ifndef XLANG3_XAPI_H
#define XLANG3_XAPI_H

#include "xlang3/xvalue.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #if defined(XLANG3_BUILD_SHARED)
    #define X3_API __declspec(dllexport)
  #elif defined(XLANG3_USE_SHARED)
    #define X3_API __declspec(dllimport)
  #else
    #define X3_API
  #endif
#else
  #define X3_API __attribute__((visibility("default")))
#endif

typedef struct X3Runtime X3Runtime;
typedef struct X3IRModule X3IRModule;
typedef struct X3Executor X3Executor;

typedef enum X3Status {
  X3_STATUS_OK = 0,
  X3_STATUS_ERROR = 1
} X3Status;

X3_API X3Runtime* x3_runtime_create(void);
X3_API void x3_runtime_destroy(X3Runtime* runtime);

X3_API X3Status x3_runtime_eval_file(
    X3Runtime* runtime,
    const char* path,
    X3Value* result);

#ifdef __cplusplus
}
#endif

#endif
