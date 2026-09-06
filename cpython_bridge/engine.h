#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "xlang3/xlang3.h"
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <atomic>

namespace x3py {
// Every engine operation, including releases, runs without the GIL. This avoids
// blocking a Python thread holding the GIL behind a running XLang3 operation.
struct AllowThreads {
  PyThreadState* state = PyEval_SaveThread();
  ~AllowThreads() { PyEval_RestoreThread(state); }
};

struct PythonPayload;
struct Engine : std::enable_shared_from_this<Engine> {
  X3Runtime* runtime = nullptr;
  bool owns_runtime = true;
  std::recursive_mutex mutex;
  std::mutex foreign_mutex;
  std::unordered_map<PyObject*, PythonPayload*> foreign;
  std::atomic<bool> closing{false};
  PyObject* owner = nullptr; // Borrowed GC anchor, retained by module and proxies.
  PyTypeObject* proxy_type = nullptr;
  X3PackageHost* python_host = nullptr;
  X3Value python_class = x3_value_invalid();
  // Borrowed entries, inserted/removed only while holding the GIL.
  std::unordered_map<X3Object*, PyObject*> proxies;
  std::unordered_map<void*, PyObject*> buffers; // Borrowed Py_buffer owners, under GIL.
  Engine();
  explicit Engine(X3Runtime* borrowed) : runtime(borrowed), owns_runtime(false) {}
  ~Engine();
  static unsigned& depth(Engine* engine);
  void ensure_open() const {
    if (closing) throw std::runtime_error("CPython bridge is closed");
  }
  // Guarded runtime APIs own the VM lock themselves. Do not hold the allocation
  // mutex over them: IPC may call back on a different thread before returning.
  template<class F> auto execute(F&& fn) {
    ensure_open();
    AllowThreads allow;
    return fn();
  }
  template<class F> auto run(F&& fn) {
    AllowThreads allow;
    struct Guard {
      Engine* engine;
      Guard(Engine* e) : engine(e) {
        auto& count = Engine::depth(e);
        if (!count) e->mutex.lock();
        ++count;
      }
      ~Guard() { if (!--Engine::depth(engine)) engine->mutex.unlock(); }
    } guard(this);
    return fn();
  }
  void check(X3Status status);
  void check_protocol(X3Status status, const char* fallback);
};

struct ProtocolError : std::runtime_error {
  std::string type;
  ProtocolError(std::string kind, std::string message)
      : std::runtime_error(message), type(std::move(kind)) {}
};

struct ModuleState {
  std::shared_ptr<Engine>* engine = nullptr;
  PyObject* proxy_type = nullptr;
  PyObject* owner = nullptr;
};

// The native-package dispatcher releases the VM lock before calling us. Release
// our embedding lock as well before entering CPython, then restore it without GIL.
struct EnterPython {
  Engine* engine;
  unsigned saved_depth;
  PyGILState_STATE gil;
  explicit EnterPython(Engine* e) : engine(e), saved_depth(Engine::depth(e)) {
    if (saved_depth) { Engine::depth(e) = 0; e->mutex.unlock(); }
    gil = PyGILState_Ensure();
  }
  ~EnterPython() {
    PyGILState_Release(gil);
    if (saved_depth) { engine->mutex.lock(); Engine::depth(engine) = saved_depth; }
  }
};

struct PythonError {};
PyObject* translate_exception();
template<class F> PyObject* protect(F&& fn) noexcept {
  try { return fn(); } catch (...) { return translate_exception(); }
}

struct OwnedValue {
  std::shared_ptr<Engine> engine;
  X3Value value = x3_value_invalid();
  OwnedValue(std::shared_ptr<Engine> owner, X3Value v) : engine(std::move(owner)), value(v) {}
  OwnedValue(const OwnedValue&) = delete;
  ~OwnedValue() { engine->run([&] { x3_value_release(value); }); }
  X3Value detach() { auto v = value; value = x3_value_invalid(); return v; }
};
}
