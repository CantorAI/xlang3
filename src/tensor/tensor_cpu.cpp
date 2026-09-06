#include "tensor_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace xlang3::tensor {
Kernel resolve(const Tensor& t) {
  if (!t.registration) return t.storage ? Kernel::Constant : Kernel::Input;
  if (t.registration->provider != "cpu") return Kernel::Unsupported;
  static const std::unordered_map<std::string, Kernel> kernels{
    {"add", Kernel::Add}, {"sub", Kernel::Sub}, {"minus", Kernel::Sub}, {"mul", Kernel::Mul},
    {"div", Kernel::Div}, {"neg", Kernel::Neg}, {"relu", Kernel::Relu}, {"exp", Kernel::Exp},
    {"matmul", Kernel::Matmul}, {"sum", Kernel::Sum}, {"reshape", Kernel::Reshape}, {"permute", Kernel::Permute}};
  auto it = kernels.find(t.name); return it == kernels.end() ? Kernel::Unsupported : it->second;
}
namespace {
std::vector<int64_t> broadcast(const std::vector<int64_t>& a, const std::vector<int64_t>& b) {
  std::vector<int64_t> result((std::max)(a.size(), b.size()), 1);
  for (size_t i = 0; i < result.size(); ++i) {
    auto x = i < a.size() ? a[a.size()-1-i] : 1, y = i < b.size() ? b[b.size()-1-i] : 1;
    if (x != y && x != 1 && y != 1) throw std::runtime_error("tensor shapes do not broadcast");
    result[result.size()-1-i] = x == 1 ? y : x;
  }
  count(result); return result;
}
uint64_t offset_at(uint64_t index, const std::vector<int64_t>& output_shape,
    const std::vector<int64_t>& shape, const std::vector<int64_t>& strides) {
  uint64_t offset = 0;
  for (size_t i = output_shape.size(); i-- > 0;) {
    auto coordinate = index % output_shape[i]; index /= output_shape[i];
    if (i + shape.size() >= output_shape.size()) {
      auto d = i + shape.size() - output_shape.size();
      if (shape[d] != 1) offset += coordinate * strides[d];
    }
  }
  return offset;
}
template<class T, class U> T read_number(const void* data) { U x; std::memcpy(&x, data, sizeof(x)); return static_cast<T>(x); }
template<class T> struct Reader {
  const unsigned char* data = nullptr;
  const Tensor* tensor = nullptr;
  T scalar{};
  T (*read)(const void*) = nullptr;
  bool flat = false;
  Reader(const Value& v, const std::vector<int64_t>& output) {
    tensor = get(v);
    if (!tensor) {
      if constexpr (std::is_integral_v<T>) {
        if (v.tag == ValueTag::Double) throw std::runtime_error("float scalar cannot be narrowed to integer tensor");
        auto n = v.tag == ValueTag::Bool ? static_cast<int64_t>(v.as.b) : v.as.i64;
        if (n < (std::numeric_limits<T>::min)() || n > (std::numeric_limits<T>::max)())
          throw std::runtime_error("scalar is outside tensor dtype range");
        scalar = static_cast<T>(n);
      } else scalar = static_cast<T>(v.tag == ValueTag::Double ? v.as.f64 : v.tag == ValueTag::Bool ? v.as.b : v.as.i64);
      return;
    }
    if (!tensor->storage || tensor->storage->device != 0) throw std::runtime_error("CPU execution requires materialized CPU tensors");
    validate_layout(*tensor);
    data = tensor->storage->data ? static_cast<const unsigned char*>(tensor->storage->data) + tensor->offset : nullptr;
    flat = contiguous(*tensor) && tensor->shape == output;
    switch (tensor->dtype) {
      case X3_TENSOR_FLOAT32: read = read_number<T, float>; break;
      case X3_TENSOR_FLOAT64: read = read_number<T, double>; break;
      case X3_TENSOR_INT32: read = read_number<T, int32_t>; break;
      case X3_TENSOR_INT64: read = read_number<T, int64_t>; break;
      default: throw std::runtime_error("unsupported CPU dtype");
    }
  }
  T at(uint64_t i, const std::vector<int64_t>& output) const {
    if (!tensor) return scalar;
    return read(data + (flat ? i * item_size(tensor->dtype) : offset_at(i, output, tensor->shape, tensor->strides)));
  }
};
template<class T> T add(T a, T b) {
  if constexpr (std::is_integral_v<T>) {
    if ((b > 0 && a > (std::numeric_limits<T>::max)()-b) || (b < 0 && a < (std::numeric_limits<T>::min)()-b))
      throw std::runtime_error("integer tensor addition overflow");
  }
  return a + b;
}
template<class T> T multiply(T a, T b) {
  if constexpr (std::is_integral_v<T>) {
    const auto lo = (std::numeric_limits<T>::min)(), hi = (std::numeric_limits<T>::max)();
    if (a && b && ((a > 0 && (b > 0 ? a > hi/b : b < lo/a)) ||
        (a < 0 && (b > 0 ? a < lo/b : a < hi/b)))) throw std::runtime_error("integer tensor multiplication overflow");
  }
  return a * b;
}
template<class T> T element(Kernel op, T a, T b) {
  switch (op) {
    case Kernel::Add: return add(a,b);
    case Kernel::Sub:
      if constexpr (std::is_integral_v<T>) {
        if ((b > 0 && a < (std::numeric_limits<T>::min)()+b) || (b < 0 && a > (std::numeric_limits<T>::max)()+b))
          throw std::runtime_error("integer tensor subtraction overflow");
      }
      return a-b;
    case Kernel::Mul: return multiply(a,b);
    case Kernel::Div: if (b == 0) throw std::runtime_error("tensor division by zero"); return a/b;
    case Kernel::Neg:
      if constexpr (std::is_integral_v<T>) if (a == (std::numeric_limits<T>::min)()) throw std::runtime_error("integer tensor negation overflow");
      return -a;
    case Kernel::Relu: return (std::max)(a, T(0));
    case Kernel::Exp: return static_cast<T>(std::exp(a));
    default: throw std::runtime_error("invalid elementwise kernel");
  }
}
template<class T, Kernel Op> void elementwise(Tensor& out, const Reader<T>& a, const Reader<T>& b) {
  auto* dst = static_cast<T*>(out.storage->data);
  const auto n = count(out.shape);
  // Both dtype and operation are selected before entering the numeric loop.
  if (a.flat && a.tensor->dtype == out.dtype && (!b.tensor || (b.flat && b.tensor->dtype == out.dtype))) {
    auto* x = reinterpret_cast<const T*>(a.data);
    auto* y = reinterpret_cast<const T*>(b.data);
    if (y) for (uint64_t i=0; i<n; ++i) dst[i] = element(Op, x[i], y[i]);
    else for (uint64_t i=0; i<n; ++i) dst[i] = element(Op, x[i], b.scalar);
  } else for (uint64_t i=0; i<n; ++i) dst[i] = element(Op, a.at(i,out.shape), b.at(i,out.shape));
}

template<class T> void execute(Tensor& out, Kernel op, const std::vector<Value>& args,
    const Value& attrs) {
  Reader<T> a(args[0], out.shape), b(args.size() == 2 ? args[1] : Value::int64(0), out.shape);
  auto* dst = static_cast<T*>(out.storage->data);
  const auto n = count(out.shape);
  if (op == Kernel::Sum) {
    if (!a.tensor) throw std::runtime_error("sum requires a tensor");
    auto axis = attr(attrs, "axis");
    if (axis.tag == ValueTag::Invalid || axis.tag == ValueTag::None) {
      T total{};
      for (uint64_t i=0; i<count(a.tensor->shape); ++i) total = add(total, a.at(i, a.tensor->shape));
      dst[0] = total; return;
    }
    auto d = axis.as.i64; if (d < 0) d += a.tensor->shape.size();
    auto shape = a.tensor->shape, strides = a.tensor->strides;
    auto extent = shape[d], stride = strides[d]; shape.erase(shape.begin()+d); strides.erase(strides.begin()+d);
    for (uint64_t i=0; i<n; ++i) {
      T total{}; auto base = offset_at(i, shape, shape, strides);
      for (int64_t k=0; k<extent; ++k) total = add(total, a.read(a.data+base+k*stride));
      dst[i] = total;
    }
    return;
  }
  if (op == Kernel::Matmul) {
    const auto& x = *a.tensor; const auto& y = *b.tensor;
    auto rank = out.shape.size();
    auto m = out.shape[rank-2], cols = out.shape.back(), inner = x.shape.back();
    std::vector<int64_t> batch(out.shape.begin(), out.shape.end()-2);
    std::vector<int64_t> xs(x.shape.begin(), x.shape.end()-2), ys(y.shape.begin(), y.shape.end()-2);
    std::vector<int64_t> xt(x.strides.begin(), x.strides.end()-2), yt(y.strides.begin(), y.strides.end()-2);
    for (uint64_t z=0; z<count(batch); ++z) {
      auto xb = offset_at(z,batch,xs,xt), yb = offset_at(z,batch,ys,yt);
      for (int64_t i=0; i<m; ++i) for (int64_t j=0; j<cols; ++j) {
        T sum{};
        for (int64_t k=0; k<inner; ++k)
          sum = add(sum, multiply(a.read(a.data+xb+i*x.strides[x.strides.size()-2]+k*x.strides.back()),
              b.read(b.data+yb+k*y.strides[y.strides.size()-2]+j*y.strides.back())));
        dst[(z*m+i)*cols+j] = sum;
      }
    }
    return;
  }
  switch (op) {
    case Kernel::Add: return elementwise<T,Kernel::Add>(out,a,b);
    case Kernel::Sub: return elementwise<T,Kernel::Sub>(out,a,b);
    case Kernel::Mul: return elementwise<T,Kernel::Mul>(out,a,b);
    case Kernel::Div: return elementwise<T,Kernel::Div>(out,a,b);
    case Kernel::Neg: return elementwise<T,Kernel::Neg>(out,a,b);
    case Kernel::Relu: return elementwise<T,Kernel::Relu>(out,a,b);
    case Kernel::Exp: return elementwise<T,Kernel::Exp>(out,a,b);
    default: throw std::runtime_error("invalid elementwise kernel");
  }
}
}
Value compute(Runtime& rt, const Tensor&, Kernel op, const std::vector<Value>& args, const Value& attrs) {
  if (auto* keywords=value_as_dict(attrs)) for (const auto& keyword:keywords->entries) {
    const auto name=value_to_string(keyword.first);
    if (!((op==Kernel::Sum && name=="axis") || (op==Kernel::Reshape && name=="shape") ||
          (op==Kernel::Permute && name=="axes")))
      throw std::runtime_error("unsupported CPU operator keyword: " + name);
  }
  const bool binary = op == Kernel::Add || op == Kernel::Sub || op == Kernel::Mul || op == Kernel::Div || op == Kernel::Matmul;
  if (args.size() != (binary ? 2 : 1)) throw std::runtime_error("CPU operator arity mismatch");
  Tensor* first = nullptr;
  X3TensorDType dtype = X3_TENSOR_INT32;
  for (const auto& v : args) if (auto* t = get(v)) {
    if (!t->storage || t->storage->device != 0) throw std::runtime_error("CPU backend cannot execute device/symbolic memory");
    validate_layout(*t);
    if (!first) { first = t; dtype = t->dtype; }
    else if (dtype != t->dtype) {
      if (dtype == X3_TENSOR_FLOAT64 || t->dtype == X3_TENSOR_FLOAT64 || dtype == X3_TENSOR_FLOAT32 || t->dtype == X3_TENSOR_FLOAT32) dtype = X3_TENSOR_FLOAT64;
      else dtype = X3_TENSOR_INT64;
    }
  }
  if (!first) throw std::runtime_error("CPU operation requires tensor data");
  for (const auto& v : args) if (v.tag == ValueTag::Double && (dtype == X3_TENSOR_INT32 || dtype == X3_TENSOR_INT64)) dtype = X3_TENSOR_FLOAT64;
  if ((op == Kernel::Div || op == Kernel::Exp) && (dtype == X3_TENSOR_INT32 || dtype == X3_TENSOR_INT64)) dtype = X3_TENSOR_FLOAT64;
  auto shape = first->shape;
  if (op == Kernel::Reshape || op == Kernel::Permute) {
    auto view = std::make_unique<Tensor>(*first); view->id = 0;
    if (op == Kernel::Reshape) {
      shape = dimensions(attr(attrs,"shape"));
      if (!contiguous(*first) || count(shape) != count(first->shape)) throw std::runtime_error("reshape requires contiguous storage and equal element count");
      view->shape = shape; view->strides = contiguous_strides(shape,item_size(dtype));
    } else {
      auto axes = dimensions(attr(attrs,"axes"),false);
      if (axes.size() != shape.size()) throw std::runtime_error("permute requires every axis");
      std::vector<bool> used(axes.size());
      for (size_t i=0; i<axes.size(); ++i) {
        if (axes[i] < 0 || static_cast<size_t>(axes[i]) >= axes.size() || used[axes[i]]) throw std::runtime_error("invalid permutation");
        used[axes[i]]=true; view->shape[i]=first->shape[axes[i]]; view->strides[i]=first->strides[axes[i]];
      }
    }
    validate_layout(*view); return wrap_tensor(rt,std::move(view));
  }
  if (op == Kernel::Matmul) {
    auto* a = get(args[0]); auto* b = get(args[1]);
    if (!a || !b || a->shape.size()<2 || b->shape.size()<2 || a->shape.back()!=b->shape[b->shape.size()-2])
      throw std::runtime_error("matmul requires compatible matrices with rank >= 2");
    shape = broadcast({a->shape.begin(),a->shape.end()-2},{b->shape.begin(),b->shape.end()-2});
    shape.push_back(a->shape[a->shape.size()-2]); shape.push_back(b->shape.back());
  } else if (binary) shape = broadcast(get(args[0]) ? get(args[0])->shape : std::vector<int64_t>{}, get(args[1]) ? get(args[1])->shape : std::vector<int64_t>{});
  else if (op == Kernel::Sum) {
    auto axis = attr(attrs,"axis");
    if (axis.tag == ValueTag::Invalid || axis.tag == ValueTag::None) shape.clear();
    else {
      if (axis.tag != ValueTag::Int64) throw std::runtime_error("sum axis must be an integer");
      auto d = axis.as.i64; if (d < 0) d += shape.size();
      if (d < 0 || static_cast<size_t>(d) >= shape.size()) throw std::runtime_error("sum axis out of range");
      shape.erase(shape.begin()+d);
    }
  }
  auto out = allocate(rt,dtype,std::move(shape));
  switch (dtype) {
    case X3_TENSOR_FLOAT32: execute<float>(*out,op,args,attrs); break;
    case X3_TENSOR_FLOAT64: execute<double>(*out,op,args,attrs); break;
    case X3_TENSOR_INT32: execute<int32_t>(*out,op,args,attrs); break;
    case X3_TENSOR_INT64: execute<int64_t>(*out,op,args,attrs); break;
  }
  return wrap_tensor(rt,std::move(out));
}
}
