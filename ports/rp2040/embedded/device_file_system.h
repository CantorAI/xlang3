/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once

#include "xlang3/rpc/file_store.h"
#include "xlang3/vfs.h"

namespace xlang3::pico {

class DeviceFileSystem final : public FileSystem {
public:
  DeviceFileSystem(xlang3::rpc::FileStore* ram, xlang3::rpc::FileStore* flash);

  bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& error) override;
  bool write_file(const std::string& path, const uint8_t* data, std::size_t size, std::string& error) override;
  bool remove(const std::string& path, std::string& error) override;
  bool list_dir(const std::string& path, std::vector<std::string>& out, std::string& error) override;
  bool stat(const std::string& path, VfsStat& out, std::string& error) override;

private:
  struct Route {
    xlang3::rpc::FileStore* store = nullptr;
    std::string path;
  };

  xlang3::rpc::FileStore* ram_ = nullptr;
  xlang3::rpc::FileStore* flash_ = nullptr;

  bool route(const std::string& path, Route& out, std::string& error) const;
};

} // namespace xlang3::pico
