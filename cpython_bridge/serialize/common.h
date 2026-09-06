#pragma once
#include "graph.h"
#include <cstring>
#include <vector>

namespace x3py::graph {
constexpr uint32_t version = 1;
constexpr uint32_t max_nodes = 1000000;
constexpr uint64_t max_refs = 8000000;
constexpr uint64_t max_bytes = 512ull * 1024 * 1024;
enum class Kind : uint8_t {
  None, False, True, Ellipsis, NotImplemented, Int, Float, Complex,
  Text, Bytes, ByteArray, List, Tuple, Dict, Set, FrozenSet, Cell,
  Code, Function, Globals, Module, Imported, Class, Instance,
  StaticMethod, ClassMethod, Property, Method, Reduced
};
struct Ref {
  PyObject* p = nullptr;
  Ref() = default;
  explicit Ref(PyObject* value) : p(value) { if (!p) throw PythonError{}; }
  Ref(const Ref&) = delete;
  Ref& operator=(const Ref&) = delete;
  Ref(Ref&& other) noexcept : p(other.p) { other.p = nullptr; }
  Ref& operator=(Ref&& other) noexcept {
    if (this != &other) { Py_XDECREF(p); p = other.p; other.p = nullptr; }
    return *this;
  }
  ~Ref() { Py_XDECREF(p); }
  PyObject* release() { auto* result = p; p = nullptr; return result; }
};
[[noreturn]] inline void invalid(const char* message) {
  PyErr_SetString(PyExc_ValueError, message); throw PythonError{};
}
[[noreturn]] inline void unsupported(PyObject* object) {
  PyErr_Format(PyExc_TypeError, "CPython snapshot does not support type %.200s", Py_TYPE(object)->tp_name);
  throw PythonError{};
}
inline Ref attr(PyObject* object, const char* name) { return Ref(PyObject_GetAttrString(object, name)); }
inline void checked(int status) { if (status < 0) throw PythonError{}; }
inline bool python_instance_layout(PyTypeObject* type) {
  for (auto* base = type; base && base != &PyBaseObject_Type; base = base->tp_base)
    if (!(base->tp_flags & Py_TPFLAGS_HEAPTYPE) || base->tp_itemsize) return false;
  auto constructor = attr(reinterpret_cast<PyObject*>(type), "__new__");
  auto base_constructor = attr(reinterpret_cast<PyObject*>(&PyBaseObject_Type), "__new__");
  return constructor.p == base_constructor.p || PyFunction_Check(constructor.p);
}

// Fixed-width scalars use one stream transfer; swap only on big-endian hosts.
inline bool little_endian() {
  const uint16_t marker = 1;
  return *reinterpret_cast<const uint8_t*>(&marker) != 0;
}
template<class T> T little(T value) {
  if (little_endian()) return value;
  T result{};
  auto* src = reinterpret_cast<const uint8_t*>(&value);
  auto* dst = reinterpret_cast<uint8_t*>(&result);
  for (size_t i = 0; i < sizeof(T); ++i) dst[i] = src[sizeof(T) - 1 - i];
  return result;
}
struct Output {
  Engine& engine;
  X3Stream* stream;
  void data(const void* p, uint64_t size) {
    if (size > max_bytes - x3_stream_size(stream)) invalid("snapshot exceeds byte limit");
    engine.check(x3_stream_write(stream, p, size));
  }
  template<class T> void scalar(T value) { value = little(value); data(&value, sizeof(value)); }
};
struct Input {
  Engine& engine;
  X3Stream* stream;
  uint64_t remaining;
  void data(void* p, uint64_t size) {
    if (size > remaining) invalid("truncated CPython graph");
    engine.check(x3_stream_read(stream, p, size)); remaining -= size;
  }
  template<class T> T scalar() { T value; data(&value, sizeof(value)); return little(value); }
};
}
