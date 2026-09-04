/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "net_common.h"

#include <curl/curl.h>

#include <map>
#include <string>
#include <vector>

namespace xlang_net {

class Curl {
public:
  Curl();
  ~Curl();

  bool SetOpt(X::Value option, X::Value value);
  bool Perform();
  X::Value GetInfo(X::Value info) const;
  bool Reset();
  X::Value Escape(std::string value) const;
  X::Value Unescape(std::string value) const;
  X::Value Response() const;
  X::Value Error() const;

  static constexpr long long kWriteData = 10001;
  static constexpr long long kUrl = 10002;
  static constexpr long long kPort = 3;
  static constexpr long long kPost = 47;
  static constexpr long long kPostFields = 10015;
  static constexpr long long kHttpHeader = 10023;
  static constexpr long long kFollowLocation = 52;
  static constexpr long long kTimeout = 13;
  static constexpr long long kUserAgent = 10018;
  static constexpr long long kVerbose = 41;
  static constexpr long long kCustomRequest = 10036;
  static constexpr long long kProxy = 10004;
  static constexpr long long kProxyPort = 59;
  static constexpr long long kCookie = 10022;
  static constexpr long long kWriteFunction = 20011;
  static constexpr long long kInfoResponseCode = 2097154;
  static constexpr long long kInfoTotalTime = 3145731;
  static constexpr long long kInfoContentType = 1048594;
  static constexpr long long kInfoEffectiveUrl = 1048577;

  BEGIN_PACKAGE(Curl)
    APISET().AddFunc<2>("setOpt", &Curl::SetOpt);
    APISET().AddFunc<0>("perform", &Curl::Perform);
    APISET().AddFunc<1>("getInfo", &Curl::GetInfo);
    APISET().AddFunc<0>("reset", &Curl::Reset);
    APISET().AddFunc<1>("escape", &Curl::Escape);
    APISET().AddFunc<1>("unescape", &Curl::Unescape);
    APISET().AddProp("response", &Curl::Response);
    APISET().AddProp("error", &Curl::Error);
    APISET().AddConst("WRITEDATA", static_cast<long long>(Curl::kWriteData));
    APISET().AddConst("URL", static_cast<long long>(Curl::kUrl));
    APISET().AddConst("PORT", static_cast<long long>(Curl::kPort));
    APISET().AddConst("POST", static_cast<long long>(Curl::kPost));
    APISET().AddConst("POSTFIELDS", static_cast<long long>(Curl::kPostFields));
    APISET().AddConst("HTTPHEADER", static_cast<long long>(Curl::kHttpHeader));
    APISET().AddConst("FOLLOWLOCATION", static_cast<long long>(Curl::kFollowLocation));
    APISET().AddConst("TIMEOUT", static_cast<long long>(Curl::kTimeout));
    APISET().AddConst("USERAGENT", static_cast<long long>(Curl::kUserAgent));
    APISET().AddConst("VERBOSE", static_cast<long long>(Curl::kVerbose));
    APISET().AddConst("CUSTOMREQUEST", static_cast<long long>(Curl::kCustomRequest));
    APISET().AddConst("PROXY", static_cast<long long>(Curl::kProxy));
    APISET().AddConst("PROXYPORT", static_cast<long long>(Curl::kProxyPort));
    APISET().AddConst("COOKIE", static_cast<long long>(Curl::kCookie));
    APISET().AddConst("WRITEFUNCTION", static_cast<long long>(Curl::kWriteFunction));
    APISET().AddConst("CURLOPT_WRITEDATA", static_cast<long long>(Curl::kWriteData));
    APISET().AddConst("CURLOPT_URL", static_cast<long long>(Curl::kUrl));
    APISET().AddConst("CURLOPT_PORT", static_cast<long long>(Curl::kPort));
    APISET().AddConst("CURLOPT_POST", static_cast<long long>(Curl::kPost));
    APISET().AddConst("CURLOPT_POSTFIELDS", static_cast<long long>(Curl::kPostFields));
    APISET().AddConst("CURLOPT_HTTPHEADER", static_cast<long long>(Curl::kHttpHeader));
    APISET().AddConst("CURLOPT_FOLLOWLOCATION", static_cast<long long>(Curl::kFollowLocation));
    APISET().AddConst("CURLOPT_TIMEOUT", static_cast<long long>(Curl::kTimeout));
    APISET().AddConst("CURLOPT_USERAGENT", static_cast<long long>(Curl::kUserAgent));
    APISET().AddConst("CURLOPT_VERBOSE", static_cast<long long>(Curl::kVerbose));
    APISET().AddConst("CURLOPT_CUSTOMREQUEST", static_cast<long long>(Curl::kCustomRequest));
    APISET().AddConst("CURLOPT_PROXY", static_cast<long long>(Curl::kProxy));
    APISET().AddConst("CURLOPT_PROXYPORT", static_cast<long long>(Curl::kProxyPort));
    APISET().AddConst("CURLOPT_COOKIE", static_cast<long long>(Curl::kCookie));
    APISET().AddConst("CURLOPT_WRITEFUNCTION", static_cast<long long>(Curl::kWriteFunction));
    APISET().AddConst("CURLINFO_RESPONSE_CODE", static_cast<long long>(Curl::kInfoResponseCode));
    APISET().AddConst("CURLINFO_TOTAL_TIME", static_cast<long long>(Curl::kInfoTotalTime));
    APISET().AddConst("CURLINFO_CONTENT_TYPE", static_cast<long long>(Curl::kInfoContentType));
    APISET().AddConst("CURLINFO_EFFECTIVE_URL", static_cast<long long>(Curl::kInfoEffectiveUrl));
  END_PACKAGE

private:
  static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
  long OptionCode(const X::Value& option) const;
  long InfoCode(const X::Value& info) const;
  bool SetHeaders(const X::Value& value);

  CURL* curl_ = nullptr;
  curl_slist* headers_ = nullptr;
  std::vector<std::string> string_options_;
  std::string response_;
  std::string error_;
  char error_buffer_[CURL_ERROR_SIZE] = {};
};

} // namespace xlang_net
