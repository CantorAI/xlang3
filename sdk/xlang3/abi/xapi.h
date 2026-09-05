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
#ifndef XLANG3_ABI_API_H
#define XLANG3_ABI_API_H

#include "xlang3/abi/xvalue.h"

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

typedef enum X3ValueBinaryOp {
  X3_VALUE_BINARY_ADD = 1
} X3ValueBinaryOp;

typedef enum X3ValueCompareOp {
  X3_VALUE_COMPARE_EQ = 1,
  X3_VALUE_COMPARE_NE = 2,
  X3_VALUE_COMPARE_LT = 3,
  X3_VALUE_COMPARE_LE = 4,
  X3_VALUE_COMPARE_GT = 5,
  X3_VALUE_COMPARE_GE = 6
} X3ValueCompareOp;

X3_API X3Runtime* x3_runtime_create(void);
X3_API void x3_runtime_destroy(X3Runtime* runtime);
X3_API const char* x3_runtime_last_error(X3Runtime* runtime);
typedef X3Status (*X3EventChanged)(void* context, uint64_t count);
/* On success the event owns context until replacement or destruction. */
X3_API X3Status x3_event_set_change_handler(X3Runtime*, X3Value,
    X3EventChanged callback, void* context, void (*cleanup)(void*));
X3_API X3Status x3_runtime_add_import_root(X3Runtime* runtime, const char* path);
X3_API X3Status x3_runtime_import_remote(X3Runtime* runtime, const char* name,
    const char* endpoint, X3Value* result);

X3_API X3Status x3_runtime_eval_file(
    X3Runtime* runtime,
    const char* path,
    X3Value* result);

X3_API void x3_value_retain(X3Value value);
X3_API void x3_value_release(X3Value value);
X3_API X3Value x3_value_string(X3Runtime* runtime, const char* value);
X3_API X3Value x3_value_bytes(X3Runtime* runtime, const void* data, uint64_t size);
X3_API X3Value x3_value_list(X3Runtime* runtime);
X3_API X3Value x3_value_dict(X3Runtime* runtime);
X3_API const char* x3_value_to_cstr(X3Runtime* runtime, X3Value value);
X3_API X3ObjectKind x3_value_object_kind(X3Value value);
X3_API X3Status x3_value_bytes_data(X3Runtime* runtime, X3Value value, const void** data, uint64_t* size);
X3_API X3Status x3_value_to_bytes(X3Runtime* runtime, X3Value value, X3Value* result);
X3_API X3Status x3_value_from_bytes(X3Runtime* runtime, X3Value bytes, X3Value* result);
/* Evaluate against a resource snapshot without changing it. Reservations are
   returned separately; the scheduler must commit them atomically. */
X3_API X3Status x3_expression_evaluate(X3Runtime* runtime, X3Value expression,
    X3Value bindings, X3Value* result, X3Value* reservations);
X3_API X3Status x3_expression_inspect(X3Runtime* runtime, X3Value expression, X3Value* result);
X3_API X3Status x3_value_binary_op(
    X3Runtime* runtime,
    X3ValueBinaryOp op,
    X3Value left,
    X3Value right,
    X3Value* result);
X3_API X3Status x3_value_compare_op(
    X3Runtime* runtime,
    X3ValueCompareOp op,
    X3Value left,
    X3Value right,
    int32_t* result);
X3_API X3Status x3_event_create(X3Runtime* runtime, const char* name, X3Value* result);
X3_API X3Status x3_event_subscribe(X3Runtime* runtime, X3Value event, X3Value callable, uint64_t* cookie);
X3_API X3Status x3_event_unsubscribe(X3Runtime* runtime, X3Value event, uint64_t cookie);
X3_API X3Status x3_event_fire(
    X3Runtime* runtime,
    X3Value event,
    const X3Value* args,
    uint32_t argc,
    X3Value* result);

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

/* Names and values are borrowed for the duration of the call. */
typedef struct X3KeywordArg {
  const char* name;
  X3Value value;
} X3KeywordArg;
X3_API X3Status x3_call_kw(X3Runtime* runtime, X3Value callable,
    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
    uint32_t kwargc, X3Value* result);

X3_API X3Status x3_len(X3Runtime* runtime, X3Value value, uint64_t* result);
/* Borrowed native payload, valid while the instance remains alive. */
X3_API void* x3_instance_get_native_data(X3Value instance, const char* type_name);
X3_API X3Status x3_instance_set_native_data(X3Value instance, const char* type_name,
    void* data, void (*cleanup)(void*));
X3_API X3Value x3_value_instance(X3Runtime* runtime, X3Value klass);
/* The callback returns a borrowed, correctly adjusted native base pointer. */
typedef void* (*X3NativeDataCast)(void* data, const char* type_name);
X3_API X3Status x3_instance_set_native_cast(X3Value instance, X3NativeDataCast cast);
/* On success, cleanup receives owner, not data. Ownership transfers only on success. */
X3_API X3Status x3_instance_set_native_owner(X3Value instance, const char* type_name,
    void* data, void* owner, void (*cleanup)(void*));
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
