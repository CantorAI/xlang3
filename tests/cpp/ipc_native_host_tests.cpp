#include "xlang3/xlang3.h"
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

struct NativeIpcService {
  BEGIN_PACKAGE(NativeIpcService)
    APISET().AddFunc<1>("echo", &NativeIpcService::Echo);
    APISET().AddFunc<2>("add", &NativeIpcService::Add);
  END_PACKAGE

  std::string Echo(std::string value) const { return "native:" + value; }
  std::int64_t Add(std::int64_t left, std::int64_t right) const { return left + right; }
};

int main() {
  try {
    X::Runtime server;
    NativeIpcService service;
    auto package = server.RegisterPackage("native_ipc", service);
    server.RegisterRemoteObject("native_ipc", package);
    if (x3_runtime_register_remote_object(server.get(), nullptr, package.raw()) != X3_STATUS_ERROR)
      throw std::runtime_error("native LRPC registration accepted an empty object name");
    if (x3_runtime_lrpc_listen(server.get(), 0) != X3_STATUS_ERROR)
      throw std::runtime_error("native LRPC listener accepted an invalid port");
    if (x3_runtime_lrpc_listen(server.get(), 65536) != X3_STATUS_ERROR)
      throw std::runtime_error("native LRPC listener accepted an out-of-range port");
    X::Runtime other_runtime;
    auto foreign = other_runtime.Dict();
    bool foreign_rejected = false;
    try { server.RegisterRemoteObject("foreign", foreign); }
    catch (const std::invalid_argument&) { foreign_rejected = true; }
    if (!foreign_rejected) throw std::runtime_error("native LRPC registration accepted a foreign runtime value");

    std::random_device entropy;
    std::uint16_t port = 0;
    std::string last_error;
    for (int attempt = 0; attempt < 24; ++attempt) {
      port = static_cast<std::uint16_t>(20000 + entropy() % 40000);
      try {
        server.LrpcListen(port);
        last_error.clear();
        break;
      } catch (const std::exception& error) {
        last_error = error.what();
      }
    }
    if (!last_error.empty()) throw std::runtime_error("cannot start native LRPC host: " + last_error);

    X::Runtime client;
    const auto endpoint = "lrpc:" + std::to_string(port);
    auto remote = client.ImportRemote("native_ipc", endpoint.c_str());
    if (remote["echo"]("round trip").ToString() != "native:round trip")
      throw std::runtime_error("native LRPC string round trip failed");
    if (remote["add"](20, 22).ToInt64() != 42)
      throw std::runtime_error("native LRPC integer round trip failed");

    X3LrpcEndpointInfo info{};
    info.size = sizeof(info);
    client.check(x3_runtime_probe_remote(client.get(), endpoint.c_str(), 1000, &info));
    if (!info.pid || !info.session_id) throw std::runtime_error("native LRPC probe omitted host identity");
    std::cout << "C++ registered native service hosted and called over xlang3 shared-memory LRPC\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
