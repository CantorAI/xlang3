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
void register_sys_module(Runtime& runtime);
constexpr int64_t kSysMonitoringEventPyStart = 1;
constexpr int64_t kSysMonitoringEventPyResume = 2;
constexpr int64_t kSysMonitoringEventPyReturn = 4;
constexpr int64_t kSysMonitoringEventPyYield = 8;
constexpr int64_t kSysMonitoringEventCall = 16;
constexpr int64_t kSysMonitoringEventLine = 32;
constexpr int64_t kSysMonitoringEventInstruction = 64;
constexpr int64_t kSysMonitoringEventJump = 128;
constexpr int64_t kSysMonitoringEventBranchLeft = 256;
constexpr int64_t kSysMonitoringEventBranchRight = 512;
constexpr int64_t kSysMonitoringEventStopIteration = 1024;
constexpr int64_t kSysMonitoringEventRaise = 2048;
constexpr int64_t kSysMonitoringEventExceptionHandled = 4096;
constexpr int64_t kSysMonitoringEventPyUnwind = 8192;
constexpr int64_t kSysMonitoringEventPyThrow = 16384;
constexpr int64_t kSysMonitoringEventReraise = 32768;
constexpr int64_t kSysMonitoringEventCReturn = 65536;
constexpr int64_t kSysMonitoringEventCRaise = 131072;
bool sys_monitoring_dispatch_event(
    Runtime& runtime,
    int64_t event,
    const Value& code,
    int64_t instruction_offset,
    const Value* arg,
    std::string& error);
void register_time_module(Runtime& runtime);
void register_abc_module(Runtime& runtime);
void register_argparse_module(Runtime& runtime);
void register_atexit_module(Runtime& runtime);
void register_ast_module(Runtime& runtime);
void register_io_module(Runtime& runtime);
void register_os_module(Runtime& runtime);
void register_platform_module(Runtime& runtime);
void register_pkgutil_module(Runtime& runtime);
void register_stat_module(Runtime& runtime);
void register_string_module(Runtime& runtime);
void register_imp_module(Runtime& runtime);
void register_dataclasses_module(Runtime& runtime);
void register_functools_module(Runtime& runtime);
void register_future_module(Runtime& runtime);
void register_typing_module(Runtime& runtime);
void register_contextlib_module(Runtime& runtime);
void register_warnings_module(Runtime& runtime);
void register_re_module(Runtime& runtime);
void register_fnmatch_glob_modules(Runtime& runtime);
void register_ctypes_module(Runtime& runtime);
void register_dis_module(Runtime& runtime);
void register_enum_module(Runtime& runtime);
void register_getpass_module(Runtime& runtime);
void register_itertools_module(Runtime& runtime);
void register_json_module(Runtime& runtime);
void register_sysconfig_module(Runtime& runtime);
void register_linecache_module(Runtime& runtime);
void register_urllib_module(Runtime& runtime);
void register_codecs_module(Runtime& runtime);
void register_unicodedata_module(Runtime& runtime);
void register_struct_module(Runtime& runtime);
void register_signal_module(Runtime& runtime);
void register_code_module(Runtime& runtime);
void register_xmlrpc_http_modules(Runtime& runtime);
void register_collections_module(Runtime& runtime);
void register_site_module(Runtime& runtime);
void register_queue_module(Runtime& runtime);
void register_types_module(Runtime& runtime);
void register_traceback_module(Runtime& runtime);
void register_runpy_module(Runtime& runtime);
void register_importlib_module(Runtime& runtime);
void register_weakref_module(Runtime& runtime);
Value make_weakref_ref(Runtime& runtime, const Value& target);
bool weakref_get_target(const Value& ref, Value& out);
void weakref_invalidate_target(Object* target);
void register_inspect_module(Runtime& runtime);
void register_logging_module(Runtime& runtime);
void register_locale_module(Runtime& runtime);
void register_marshal_module(Runtime& runtime);
void register_numbers_module(Runtime& runtime);
void register_opcode_module(Runtime& runtime);
void register_operator_module(Runtime& runtime);
void register_pathlib_module(Runtime& runtime);
void register_pickle_module(Runtime& runtime);
void register_subprocess_module(Runtime& runtime);
void register_tokenize_module(Runtime& runtime);
void register_winreg_module(Runtime& runtime);
void register_zlib_module(Runtime& runtime);
void register_zipfile_module(Runtime& runtime);
void register_zipimport_module(Runtime& runtime);
void register_pydevd_compat_modules(Runtime& runtime);
void register_socket_modules(Runtime& runtime);
void register_thread_modules(Runtime& runtime);
void register_task_modules(Runtime& runtime);
void register_asyncio_module(Runtime& runtime);

} // namespace xlang3
