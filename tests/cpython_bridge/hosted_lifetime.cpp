#include "xlang3/xlang3.h"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
  try {
    require(argc == 3, "expected modules and fixtures directories");
    for (int iteration = 0; iteration < 10; ++iteration) {
      X::Runtime runtime;
      runtime.AddImportRoot(argv[1]);
      runtime.AddImportRoot(argv[2]);
      X::Module cpython(runtime, "cpython");
      auto python = cpython["importModule"]("hosted_fixture", argv[2]);
      if (!python.IsValid()) throw std::runtime_error("CPython import failed: " + runtime.LastError());
      if (iteration) require(python["saved_is_closed"]().ToInt64() == 1, "retained callback was not invalidated");
      auto counter = python["Counter"](10);
      require(counter["add"](2).ToInt64() == 12, "SDK call failed");
      require(counter.SetAttr("value", X::Value(runtime, 23)), "SDK setattr failed");
      require(counter["value"].ToInt64() == 23, "SDK getattr failed");
      X::Module client(runtime, "hosted_client");
      require(client["dynamic_test"]().ToInt64() == 1, "dynamic method lookup failed");
      require(python["threaded"](client["callback"], 39).ToInt64() == 42, "SDK threaded callback failed");
      require(python["retain"](client["callback"]).IsValid(), "callback retention failed");
    }
    std::cout << "Hosted CPython: SDK calls and 10 runtime lifecycles PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
