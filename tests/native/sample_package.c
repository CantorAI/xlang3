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
#include "xlang3/xmodule.h"

#if defined(_WIN32)
#define X3_SAMPLE_EXPORT __declspec(dllexport)
#else
#define X3_SAMPLE_EXPORT __attribute__((visibility("default")))
#endif

static X3Status sample_add(X3CallContext* context, const X3Value* args, uint32_t argc, X3Value* result) {
  if (argc != 2 || args[0].tag != X3_TAG_INT64 || args[1].tag != X3_TAG_INT64) {
    return X3_STATUS_ERROR;
  }
  (void)context;
  *result = x3_value_int64(args[0].as.i64 + args[1].as.i64);
  return X3_STATUS_OK;
}

X3_SAMPLE_EXPORT X3Status x3_package_init(const X3PackageHost* host, X3Package* package) {
  X3Module* sample = 0;
  X3NativeFunctionDef add_def;

  if (host == 0 || host->abi_version != X3_ABI_VERSION) {
    return X3_STATUS_ERROR;
  }
  if (host->add_module(package, "sample", &sample) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  if (host->module_add_value(sample, "answer", x3_value_int64(42)) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  add_def.size = sizeof(add_def);
  add_def.name = "add";
  add_def.callback = sample_add;
  add_def.user_data = 0;
  add_def.min_argc = 2;
  add_def.max_argc = 2;
  add_def.flags = 0;
  return host->module_add_function(sample, &add_def);
}
