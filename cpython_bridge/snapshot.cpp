#include "snapshot.h"
#include "xlang_object.h"
#include "serialize/graph.h"
#include <cstring>
#include <limits>

namespace x3py {
namespace {
constexpr size_t header_size = 24;
constexpr char magic[] = "X3PYOBJ";
struct Ref {
  PyObject* value;
  ~Ref() { Py_XDECREF(value); }
};
void put32(char* out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<char>(value >> (8 * i));
}
uint32_t get32(const char* in) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= uint32_t(static_cast<unsigned char>(in[i])) << (8 * i);
  return value;
}
}

PyObject* dump_snapshot(const std::shared_ptr<Engine>& engine, PyObject* object) {
  engine->ensure_open();
  bool native = PyObject_TypeCheck(object, engine->proxy_type);
  X3Stream* stream = nullptr;
  engine->execute([&] { engine->check(x3_stream_create(engine->runtime, &stream)); });
  struct StreamOwner { X3Stream* stream; ~StreamOwner() { x3_stream_destroy(stream); } } owner{stream};
  if (native) {
    auto* proxy = reinterpret_cast<XlangObject*>(object);
    if (*proxy->engine != engine) throw std::invalid_argument("cannot mix XLang3 runtimes");
    engine->execute([&] { engine->check(x3_value_to_stream(engine->runtime, proxy->value, stream)); });
  } else {
    write_python_graph(*engine, stream, object);
  }
  auto size = x3_stream_size(stream);
  if (size > PY_SSIZE_T_MAX - header_size) throw std::overflow_error("snapshot too large");
  Ref result{PyBytes_FromStringAndSize(nullptr, static_cast<Py_ssize_t>(size + header_size))};
  if (!result.value) return nullptr;
  char* out = PyBytes_AS_STRING(result.value);
  std::memcpy(out, magic, 8);
  put32(out + 8, native ? 2 : 3);
  put32(out + 12, PY_VERSION_HEX);
  put32(out + 16, static_cast<uint32_t>(size));
  put32(out + 20, static_cast<uint32_t>(static_cast<uint64_t>(size) >> 32));
  engine->check(x3_stream_copy(stream, out + header_size, size));
  auto* value = result.value; result.value = nullptr; return value;
}

PyObject* load_snapshot(const std::shared_ptr<Engine>& engine, PyObject* object, bool trusted) {
  engine->ensure_open();
  if (!trusted) {
    PyErr_SetString(PyExc_ValueError, "snapshot loading can execute code; pass trusted=True only for trusted input");
    return nullptr;
  }
  Py_buffer buffer{};
  if (PyObject_GetBuffer(object, &buffer, PyBUF_CONTIG_RO) < 0) return nullptr;
  struct Release { Py_buffer* buffer; ~Release() { PyBuffer_Release(buffer); } } release{&buffer};
  auto* bytes = static_cast<const char*>(buffer.buf);
  if (buffer.len < header_size || std::memcmp(bytes, magic, 8) || get32(bytes + 12) != PY_VERSION_HEX) {
    PyErr_SetString(PyExc_ValueError, "invalid snapshot or incompatible CPython version");
    return nullptr;
  }
  const uint64_t size = uint64_t(get32(bytes + 16)) | (uint64_t(get32(bytes + 20)) << 32);
  if (size != static_cast<uint64_t>(buffer.len - header_size)) {
    PyErr_SetString(PyExc_ValueError, "snapshot length mismatch");
    return nullptr;
  }
  const auto kind = get32(bytes + 8);
  if (kind != 2 && kind != 3) {
    PyErr_SetString(PyExc_ValueError, "unknown snapshot format");
    return nullptr;
  }
  X3StreamBlock block{bytes + header_size, size};
  X3Stream* stream = nullptr;
  X3Value result = x3_value_invalid();
  engine->execute([&] { engine->check(x3_stream_from_blocks(engine->runtime, &block, 1, &stream)); });
  if (kind == 3) {
    struct StreamOwner { X3Stream* stream; ~StreamOwner() { x3_stream_destroy(stream); } } owner{stream};
    return read_python_graph(*engine, stream);
  }
  try { engine->execute([&] { engine->check(x3_value_from_stream(engine->runtime, stream, &result)); }); }
  catch (...) { engine->run([&] { x3_stream_destroy(stream); }); throw; }
  engine->run([&] { x3_stream_destroy(stream); });
  return to_python(engine, engine->proxy_type, result);
}

PyObject* dumps_method(PyObject* module, PyObject* value) {
  return protect([&]() { return dump_snapshot(*static_cast<ModuleState*>(PyModule_GetState(module))->engine, value); });
}
PyObject* loads_method(PyObject* module, PyObject* args, PyObject* kwargs) {
  return protect([&]() -> PyObject* {
    PyObject* data;
    int trusted = 0;
    static const char* names[] = {"data", "trusted", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|$p:loads", const_cast<char**>(names), &data, &trusted)) return nullptr;
    return load_snapshot(*static_cast<ModuleState*>(PyModule_GetState(module))->engine, data, trusted != 0);
  });
}
}
