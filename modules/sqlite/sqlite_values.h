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

#include "xlang3/xmodule.h"

#include "sqlite3.h"

namespace xlang3_sqlite {

bool require_string(
    const X3PackageHost* host,
    X3CallContext* context,
    X3Runtime* runtime,
    X3Value value,
    const char* message,
    const char** out);
bool bind_value(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt, int index, X3Value value);
bool bind_params(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt, X3Value params);
X3Value column_value(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt, int column);
X3Value row_list(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt);
X3Value row_dict(const X3PackageHost* host, X3Runtime* runtime, sqlite3_stmt* stmt);

} // namespace xlang3_sqlite
