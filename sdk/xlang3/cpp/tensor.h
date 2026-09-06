#pragma once
#include "xlang3/abi/xtensor.h"
#include "xlang3/cpp/value.h"
#include <vector>
#include <stdexcept>

namespace X {
class Tensor : public Value {
  static void Check(X3PackageHost* host,X3Status status) {
    if (status!=X3_STATUS_OK) throw std::runtime_error(host ? host->runtime_last_error(host->runtime) : "tensor requires a host");
  }
public:
  Tensor() = default;
  explicit Tensor(Value v) : Value(std::move(v)) { Info(); }
  static Tensor Create(X3PackageHost* host,X3TensorDType dtype,const std::vector<int64_t>& shape,
      const void* data=nullptr,uint64_t bytes=0) {
    X3Value out=x3_value_invalid();
    Check(host,x3_tensor_create(host ? host->runtime : nullptr,dtype,shape.data(),static_cast<uint32_t>(shape.size()),data,bytes,&out));
    return Tensor(Value(host,out,false));
  }
  static Tensor Input(X3PackageHost* host,const char* name,X3TensorDType dtype,const std::vector<int64_t>& shape) {
    X3Value out=x3_value_invalid();
    Check(host,x3_tensor_input(host ? host->runtime : nullptr,name,dtype,shape.data(),static_cast<uint32_t>(shape.size()),&out));
    return Tensor(Value(host,out,false));
  }
  X3TensorInfo Info() const {
    X3TensorInfo info{}; info.size=sizeof(info); Check(host(),x3_tensor_info(runtime(),raw(),&info)); return info;
  }
  Tensor View(const std::vector<int64_t>& shape,const std::vector<int64_t>& strides,uint64_t offset=0) const {
    if (shape.size()!=strides.size()) throw std::runtime_error("tensor view rank mismatch");
    X3Value out=x3_value_invalid();
    Check(host(),x3_tensor_view(runtime(),raw(),shape.data(),strides.data(),static_cast<uint32_t>(shape.size()),offset,&out));
    return Tensor(Value(host(),out,false));
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
