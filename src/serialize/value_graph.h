/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once
#include "xlang3/value.h"
#include "serialize/xlang_stream.h"

namespace xlang3 { class Runtime; }
namespace xlang3::serialize {
bool write_value_graph(Runtime& runtime, XLangStream& stream, const Value& value, std::string& error);
bool read_value_graph(Runtime& runtime, XLangStream& stream, Value& value, std::string& error);
}
