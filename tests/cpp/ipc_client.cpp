#include "xlang3/xlang3.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) return 2;
    X::Runtime runtime;
    auto endpoint = std::string("lrpc:") + argv[1];
    auto server = runtime.ImportRemote("ipc_srv", endpoint.c_str());
    require(server["name"].ToString() == "ipc-smoke", "remote attribute failed");
    require(server["echo"]("native").ToString() == "echo:native", "remote string call failed");
    require(server["add"](20, 22).ToInt64() == 42, "remote integer call failed");
    auto box = server["Box"](73);
    require(box["value"]().ToInt64() == 73, "remote returned object failed");
    require(server["get_len"]()("abcd").ToInt64() == 4, "remote returned callable failed");
    for (size_t size : {size_t(0), size_t(31), size_t(65536), size_t(1048577), size_t(2000000)}) {
      std::vector<char> payload(size);
      for (size_t i = 0; i < size; ++i) payload[i] = static_cast<char>(i);
      auto bytes = X::Value::Bytes(runtime.host(), payload.data(), payload.size());
      auto echoed = server["bytes_echo"](bytes);
      if (!echoed.IsValid()) throw std::runtime_error("bytes_echo: " + runtime.LastError());
      uint64_t length = 0;
      auto* data = echoed.BytesData(&length);
      if (!echoed.IsBin() || length != size || (size && (!data || std::memcmp(data, payload.data(), size) != 0))) {
        std::cerr << "binary round trip: sent=" << size << " received=" << length
                  << " tag=" << echoed.raw().tag << " bytes=" << echoed.IsBin() << '\n';
      }
      require(echoed.IsBin() && length == size && (!size || std::memcmp(data, payload.data(), size) == 0),
          "remote binary payload changed");
    }
    auto oversized = X::Value::Bytes(runtime.host(), std::vector<char>(2097152).data(), 2097152);
    for (int attempt = 0; attempt < 40; ++attempt) {
      require(!server["bytes_echo"](oversized).IsValid(), "oversized request accepted");
      require(server["add"](20, 22).ToInt64() == 42, "failed request leaked slots");
      require(!server["make_bytes"](2097152).IsValid(), "oversized response accepted");
      auto small = server["make_bytes"](16);
      uint64_t size = 0;
      const void* data = small.BytesData(&size);
      require(size == 16 && data && std::memcmp(data, "xxxxxxxxxxxxxxxx", 16) == 0,
          "failed response leaked slots");
    }
    auto graph = runtime.Dict();
    auto items = runtime.List();
    items.Append(X::Value(42));
    graph.Set("items", items);
    auto echoed = server["size_echo"](graph);
    require(echoed["items"][0].ToInt64() == 42, "remote nested value failed");
    bool rejected = false;
    try { runtime.ImportRemote("ipc_srv", "invalid:1"); }
    catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid endpoint accepted");
    std::cout << "C++ shared-memory IPC passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
