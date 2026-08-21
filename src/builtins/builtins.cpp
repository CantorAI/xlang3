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
#include "xlang3/builtins.h"

namespace xlang3 {

void register_core_builtins(Runtime& runtime) {
  register_object_type_builtins(runtime);
  register_exception_builtins(runtime);
  register_functional_builtins(runtime);
  register_io_builtins(runtime);
  register_sequence_builtins(runtime);
  register_raw_block_builtins(runtime);
  register_builtin_modules(runtime);
  register_math_module(runtime);
#ifndef XLANG3_EMBEDDED
  register_time_module(runtime);
  register_atexit_module(runtime);
  register_io_module(runtime);
  register_os_module(runtime);
  register_stat_module(runtime);
  register_imp_module(runtime);
  register_collections_module(runtime);
  register_queue_module(runtime);
  register_types_module(runtime);
  register_traceback_module(runtime);
  register_runpy_module(runtime);
  register_importlib_module(runtime);
  register_weakref_module(runtime);
#endif
  register_thread_modules(runtime);
#ifndef XLANG3_EMBEDDED
  register_task_modules(runtime);
  register_asyncio_module(runtime);
#endif
}

} // namespace xlang3
