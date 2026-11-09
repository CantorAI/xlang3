// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// SPDX-License-Identifier: Apache-2.0

#include "xlang3/xlang3.h"

class demo {
public:
  int Answer() const { return 42; }

  X::Value ParseYaml(const X::Value& text) const {
    // Use the runtime that loaded this package.
    X::Module yaml(Host(), "yaml");
    X::Value result;
    if (!yaml.fn("loads").Call({text}, result)) {
      throw X::Error(Host()->runtime_last_error(Host()->runtime));
    }
    return result;
  }

  BEGIN_PACKAGE(demo)
    APISET().AddFunc<0>("answer", &demo::Answer);
    APISET().AddFunc<1>("parse_yaml", &demo::ParseYaml);
  END_PACKAGE
};

XLANG3_IMPLEMENT_PACKAGE(demo)
