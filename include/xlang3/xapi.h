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
X3_API const char* x3_runtime_last_error(X3Runtime* runtime);
X3_API X3Status x3_runtime_add_import_root(X3Runtime* runtime, const char* path);

X3_API X3Status x3_runtime_eval_file(
    X3Runtime* runtime,
    const char* path,
    X3Value* result);

X3_API void x3_value_retain(X3Value value);
X3_API void x3_value_release(X3Value value);
X3_API X3Value x3_value_string(X3Runtime* runtime, const char* value);
X3_API X3Value x3_value_list(X3Runtime* runtime);
X3_API X3Value x3_value_dict(X3Runtime* runtime);
X3_API const char* x3_value_to_cstr(X3Runtime* runtime, X3Value value);
X3_API X3ObjectKind x3_value_object_kind(X3Value value);

X3_API X3Status x3_runtime_import_module(
    X3Runtime* runtime,
    const char* package_name,
    const char* module_name,
    X3Value* result);

X3_API X3Status x3_get_attr(
    X3Runtime* runtime,
    X3Value object,
    const char* name,
    X3Value* result);

X3_API X3Status x3_set_attr(
    X3Runtime* runtime,
    X3Value object,
    const char* name,
    X3Value value);

X3_API X3Status x3_call(
    X3Runtime* runtime,
    X3Value callable,
    const X3Value* args,
    uint32_t argc,
    X3Value* result);

X3_API X3Status x3_len(X3Runtime* runtime, X3Value value, uint64_t* result);
X3_API X3Status x3_get_item(
    X3Runtime* runtime,
    X3Value object,
    X3Value key,
    X3Value* result);
X3_API X3Status x3_list_append(X3Runtime* runtime, X3Value list, X3Value item);
X3_API X3Status x3_dict_set_item(
    X3Runtime* runtime,
    X3Value dict,
    X3Value key,
    X3Value item);
X3_API X3Status x3_dict_get_entry(
    X3Runtime* runtime,
    X3Value dict,
    uint64_t index,
    X3Value* key,
    X3Value* value);

#ifdef __cplusplus
}
#endif

#endif
