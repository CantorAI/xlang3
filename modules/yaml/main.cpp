/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "yaml_package.h"

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_ptr, X3Value curModule) {
  auto* host = static_cast<X3PackageHost*>(host_ptr);
  if (host == nullptr || host->abi_version != X3_ABI_VERSION) {
    return X3_STATUS_ERROR;
  }
  host->package_set_metadata(host, "package", "xlang_yaml");
  host->package_set_metadata(host, "version", "0.1.0");
  host->package_set_metadata(host, "abi", "10");
  return register_yaml_module(host, curModule);
}
