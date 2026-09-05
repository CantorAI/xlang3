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
#ifndef XLANG3_ABI_MODULE_H
#define XLANG3_ABI_MODULE_H

#include "xlang3/abi/xapi.h"
#include "xlang3/abi/xstream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define X3_ABI_VERSION 21u
#define X3_NATIVE_CAPTURE_EXPRESSIONS 1u
/* Copy the complete argument graph across IPC; local calls are unchanged. */
#define X3_NATIVE_IPC_ARGS_BY_VALUE 2u
#define X3_PACKAGE_INIT_NAME Load
#define X3_PACKAGE_ABI_SYMBOL xlang3_package_abi_version

typedef struct X3Module X3Module;
typedef struct X3CallContext X3CallContext;

typedef X3Status (*X3NativeFn)(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result);

typedef void (*X3NativeDataCleanup)(void* data);
typedef X3Status (*X3NativeKeywordFn)(X3CallContext* context, X3Runtime* runtime,
    void* user_data, const X3Value* args, uint32_t argc,
    const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result);
typedef void (*X3PackageCleanup)(void* data);

typedef struct X3NativeFunctionDef {
  uint32_t size;
  const char* name;
  X3NativeFn callback;
  void* user_data;
  uint32_t min_argc;
  uint32_t max_argc;
  uint32_t flags;
  X3NativeKeywordFn keyword_callback;
} X3NativeFunctionDef;

typedef struct X3PackageHost {
  uint32_t abi_version;
  uint32_t size;
  void* package_context;
  X3Runtime* runtime;
  const char* package_name;
  const char* library_path;
  X3Status (*add_module)(struct X3PackageHost* host, const char* name, X3Module** out_module);
  X3Status (*module_add_value)(X3Module* module, const char* name, X3Value value);
  X3Status (*module_add_function)(X3Module* module, const X3NativeFunctionDef* def);
  X3Status (*set_error)(X3CallContext* context, const char* message);
  X3Status (*raise_class_error)(X3CallContext* context, const char* class_name, const char* message);
  X3Status (*raise_error)(X3CallContext* context, X3Value exception_class, const char* message);
  const char* (*runtime_last_error)(X3Runtime* runtime);
  void (*value_retain)(X3Value value);
  void (*value_release)(X3Value value);
  X3Value (*value_string)(X3Runtime* runtime, const char* value);
  X3Value (*value_bytes)(X3Runtime* runtime, const void* data, uint64_t size);
  X3Value (*value_list)(X3Runtime* runtime);
  X3Value (*value_dict)(X3Runtime* runtime);
  const char* (*value_to_cstr)(X3Runtime* runtime, X3Value value);
  X3ObjectKind (*value_object_kind)(X3Value value);
  X3Status (*value_bytes_data)(X3Runtime* runtime, X3Value value, const void** data, uint64_t* size);
  X3Status (*value_to_bytes)(X3Runtime* runtime, X3Value value, X3Value* result);
  X3Status (*value_from_bytes)(X3Runtime* runtime, X3Value bytes, X3Value* result);
  X3Status (*value_binary_op)(X3Runtime* runtime, X3ValueBinaryOp op, X3Value left, X3Value right, X3Value* result);
  X3Status (*value_compare_op)(X3Runtime* runtime, X3ValueCompareOp op, X3Value left, X3Value right, int32_t* result);
  X3Status (*event_create)(X3Runtime* runtime, const char* name, X3Value* result);
  X3Status (*event_subscribe)(X3Runtime* runtime, X3Value event, X3Value callable, uint64_t* cookie);
  X3Status (*event_unsubscribe)(X3Runtime* runtime, X3Value event, uint64_t cookie);
  X3Status (*event_fire)(X3Runtime* runtime, X3Value event, const X3Value* args, uint32_t argc, X3Value* result);
  X3Status (*call)(X3Runtime* runtime, X3Value callable, const X3Value* args, uint32_t argc, X3Value* result);
  X3Status (*len)(X3Runtime* runtime, X3Value value, uint64_t* result);
  X3Status (*get_attr)(X3Runtime* runtime, X3Value object, const char* name, X3Value* result);
  X3Status (*get_item)(X3Runtime* runtime, X3Value object, X3Value key, X3Value* result);
  X3Status (*set_attr)(X3Runtime* runtime, X3Value object, const char* name, X3Value value);
  X3Status (*list_append)(X3Runtime* runtime, X3Value list, X3Value item);
  X3Status (*dict_set_item)(X3Runtime* runtime, X3Value dict, X3Value key, X3Value item);
  X3Status (*dict_get_entry)(X3Runtime* runtime, X3Value dict, uint64_t index, X3Value* key, X3Value* value);
  X3Status (*module_add_class)(
      X3Module* module,
      const char* name,
      const X3NativeFunctionDef* methods,
      uint32_t method_count,
      X3Value* out_class);
  X3Status (*builtin_value)(struct X3PackageHost* host, const char* name, X3Value* out_value);
  X3Status (*class_set_base)(X3Value klass, X3Value base);
  X3Status (*instance_set_native_data)(
      X3Value instance,
      const char* type_name,
      void* data,
      X3NativeDataCleanup cleanup);
  void* (*instance_get_native_data)(X3Value instance, const char* type_name);
  X3Value (*value_instance)(X3Runtime* runtime, X3Value klass);
  X3Status (*package_set_cleanup)(struct X3PackageHost* host, void* data, X3PackageCleanup cleanup);
  X3Status (*class_add_value)(X3Value klass, const char* name, X3Value value);
  X3Status (*property_create)(
      X3Runtime* runtime,
      const char* name,
      X3NativeFn getter,
      X3NativeFn setter,
      void* user_data,
      X3Value* result);
  X3Status (*package_set_metadata)(struct X3PackageHost* host, const char* key, const char* value);
  X3Status (*module_get_value)(X3Module* module, X3Value* out_value);
  X3Status (*module_get_attr)(X3Module* module, const char* name, X3Value* out_value);
  X3Status (*module_set_attr)(X3Module* module, const char* name, X3Value value);
  X3Status (*expression_evaluate)(X3Runtime* runtime, X3Value expression,
      X3Value bindings, X3Value* result, X3Value* reservations);
  X3Status (*expression_inspect)(X3Runtime* runtime, X3Value expression, X3Value* result);
  X3Status (*call_kw)(X3Runtime*, X3Value, const X3Value*, uint32_t,
      const X3KeywordArg*, uint32_t, X3Value*);
  X3Status (*stream_create)(X3Runtime*, X3Stream**);
  X3Status (*stream_from_blocks)(X3Runtime*, const X3StreamBlock*, uint32_t, X3Stream**);
  X3Status (*stream_create_provider)(X3Runtime*, X3StreamAllocate, void*, X3Stream**);
  void (*stream_destroy)(X3Stream*);
  uint64_t (*stream_size)(const X3Stream*);
  X3Status (*stream_rewind)(X3Stream*);
  X3Status (*stream_write)(X3Stream*, const void*, uint64_t);
  X3Status (*stream_read)(X3Stream*, void*, uint64_t);
  X3Status (*stream_copy)(const X3Stream*, void*, uint64_t);
  X3Status (*value_to_stream)(X3Runtime*, X3Value, X3Stream*);
  X3Status (*value_from_stream)(X3Runtime*, X3Stream*, X3Value*);
  X3Status (*register_native_serializer)(X3Runtime*, const X3NativeSerializerDef*);
  X3Status (*collect_serialized_objects)(X3Runtime*, uint64_t*);
  X3Status (*instance_set_native_owner)(X3Value, const char*, void*, void*, X3NativeDataCleanup);
  X3Status (*event_set_change_handler)(X3Runtime*, X3Value, X3EventChanged, void*, void (*)(void*));
  /* Creates a class value without publishing it as a module attribute. */
  X3Status (*create_class)(struct X3PackageHost*, const char*, const X3NativeFunctionDef*, uint32_t, X3Value*);
  X3Status (*instance_set_native_cast)(X3Value, X3NativeDataCast);
} X3PackageHost;

typedef X3Status (*X3PackageInitFn)(void* host, X3Value cur_module);
/* Synchronous executable-package registration. Context is borrowed for this call;
   the package host and registered modules remain owned by the runtime. */
typedef X3Status (*X3EmbeddedPackageInitFn)(void* host, X3Value cur_module, void* context);
X3_API X3Status x3_runtime_register_package(X3Runtime* runtime, const char* name,
    X3EmbeddedPackageInitFn init, void* context, X3Value* result);

#ifdef __cplusplus
}
#endif

#endif
