#pragma once
#include "xlang3/abi/xtensor.h"
#include "xlang3/cpp/value.h"
#include <vector>
#include <stdexcept>

namespace X {
class TensorUse {
  X3TensorUse* use_ = nullptr;
public:
  explicit TensorUse(X3TensorUse* use = nullptr) : use_(use) {}
  TensorUse(const TensorUse&) = delete;
  TensorUse& operator=(const TensorUse&) = delete;
  TensorUse(TensorUse&& other) noexcept : use_(other.use_) { other.use_ = nullptr; }
  TensorUse& operator=(TensorUse&& other) noexcept {
    if (this != &other) { if (use_) x3_tensor_end_use(use_, nullptr); use_ = other.use_; other.use_ = nullptr; }
    return *this;
  }
  ~TensorUse() { if (use_) x3_tensor_end_use(use_, nullptr); }
  explicit operator bool() const { return use_ != nullptr; }
  // The default destructor is for synchronous access only. Async users must
  // publish a completion before destruction, including on exceptional paths.
  void Finish(const X3TensorCompletion* completion = nullptr) {
    if (!use_) return;
    if (x3_tensor_end_use(use_, completion) != X3_STATUS_OK)
      throw std::runtime_error("invalid tensor completion");
    use_ = nullptr;
  }
};
class Tensor : public Value {
  struct Adopt {};
  Tensor(X3PackageHost* host, X3Value value, Adopt) : Value(host, value, false) {}
  static void Check(X3PackageHost* host,X3Status status) {
    if (status!=X3_STATUS_OK) throw std::runtime_error(host ? host->runtime_last_error(host->runtime) : "tensor requires a host");
  }
public:
  Tensor() = default;
  static bool IsTensor(const Value& value) { return x3_tensor_is_tensor(value.raw()) != 0; }
  explicit Tensor(Value v) : Value(std::move(v)) { Info(); }
  // The runtime takes ownership only after a successful wrap. Views and graphs
  // share that ownership; no payload copy is made.
  static Tensor Wrap(X3PackageHost* host, const X3TensorInfo& info,
      void* owner = nullptr, void (*cleanup)(void*) = nullptr) {
    X3Value out = x3_value_invalid();
    Check(host, x3_tensor_wrap(host ? host->runtime : nullptr, &info, owner, cleanup, &out));
    return Tensor(host, out, Adopt{});
  }
  static Tensor Create(X3PackageHost* host,X3TensorDType dtype,const std::vector<int64_t>& shape,
      const void* data=nullptr,uint64_t bytes=0) {
    X3Value out=x3_value_invalid();
    Check(host,x3_tensor_create(host ? host->runtime : nullptr,dtype,shape.data(),static_cast<uint32_t>(shape.size()),data,bytes,&out));
    return Tensor(host,out,Adopt{});
  }
  static Tensor Input(X3PackageHost* host,const char* name,X3TensorDType dtype,const std::vector<int64_t>& shape) {
    X3Value out=x3_value_invalid();
    Check(host,x3_tensor_input(host ? host->runtime : nullptr,name,dtype,shape.data(),static_cast<uint32_t>(shape.size()),&out));
    return Tensor(host,out,Adopt{});
  }
  X3TensorInfo Info() const {
    X3TensorInfo info{}; info.size=sizeof(info); Check(host(),x3_tensor_info(runtime(),raw(),&info)); return info;
  }
  TensorUse Acquire(X3TensorAccess access = X3_TENSOR_READ,
      const X3TensorExecution* execution = nullptr) const {
    X3TensorUse* use = nullptr;
    Check(host(), x3_tensor_begin_use(runtime(), raw(), access, execution, &use));
    return TensorUse(use);
  }
  static TensorUse AcquireMany(const std::vector<std::pair<Tensor, X3TensorAccess>>& tensors,
      const X3TensorExecution* execution = nullptr) {
    if (tensors.empty()) return TensorUse();
    auto* host = tensors.front().first.host();
    std::vector<X3TensorUseRequest> requests;
    requests.reserve(tensors.size());
    for (const auto& entry : tensors) {
      if (entry.first.host() != host) throw std::runtime_error("tensor uses require one host");
      requests.push_back({entry.first.raw(), entry.second});
    }
    X3TensorUse* use = nullptr;
    Check(host, x3_tensor_begin_uses(host ? host->runtime : nullptr, requests.data(),
        static_cast<uint32_t>(requests.size()), execution, &use));
    return TensorUse(use);
  }
  Tensor View(const std::vector<int64_t>& shape,const std::vector<int64_t>& strides,uint64_t offset=0) const {
    if (shape.size()!=strides.size()) throw std::runtime_error("tensor view rank mismatch");
    X3Value out=x3_value_invalid();
    Check(host(),x3_tensor_view(runtime(),raw(),shape.data(),strides.data(),static_cast<uint32_t>(shape.size()),offset,&out));
    return Tensor(host(),out,Adopt{});
  }
  Value Apply(const char* operation,const Value& rhs) const {
    X3Value out=x3_value_invalid(); Check(host(),x3_tensor_apply(runtime(),raw(),rhs.raw(),operation,&out)); return Value(host(),out,false);
  }
  Value operator+(const Value& rhs) const { return Apply("add",rhs); }
  Value operator-(const Value& rhs) const { return Apply("sub",rhs); }
  Value operator*(const Value& rhs) const { return Apply("mul",rhs); }
  Value operator/(const Value& rhs) const { return Apply("div",rhs); }
};
class TensorGraph : public Value {
  void Check(X3Status status) const {
    if (status!=X3_STATUS_OK) throw std::runtime_error(host() ? host()->runtime_last_error(runtime()) : "graph requires a host");
  }
public:
  static bool IsGraph(const Value& value) { return x3_tensor_is_graph(value.raw()) != 0; }
  explicit TensorGraph(const Value& outputs) {
    X3Value out=x3_value_invalid();
    auto status=x3_tensor_graph(outputs.runtime(),outputs.raw(),&out);
    if (status!=X3_STATUS_OK) throw std::runtime_error(outputs.host() ? outputs.host()->runtime_last_error(outputs.runtime()) : "graph requires a host");
    Value::operator=(Value(outputs.host(),out,false));
  }
  Value Run(const Value& bindings) const {
    X3Value out=x3_value_invalid(); Check(x3_tensor_graph_run(runtime(),raw(),bindings.raw(),&out)); return Value(host(),out,false);
  }
  Value Run() const { return Run(Value::Dict(host())); }
  Value Inspect() const {
    X3Value out=x3_value_invalid(); Check(x3_tensor_graph_inspect(runtime(),raw(),&out)); return Value(host(),out,false);
  }
  void Replay(X3TensorVisitor visitor=nullptr,void* context=nullptr) const { Check(x3_tensor_graph_replay(runtime(),raw(),visitor,context)); }
};
}
