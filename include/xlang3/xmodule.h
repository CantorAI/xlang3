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

#define X3_ABI_VERSION 1u
#define X3_PACKAGE_INIT_NAME x3_package_init

typedef struct X3Package X3Package;
typedef struct X3Module X3Module;
typedef struct X3CallContext X3CallContext;

typedef X3Status (*X3NativeFn)(
    X3CallContext* context,
    const X3Value* args,
    uint32_t argc,
    X3Value* result);

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
} X3PackageHost;

typedef X3Status (*X3PackageInitFn)(const X3PackageHost* host, X3Package* package);

X3_API void* x3_call_context_user_data(X3CallContext* context);
X3_API X3Runtime* x3_call_context_runtime(X3CallContext* context);

#ifdef __cplusplus
}
#endif

#endif
