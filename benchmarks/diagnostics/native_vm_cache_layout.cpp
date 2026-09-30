/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0.
*/

// Compile this standalone diagnostic with the same architecture/compiler as
// the Release runtime. It reports actual native layout, without executing or
// instrumenting Python workloads.
#include "../../src/executor/xlang_vm/xlang_frame.h"

#include <cstdio>

int main() {
  using namespace xlang3;
  std::printf("Value=%zu\n", sizeof(Value));
  std::printf("XlangVMInstrCacheCore=%zu\n", sizeof(XlangVMInstrCacheCore));
  std::printf("GlobalSiteCache=%zu\n", sizeof(GlobalSiteCache));
  std::printf("CallSiteCache=%zu\n", sizeof(CallSiteCache));
  std::printf("AttrSiteCache=%zu\n", sizeof(AttrSiteCache));
  std::printf("XlangVMInstrCache=%zu\n", sizeof(XlangVMInstrCache));
  std::printf("XlangVMPreparedFunctionState=%zu\n", sizeof(XlangVMPreparedFunctionState));
  std::printf("XlangVMFrame=%zu\n", sizeof(XlangVMFrame));
}
