#include "xlang3/xlang3.h"
#include <atomic>
#include <memory>
#include <cstdlib>

static std::atomic<int> live_boxes{0};
class NativeBox {
public:
  NativeBox() { ++live_boxes; }
  ~NativeBox() { --live_boxes; }
  long long count = 0;
  X::Value payload;
  void set(long long number, X::Value data) { count = number; payload = std::move(data); }
  long long number() const { return count; }
  X::Value data() const { return payload; }
  BEGIN_PACKAGE(NativeBox)
    APISET().AddFunc<2>("set", &NativeBox::set);
    APISET().AddFunc<0>("number", &NativeBox::number);
    APISET().AddFunc<0>("data", &NativeBox::data);
  END_PACKAGE
};
class xlang_graph_native {
public:
  int live() const { return live_boxes.load(); }
  BEGIN_PACKAGE(xlang_graph_native)
    APISET().AddClass<0, NativeBox>("Box");
    APISET().AddFunc<0>("live", &xlang_graph_native::live);
  END_PACKAGE
};

static X3Status encode(X3Runtime*, X3Value object, void* user, X3Value* result) {
  auto* host = static_cast<X3PackageHost*>(user);
  auto* box = static_cast<NativeBox*>(host->instance_get_native_data(object, X::detail::native_type_name<NativeBox>()));
  if (!box) return X3_STATUS_ERROR;
  auto state = X::Value::Dict(host);
  if (!state.Set("count", X::Value(box->count)) || !state.Set("payload", box->payload)) return X3_STATUS_ERROR;
  *result = state.Detach();
  return X3_STATUS_OK;
}
static X3Status decode(X3Runtime*, X3Value object, X3Value input, void* user) {
  auto* host = static_cast<X3PackageHost*>(user);
  X::Value state(host, input, true);
  if (!state.IsDict()) return X3_STATUS_ERROR;
  auto box = std::make_unique<NativeBox>();
  box->__xlang3_host_ = host;
  box->count = state["count"].ToLongLong();
  box->payload = state["payload"];
  if (host->instance_set_native_data(object, X::detail::native_type_name<NativeBox>(), box.get(),
      [](void* p) { delete static_cast<NativeBox*>(p); }) != X3_STATUS_OK) return X3_STATUS_ERROR;
  box.release();
  if (std::getenv("XLANG3_TEST_GRAPH_DECODE_FAIL")) return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}
extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;
extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* pointer, X3Value module) {
  auto* host = static_cast<X3PackageHost*>(pointer);
  xlang_graph_native::BuildAPI();
  if (xlang_graph_native::APISET().Create(host, "xlang_graph_native", module) != X3_STATUS_OK) return X3_STATUS_ERROR;
  if (std::getenv("XLANG3_TEST_GRAPH_NO_CODEC")) return X3_STATUS_OK;
  X3NativeSerializerDef codec{};
  codec.size = sizeof(codec);
  codec.type_id = "xlang.tests.NativeBox";
  codec.native_type = X::detail::native_type_name<NativeBox>();
  codec.version = std::getenv("XLANG3_TEST_GRAPH_BAD_VERSION") ? 2 : 1;
  codec.encode = encode;
  codec.decode = decode;
  codec.user_data = host;
  return host->register_native_serializer(host->runtime, &codec);
}
