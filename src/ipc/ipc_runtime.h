/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang3/value.h"

#include <string>

namespace xlang3 {

class Runtime;

bool ipc_register_remote_object(Runtime& runtime, const std::string& name, const Value& object, std::string& error);
bool ipc_lrpc_listen(Runtime& runtime, int64_t port, bool wait, Value& out, std::string& error);
bool ipc_import_thru(Runtime& runtime, const std::string& name, const Value& endpoint, Value& out, std::string& error);
void register_ipc_builtins(Runtime& runtime);

} // namespace xlang3
