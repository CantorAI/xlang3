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
#ifndef XLANG3_XMODULE_H
#define XLANG3_XMODULE_H

#include "xlang3/xapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define X3_ABI_VERSION 7u
#define X3_PACKAGE_INIT_NAME x3_package_init

typedef struct X3Package X3Package;
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
typedef void (*X3PackageCleanup)(void* data);

typedef struct X3NativeFunctionDef {
  uint32_t size;
  const char* name;
  X3NativeFn callback;
  void* user_data;
  uint32_t min_argc;
  uint32_t max_argc;
  uint32_t flags;
} X3NativeFunctionDef;

typedef struct X3PackageHost {
  uint32_t abi_version;
  uint32_t size;
  X3Status (*add_module)(X3Package* package, const char* name, X3Module** out_module);
  X3Status (*module_add_value)(X3Module* module, const char* name, X3Value value);
  X3Status (*module_add_function)(X3Module* module, const X3NativeFunctionDef* def);
  X3Status (*set_error)(X3CallContext* context, const char* message);
  X3Status (*raise_class_error)(X3CallContext* context, const char* class_name, const char* message);
  X3Status (*raise_error)(X3CallContext* context, X3Value exception_class, const char* message);
  const char* (*runtime_last_error)(X3Runtime* runtime);
  void (*value_release)(X3Value value);
  X3Value (*value_string)(X3Runtime* runtime, const char* value);
  X3Value (*value_list)(X3Runtime* runtime);
  X3Value (*value_dict)(X3Runtime* runtime);
  const char* (*value_to_cstr)(X3Runtime* runtime, X3Value value);
  X3ObjectKind (*value_object_kind)(X3Value value);
  X3Status (*len)(X3Runtime* runtime, X3Value value, uint64_t* result);
  X3Status (*get_item)(X3Runtime* runtime, X3Value object, X3Value key, X3Value* result);
  X3Status (*list_append)(X3Runtime* runtime, X3Value list, X3Value item);
  X3Status (*dict_set_item)(X3Runtime* runtime, X3Value dict, X3Value key, X3Value item);
  X3Status (*dict_get_entry)(X3Runtime* runtime, X3Value dict, uint64_t index, X3Value* key, X3Value* value);
  X3Status (*module_add_class)(
      X3Module* module,
      const char* name,
      const X3NativeFunctionDef* methods,
      uint32_t method_count,
      X3Value* out_class);
  X3Status (*builtin_value)(X3Package* package, const char* name, X3Value* out_value);
  X3Status (*class_set_base)(X3Value klass, X3Value base);
  X3Status (*instance_set_native_data)(
      X3Value instance,
      const char* type_name,
      void* data,
      X3NativeDataCleanup cleanup);
  void* (*instance_get_native_data)(X3Value instance, const char* type_name);
  X3Value (*value_instance)(X3Runtime* runtime, X3Value klass);
  X3Status (*package_set_cleanup)(X3Package* package, void* data, X3PackageCleanup cleanup);
  X3Status (*package_set_metadata)(X3Package* package, const char* key, const char* value);
} X3PackageHost;

typedef X3Status (*X3PackageInitFn)(const X3PackageHost* host, X3Package* package);

#ifdef __cplusplus
}
#endif

#endif
