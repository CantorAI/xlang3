#include "tensor_internal.h"
#include "xlang3/c_api_bridge.h"
#include "xlang3/object_model.h"
#include "runtime/modules/thread/runtime_lock.h"
#include <cstring>
#include <map>

namespace {
using namespace xlang3;
using namespace xlang3::tensor;
template<class F> X3Status protect(X3Runtime* runtime, F f) {
  XlangRuntimeExecutionGuard guard;
  auto* rt = reinterpret_cast<Runtime*>(runtime);
  if (!rt) return X3_STATUS_ERROR;
  try { f(*rt); return X3_STATUS_OK; }
  catch (const std::exception& e) { rt->set_last_error(e.what()); return X3_STATUS_ERROR; }
  catch (...) { rt->set_last_error("unknown tensor error"); return X3_STATUS_ERROR; }
}
Value value(X3Value v) { std::string error; auto out = from_c_value(v,error); if (!error.empty()) throw std::runtime_error(error); return out; }
std::vector<int64_t> shape(const int64_t* p, uint32_t rank) {
  if (rank > 32 || (rank && !p)) throw std::runtime_error("invalid tensor shape pointer/rank");
  auto out = rank ? std::vector<int64_t>(p,p+rank) : std::vector<int64_t>{}; count(out); return out;
}
void result(X3Value* out, Value v) { if (!out) throw std::runtime_error("null tensor result"); *out = to_c_value(v); }
Tensor& checked(Runtime& rt, const Value& v) {
  auto* t = get(v); if (!t || t->runtime != &rt) throw std::runtime_error("expected a tensor belonging to this runtime"); return *t;
}
Graph& graph(Runtime& rt, const Value& v) {
  auto* g = get_graph(v); if (!g || g->runtime != &rt) throw std::runtime_error("expected a graph belonging to this runtime"); return *g;
}
}
struct X3TensorUse {
  std::vector<std::unique_ptr<xlang3::tensor::StorageUse>> uses;
};
extern "C" {
X3Status x3_tensor_begin_use(X3Runtime* rt, X3Value v, X3TensorAccess access,
    const X3TensorExecution* execution, X3TensorUse** out) {
  X3TensorUseRequest request{v, access};
  return x3_tensor_begin_uses(rt, &request, 1, execution, out);
}
X3Status x3_tensor_begin_uses(X3Runtime* rt, const X3TensorUseRequest* requests, uint32_t count,
    const X3TensorExecution* execution, X3TensorUse** out) {
  return protect(rt,[&](Runtime& r) {
    if (!out || !count || !requests || (execution && execution->size != sizeof(*execution)))
      throw std::runtime_error("invalid tensor execution descriptor/result");
    *out = nullptr;
    std::map<Storage*, std::pair<std::shared_ptr<Storage>, X3TensorAccess>> storages;
    for (uint32_t index = 0; index < count; ++index) {
      const auto& request = requests[index];
      if (request.access != X3_TENSOR_READ && request.access != X3_TENSOR_WRITE)
        throw std::runtime_error("invalid tensor access mode");
      auto owner = value(request.tensor);
      auto storage = checked(r, owner).storage;
      if (!storage || (request.access == X3_TENSOR_WRITE && storage->readonly))
        throw std::runtime_error("tensor access requires writable/materialized storage");
      auto inserted = storages.emplace(storage.get(), std::make_pair(storage, request.access));
      if (request.access == X3_TENSOR_WRITE) inserted.first->second.second = X3_TENSOR_WRITE;
    }
    X3TensorExecution host{}; host.size = sizeof(host);
    auto uses = std::make_unique<X3TensorUse>();
    uses->uses.reserve(storages.size());
    for (const auto& entry : storages)
      uses->uses.push_back(std::make_unique<StorageUse>(entry.second.first, entry.second.second,
          execution ? *execution : host, true));
    *out = uses.release();
  });
}
X3Status x3_tensor_end_use(X3TensorUse* use, const X3TensorCompletion* completion) {
  if (!use || (completion && (completion->size != sizeof(*completion) ||
      !completion->wait || !completion->query || !completion->cleanup))) return X3_STATUS_ERROR;
  try {
    std::shared_ptr<Completion> shared;
    if (completion) {
      shared = use->uses.front()->completion->front().completion;
      shared->callback = *completion;
    }
    for (auto& member : use->uses) member->finish(shared);
    delete use;
    return X3_STATUS_OK;
  }
  catch (...) { return X3_STATUS_ERROR; }
}
int32_t x3_tensor_is_tensor(X3Value v) {
  XlangRuntimeExecutionGuard guard;
  if (v.tag != X3_TAG_OBJECT || !v.as.obj) return 0;
  auto* object = reinterpret_cast<Object*>(v.as.obj);
  if (object->kind != ObjectKind::Instance) return 0;
  auto* instance = reinterpret_cast<InstanceObject*>(object);
  return instance->native_type == tensor_type && instance->native_data != nullptr;
}
int32_t x3_tensor_is_graph(X3Value v) {
  XlangRuntimeExecutionGuard guard;
  if (v.tag != X3_TAG_OBJECT || !v.as.obj) return 0;
  auto* object = reinterpret_cast<Object*>(v.as.obj);
  if (object->kind != ObjectKind::Instance) return 0;
  auto* instance = reinterpret_cast<InstanceObject*>(object);
  return instance->native_type == graph_type && instance->native_data != nullptr;
}
X3Status x3_tensor_create(X3Runtime* rt, X3TensorDType dtype, const int64_t* dims, uint32_t rank,
    const void* data, uint64_t bytes, X3Value* out) {
  return protect(rt,[&](Runtime& r) {
    if (!out || (!data && bytes)) throw std::runtime_error("invalid tensor create arguments");
    auto t = allocate(r,dtype,shape(dims,rank));
    if (data && bytes != t->storage->bytes) throw std::runtime_error("tensor data size mismatch");
    if (bytes) std::memcpy(t->storage->data,data,static_cast<size_t>(bytes));
    result(out,wrap_tensor(r,std::move(t)));
  });
}
X3Status x3_tensor_wrap(X3Runtime* rt, const X3TensorInfo* info, void* owner, void (*cleanup)(void*), X3Value* out) {
  return protect(rt,[&](Runtime& r) {
    if (!info || info->size != sizeof(*info) || !out || info->symbolic) throw std::runtime_error("invalid tensor wrap descriptor");
    auto t = std::make_unique<Tensor>(); t->dtype=info->dtype; t->shape=shape(info->shape,info->rank);
    t->strides=info->strides ? std::vector<int64_t>(info->strides,info->strides+info->rank) : contiguous_strides(t->shape,item_size(t->dtype));
    auto storage = std::make_shared<Storage>(); t->storage=storage;
    storage->data=info->data; storage->bytes=info->byte_size; storage->device=info->device_type;
    storage->device_id=info->device_id; storage->readonly=info->readonly != 0;
    if (reinterpret_cast<uintptr_t>(info->data) % item_size(t->dtype)) throw std::runtime_error("unaligned tensor storage");
    validate_layout(*t); auto v=wrap_tensor(r,std::move(t)); result(out,std::move(v));
    storage->owner=owner; storage->cleanup=cleanup;
  });
}
X3Status x3_tensor_input(X3Runtime* rt, const char* name, X3TensorDType dtype, const int64_t* dims, uint32_t rank, X3Value* out) {
  return protect(rt,[&](Runtime& r) {
    if (!name || !*name || !out) throw std::runtime_error("symbolic input requires a name/result");
    auto t=std::make_unique<Tensor>(); t->dtype=dtype; t->name=name; t->shape=shape(dims,rank);
    t->strides=contiguous_strides(t->shape,item_size(dtype)); result(out,wrap_tensor(r,std::move(t)));
  });
}
X3Status x3_tensor_info(X3Runtime* rt, X3Value v, X3TensorInfo* info) {
  return protect(rt,[&](Runtime& r) {
    if (!info || info->size != sizeof(*info)) throw std::runtime_error("invalid tensor info descriptor");
    auto owner=value(v); auto& t=checked(r,owner);
    *info={}; info->size=sizeof(*info); info->dtype=t.dtype; info->id=t.id;
    info->rank=static_cast<uint32_t>(t.shape.size()); info->shape=t.shape.data(); info->strides=t.strides.data();
    info->symbolic=!t.storage;
    if (t.registration) {
      info->rank=UINT32_MAX; info->dtype=static_cast<X3TensorDType>(0);
      info->shape=nullptr; info->strides=nullptr;
    }
    if (t.storage) {
      info->data=t.storage->data ? static_cast<unsigned char*>(t.storage->data)+t.offset : nullptr;
      info->byte_size=t.storage->bytes-t.offset; info->device_type=t.storage->device;
      info->device_id=t.storage->device_id; info->readonly=t.storage->readonly;
    }
  });
}
X3Status x3_tensor_view(X3Runtime* rt, X3Value v, const int64_t* dims, const int64_t* strides, uint32_t rank, uint64_t offset, X3Value* out) {
  return protect(rt,[&](Runtime& r) {
    auto owner=value(v); auto& source=checked(r,owner);
    if (!source.storage || !out || offset > UINT64_MAX-source.offset) throw std::runtime_error("view requires materialized storage and valid offset/result");
    auto t=std::make_unique<Tensor>(source); t->shape=shape(dims,rank);
    t->strides=strides ? std::vector<int64_t>(strides,strides+rank) : contiguous_strides(t->shape,item_size(t->dtype));
    t->offset+=offset; validate_layout(*t); result(out,wrap_tensor(r,std::move(t)));
  });
}
X3Status x3_tensor_register_operator(X3Runtime* rt, const X3TensorOperatorDef* def, X3Value* out) {
  return protect(rt,[&](Runtime& r) {
    if (!def || def->size != sizeof(*def) || !def->provider || !*def->provider || !def->name || !*def->name ||
        (def->arity != 1 && def->arity != 2) || (def->flags & ~X3_TENSOR_ORDERED) || !out)
      throw std::runtime_error("invalid tensor operator registration");
    auto registration=std::make_shared<Registration>(); registration->provider=def->provider; registration->name=def->name;
    registration->arity=def->arity; registration->flags=def->flags; registration->replay=def->replay; registration->context=def->context;
    result(out,make_factory(r,registration)); registration->cleanup=def->cleanup;
  });
}
X3Status x3_tensor_apply(X3Runtime* rt, X3Value left, X3Value right, const char* op, X3Value* out) {
  return protect(rt,[&](Runtime& r) { if (!op || !out) throw std::runtime_error("invalid tensor apply arguments"); result(out,apply(r,value(left),value(right),op)); });
}
X3Status x3_tensor_graph(X3Runtime* rt, X3Value outputs, X3Value* out) {
  return protect(rt,[&](Runtime& r) { if (!out) throw std::runtime_error("null graph result"); result(out,build_graph(r,value(outputs))); });
}
X3Status x3_tensor_graph_run(X3Runtime* rt, X3Value g, X3Value bindings, X3Value* out) {
  return protect(rt,[&](Runtime& r) { if (!out) throw std::runtime_error("null graph result"); auto v=value(g); result(out,run(r,graph(r,v),value(bindings))); });
}
X3Status x3_tensor_graph_replay(X3Runtime* rt, X3Value g, X3TensorVisitor visitor, void* context) {
  return protect(rt,[&](Runtime& r) { auto v=value(g); replay(r,graph(r,v),visitor,context); });
}
X3Status x3_tensor_graph_inspect(X3Runtime* rt, X3Value g, X3Value* out) {
  return protect(rt,[&](Runtime& r) { if (!out) throw std::runtime_error("null graph result"); auto v=value(g); result(out,inspect(graph(r,v))); });
}
X3Status x3_tensor_graph_outputs(X3Runtime* rt, X3Value g, X3Value* out) {
  return protect(rt,[&](Runtime& r) { if (!out) throw std::runtime_error("null graph result"); auto v=value(g); result(out,snapshot(graph(r,v).outputs)); });
}
}
