#include "xlang3/xlang3.h"
#include <csignal>
#include <iostream>
#include <stdexcept>

// An embedded Runtime must neither overwrite a host disposition on startup
// nor leave Python callbacks installed after its own state is destroyed.
namespace {
volatile std::sig_atomic_t host_calls = 0;
void host_handler(int) { host_calls = host_calls + 1; }
}

int main() {
#ifndef _WIN32
  struct sigaction previous{}, owned{};
  owned.sa_handler = host_handler;
  owned.sa_flags = SA_RESTART;
  sigemptyset(&owned.sa_mask);
  if (sigaction(SIGTERM, &owned, &previous) != 0) return 1;
  try {
    for (int attempt = 0; attempt != 3; ++attempt) {
      {
        X::Runtime runtime;
        X::Module builtins(runtime, "builtins");
        auto globals = runtime.Dict();
        X::Value result;
        const char* code =
            "import signal, os, time\n"
            "assert signal.getsignal(signal.SIGTERM) is None\n"
            "seen = []\n"
            "signal.signal(signal.SIGTERM, lambda signum, frame: seen.append(signum))\n"
            "os.kill(os.getpid(), signal.SIGTERM)\n"
            "deadline = time.monotonic() + 3\n"
            "while not seen and time.monotonic() < deadline:\n"
            "    pass\n"
            "assert seen == [signal.SIGTERM]\n"
            "signal.siginterrupt(signal.SIGTERM, False)\n";
        if (!builtins["exec"].Call({X::Value(runtime, code), globals}, result))
          throw X::Error(runtime.LastError());
        struct sigaction action{};
        if (sigaction(SIGTERM, nullptr, &action) != 0 ||
            (action.sa_flags & SA_RESTART) == 0)
          throw std::runtime_error("siginterrupt(False) did not enable restart");
        if (!builtins["exec"].Call({X::Value(runtime,
            "signal.siginterrupt(signal.SIGTERM, True)\n"), globals}, result))
          throw X::Error(runtime.LastError());
        if (sigaction(SIGTERM, nullptr, &action) != 0 ||
            (action.sa_flags & SA_RESTART) != 0)
          throw std::runtime_error("siginterrupt(True) did not disable restart");
      }
      struct sigaction actual{};
      if (sigaction(SIGTERM, nullptr, &actual) != 0 ||
          actual.sa_handler != host_handler || (actual.sa_flags & SA_RESTART) == 0)
        throw std::runtime_error("embedding host disposition was not restored");
      std::raise(SIGTERM);
      if (host_calls != attempt + 1)
        throw std::runtime_error("embedding host handler did not receive signal");
    }
  } catch (const std::exception& error) {
    sigaction(SIGTERM, &previous, nullptr);
    std::cerr << error.what() << '\n';
    return 1;
  }
  sigaction(SIGTERM, &previous, nullptr);
#endif
  std::cout << "embedded signal ownership: ok\n";
  return 0;
}
