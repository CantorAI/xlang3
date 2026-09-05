#include "xlang3/xlang3.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
  try {
    if (argc != 5) throw std::runtime_error("expected mode, payload path, source fixture, native directory");
    X::Runtime runtime;
    runtime.AddImportRoot(argv[4]);
    const std::filesystem::path path(argv[2]);
    if (std::string(argv[1]) == "write") {
      const auto source_dir = path.parent_path() / "graph_writer_source";
      std::filesystem::create_directories(source_dir);
      const auto source = source_dir / "writer_only_module.py";
      std::filesystem::copy_file(argv[3], source, std::filesystem::copy_options::overwrite_existing);
      runtime.AddImportRoot(source_dir.string());
      X::Module module(runtime, "writer_only_module");
      X::Stream stream(runtime);
      if (!module["payload"].ToBytes(stream)) throw std::runtime_error(runtime.LastError());
      std::vector<char> bytes(stream.Size());
      if (!stream.FullCopyTo(bytes.data(), bytes.size())) throw std::runtime_error(runtime.LastError());
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), bytes.size());
      output.close();
      if (!output) throw std::runtime_error("payload write failed");
      std::filesystem::remove(source);
      std::cout << "Graph producer passed\n";
    } else if (std::string(argv[1]) == "read" || std::string(argv[1]) == "reject") {
      std::ifstream input(path, std::ios::binary);
      if (!input) throw std::runtime_error("payload missing");
      std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
      X::Stream stream(runtime, bytes.data(), bytes.size());
      X::Value payload;
      if (std::string(argv[1]) == "reject") {
        if (payload.FromBytes(stream)) throw std::runtime_error("incompatible native state was accepted");
        X::Module native(runtime, "xlang_graph_native");
        runtime.CollectSerializedObjects();
        if (native["live"]().ToInt64() != 0) throw std::runtime_error("failed restore leaked native data");
        std::cout << "Native restore rejected with cleanup\n";
        return 0;
      }
      auto reject = [&](const std::vector<char>& damaged) {
        X::Stream bad(runtime, damaged.data(), damaged.size());
        X::Value value;
        if (value.FromBytes(bad)) throw std::runtime_error("damaged graph was accepted");
        runtime.CollectSerializedObjects();
        X::Module native(runtime, "xlang_graph_native");
        if (native["live"]().ToInt64() != 0) throw std::runtime_error("damaged graph leaked native data");
      };
      for (size_t n = 0; n < 32; ++n) reject(std::vector<char>(bytes.begin(), bytes.begin() + n));
      reject(std::vector<char>(bytes.begin(), bytes.begin() + bytes.size() / 2));
      reject(std::vector<char>(bytes.begin(), bytes.end() - 1));
      auto damaged = bytes;
      damaged[12] ^= 1; // First embedded IR checksum.
      reject(damaged);
      damaged = bytes;
      for (size_t n = damaged.size() - 8; n < damaged.size(); ++n) damaged[n] = static_cast<char>(0xff);
      reject(damaged); // Invalid root object ID.
      if (!payload.FromBytes(stream)) throw std::runtime_error(runtime.LastError());
      X::Value result;
      if (!payload["verify"].Call({payload}, result)) throw std::runtime_error(runtime.LastError());
      if (result.ToString() != "ok") throw std::runtime_error("restored function verification failed");
      X::Module native(runtime, "xlang_graph_native");
      if (native["live"]().ToInt64() != 1) throw std::runtime_error("native instance was not restored");
      payload = X::Value();
      result = X::Value();
      if (runtime.CollectSerializedObjects() == 0) throw std::runtime_error("cyclic graph was not collected");
      if (native["live"]().ToInt64() != 0) throw std::runtime_error("native instance leaked");
      if (runtime.CollectSerializedObjects() != 0) throw std::runtime_error("graph collection did not reach a fixed point");
      std::cout << "Graph consumer passed without producer source\n";
    } else throw std::runtime_error("unknown mode");
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
