#include "xlang3/xlang3.h"
#include <iostream>
#include <memory>
#ifndef _WIN32
#include <signal.h>
#include <stdexcept>

namespace {
void embedded_host_signal(int) {}

struct SignalDispositionGuard {
  struct sigaction original {};
  SignalDispositionGuard() {
    struct sigaction host_action {};
    host_action.sa_handler = embedded_host_signal;
    sigemptyset(&host_action.sa_mask);
    sigaddset(&host_action.sa_mask, SIGUSR1);
    host_action.sa_flags = SA_RESTART;
    if (::sigaction(SIGUSR2, &host_action, &original) != 0)
      throw std::runtime_error("cannot prepare host signal disposition");
  }
  ~SignalDispositionGuard() { (void)::sigaction(SIGUSR2, &original, nullptr); }
};

bool same_signal_disposition(const struct sigaction& a, const struct sigaction& b) {
  if (a.sa_handler != b.sa_handler || a.sa_flags != b.sa_flags) return false;
  for (int number = 1; number < NSIG; ++number) {
    if (sigismember(&a.sa_mask, number) != sigismember(&b.sa_mask, number)) return false;
  }
  return true;
}

void check_signal_runtime_cleanup() {
  SignalDispositionGuard guard;
  struct sigaction before {}, during {}, after {};
  if (::sigaction(SIGUSR2, nullptr, &before) != 0)
    throw std::runtime_error("cannot inspect host signal disposition");
  {
    X::Runtime runtime;
    X::Module builtins(runtime, "builtins");
    auto globals = runtime.Dict();
    X::Value result;
    if (!builtins["exec"].Call({X::Value(runtime, "import _signal\n"), globals}, result))
      throw X::Error(runtime.LastError());
    if (::sigaction(SIGUSR2, nullptr, &during) != 0 ||
        !same_signal_disposition(before, during))
      throw std::runtime_error("signal import changed the embedding host disposition");
    if (!builtins["exec"].Call({X::Value(runtime,
        "_signal.signal(_signal.SIGUSR2, lambda signum, frame: None)\n"), globals}, result))
      throw X::Error(runtime.LastError());
    if (::sigaction(SIGUSR2, nullptr, &during) != 0 ||
        during.sa_handler == before.sa_handler)
      throw std::runtime_error("runtime did not install its POSIX signal handler");
  }
  if (::sigaction(SIGUSR2, nullptr, &after) != 0 ||
      !same_signal_disposition(before, after))
    throw std::runtime_error("runtime did not restore the embedding host disposition");
  std::cout << "POSIX signal disposition restored after runtime teardown\n";
}
} // namespace
#endif

struct GlobalHost {
  std::unique_ptr<X::Runtime> runtime;
  X::Value retained;
};
static GlobalHost host;

int main() {
  try {
#ifndef _WIN32
    check_signal_runtime_cleanup();
#endif
    host.runtime = std::make_unique<X::Runtime>();
    X::Module builtins(*host.runtime, "builtins");
    auto globals = host.runtime->Dict();
    if (!builtins["eval"].Call({X::Value(*host.runtime,
        "[[], {}, {1}, (1, 2), type('Sample', (), {})()]"), globals}, host.retained)) {
      throw X::Error(host.runtime->LastError());
    }
    // Initialize exception TLS before main returns. The global runtime must still
    // be able to clear it after the main thread's TLS destructors have run.
    X::Value result;
    if (!builtins["exec"].Call({X::Value(*host.runtime,
        "try:\n"
        "    raise RuntimeError('static runtime shutdown')\n"
        "except RuntimeError:\n"
        "    pass\n"), globals}, result)) {
      throw X::Error(host.runtime->LastError());
    }
    std::cout << "global runtime retained until process teardown\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
