// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// SPDX-License-Identifier: Apache-2.0

#include "xlang3/xlang3.h"

#include <exception>
#include <iostream>

class CantorHost {
public:
  void MarkReady() { ready_ = true; }
  bool IsReady() const { return ready_; }

  BEGIN_PACKAGE(CantorHost)
    APISET().AddFunc<0>("mark_ready", &CantorHost::MarkReady);
  END_PACKAGE

private:
  bool ready_ = false;
};

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: xlang3_run_host_script <script.py>\n";
    return 2;
  }

  try {
    CantorHost host;
    X::Runtime runtime;
    runtime.RegisterPackage("cantor", host);
    runtime.EvalFile(argv[1]);

    std::cout << "Host ready: " << std::boolalpha << host.IsReady() << '\n';
    return host.IsReady() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
