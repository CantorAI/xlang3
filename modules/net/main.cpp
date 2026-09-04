/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "cypher_module.h"
#include "http_module.h"
#include "smtp_module.h"

#if defined(XLANG_NET_WITH_CURL)
#include <curl/curl.h>
#endif

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_ptr, X3Value curModule) {
  auto* host = static_cast<X3PackageHost*>(host_ptr);
  if (host == nullptr) {
    return X3_STATUS_ERROR;
  }

#if defined(XLANG_NET_WITH_CURL)
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    return X3_STATUS_ERROR;
  }
  if (host->package_set_cleanup == nullptr ||
      host->package_set_cleanup(host, nullptr, [](void*) { curl_global_cleanup(); }) != X3_STATUS_OK) {
    curl_global_cleanup();
    return X3_STATUS_ERROR;
  }
#endif

  xlang_net::xlang_net_http::BuildAPI();
  if (xlang_net::xlang_net_http::APISET().Create(host, "xlang_net.http", curModule) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  xlang_net::xlang_net_cypher::BuildAPI();
  if (xlang_net::xlang_net_cypher::APISET().Create(host, "xlang_net.cypher", curModule) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  xlang_net::xlang_net_smtp::BuildAPI();
  if (xlang_net::xlang_net_smtp::APISET().Create(host, "xlang_net.smtp", curModule) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }

  return X3_STATUS_OK;
}
