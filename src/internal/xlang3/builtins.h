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
#pragma once

#include "xlang3/runtime.h"

namespace xlang3 {

void register_core_builtins(Runtime& runtime);
void register_object_type_builtins(Runtime& runtime);
bool runtime_type_of_value(Runtime& runtime, const Value& value, Value& out);
void register_exception_builtins(Runtime& runtime);
void register_functional_builtins(Runtime& runtime);
void register_io_builtins(Runtime& runtime);
void register_sequence_builtins(Runtime& runtime);
void register_raw_block_builtins(Runtime& runtime);
void register_builtin_modules(Runtime& runtime);
void register_math_module(Runtime& runtime);
void register_time_module(Runtime& runtime);
void register_atexit_module(Runtime& runtime);
void register_io_module(Runtime& runtime);
void register_os_module(Runtime& runtime);
void register_stat_module(Runtime& runtime);
void register_imp_module(Runtime& runtime);
void register_collections_module(Runtime& runtime);
void register_queue_module(Runtime& runtime);
void register_thread_modules(Runtime& runtime);
void register_task_modules(Runtime& runtime);
void register_asyncio_module(Runtime& runtime);

} // namespace xlang3
