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
  register_sys_module(runtime);
  register_time_module(runtime);
  register_abc_module(runtime);
  register_argparse_module(runtime);
  register_atexit_module(runtime);
  register_ast_module(runtime);
  register_io_module(runtime);
  register_os_module(runtime);
  register_platform_module(runtime);
  register_pkgutil_module(runtime);
  register_stat_module(runtime);
  register_string_module(runtime);
  register_imp_module(runtime);
  register_dataclasses_module(runtime);
  register_functools_module(runtime);
  register_future_module(runtime);
  register_typing_module(runtime);
  register_contextlib_module(runtime);
  register_warnings_module(runtime);
  register_re_module(runtime);
  register_fnmatch_glob_modules(runtime);
  register_ctypes_module(runtime);
  register_dis_module(runtime);
  register_enum_module(runtime);
  register_errno_module(runtime);
  register_getpass_module(runtime);
  register_itertools_module(runtime);
  register_json_module(runtime);
  register_sysconfig_module(runtime);
  register_linecache_module(runtime);
  register_urllib_module(runtime);
  register_codecs_module(runtime);
  register_unicodedata_module(runtime);
  register_struct_module(runtime);
  register_signal_module(runtime);
  register_code_module(runtime);
  register_xmlrpc_http_modules(runtime);
  register_collections_module(runtime);
  register_site_module(runtime);
  register_queue_module(runtime);
  register_types_module(runtime);
  register_traceback_module(runtime);
  register_runpy_module(runtime);
  register_importlib_module(runtime);
  register_weakref_module(runtime);
  register_inspect_module(runtime);
  register_logging_module(runtime);
  register_locale_module(runtime);
  register_marshal_module(runtime);
  register_numbers_module(runtime);
  register_opcode_module(runtime);
  register_operator_module(runtime);
  register_pathlib_module(runtime);
  register_pickle_module(runtime);
  register_subprocess_module(runtime);
  register_tokenize_module(runtime);
#if defined(_WIN32)
  register_winapi_module(runtime);
#endif
  register_winreg_module(runtime);
  register_zlib_module(runtime);
  register_zipfile_module(runtime);
  register_zipimport_module(runtime);
  register_socket_modules(runtime);
#endif
  register_thread_modules(runtime);
#ifndef XLANG3_EMBEDDED
  register_task_modules(runtime);
  register_asyncio_module(runtime);
#endif
}

} // namespace xlang3
