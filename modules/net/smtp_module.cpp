/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "smtp_module.h"

#include <curl/curl.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace xlang_net {
namespace {

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  auto* response = static_cast<std::string*>(userp);
  const size_t total = size * nmemb;
  response->append(static_cast<const char*>(contents), total);
  return total;
}

size_t read_callback(void* ptr, size_t size, size_t nmemb, void* userp) {
  auto* data = static_cast<std::string*>(userp);
  const size_t buffer_size = size * nmemb;
  if (data->empty()) {
    return 0;
  }
  const size_t send_size = data->size() < buffer_size ? data->size() : buffer_size;
  std::memcpy(ptr, data->data(), send_size);
  data->erase(0, send_size);
  return send_size;
}

struct CurlHandle {
  CURL* handle = curl_easy_init();
  ~CurlHandle() {
    if (handle != nullptr) {
      curl_easy_cleanup(handle);
    }
  }
  explicit operator bool() const { return handle != nullptr; }
};

struct SlistHandle {
  curl_slist* list = nullptr;
  ~SlistHandle() {
    if (list != nullptr) {
      curl_slist_free_all(list);
    }
  }
};

std::string parse_access_token(const std::string& response) {
  const std::string marker = "\"access_token\":\"";
  const size_t start_marker = response.find(marker);
  if (start_marker == std::string::npos) {
    return "Access token failed to parse access token.";
  }
  const size_t start = start_marker + marker.size();
  const size_t end = response.find('"', start);
  if (end == std::string::npos || end <= start) {
    return "Access token failed to parse access token.";
  }
  return response.substr(start, end - start);
}

} // namespace

std::string xlang_net_smtp::GetAccessToken() {
  CurlHandle curl;
  if (!curl) {
    return "Access token failed to initialize";
  }

  const std::string url = "https://login.microsoftonline.com/" + tenant_id_ + "/oauth2/v2.0/token";
  const std::string post_fields = "client_id=" + client_id_ + "&client_secret=" + client_secret_ +
                                  "&grant_type=client_credentials&scope=" + smtp_scope_;
  std::string response;
  if (!cert_path_.empty()) {
    curl_easy_setopt(curl.handle, CURLOPT_CAINFO, cert_path_.c_str());
  }
  curl_easy_setopt(curl.handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDS, post_fields.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &response);

  const CURLcode result = curl_easy_perform(curl.handle);
  if (result != CURLE_OK) {
    return std::string("Access token request failed: ") + curl_easy_strerror(result);
  }
  if (response.empty()) {
    return "Access token response data is empty.";
  }
  return parse_access_token(response);
}

std::string xlang_net_smtp::Send(std::string from, std::string to, std::string subject, std::string content) {
  std::string access_token = GetAccessToken();
  if (access_token.rfind("Access token", 0) == 0) {
    return access_token;
  }

  std::stringstream email_data;
  email_data << "From: " << from << "\r\n"
             << "Subject: " << subject << "\r\n"
             << "\r\n"
             << content << "\r\n";
  std::string email_content = email_data.str();
  if (email_content.empty()) {
    return "Email content is empty.";
  }

  CurlHandle curl;
  if (!curl) {
    return "Failed to initialize Smtp";
  }

  std::ostringstream auth_string;
  auth_string << "user=" << from << "\x01auth=Bearer " << access_token << "\x01\x01";
  char* escaped_auth = curl_easy_escape(curl.handle, auth_string.str().c_str(), 0);
  if (escaped_auth == nullptr) {
    return "Failed to base64 encode the auth string.";
  }
  std::string xoauth2_bearer = escaped_auth;
  curl_free(escaped_auth);

  const std::string smtp_url = "smtp://" + smtp_server_ + ":" + std::to_string(smtp_port_);
  curl_easy_setopt(curl.handle, CURLOPT_URL, smtp_url.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);
  curl_easy_setopt(curl.handle, CURLOPT_USERNAME, from.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_XOAUTH2_BEARER, xoauth2_bearer.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_MAIL_FROM, from.c_str());

  SlistHandle recipients;
  recipients.list = curl_slist_append(recipients.list, to.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_MAIL_RCPT, recipients.list);
  curl_easy_setopt(curl.handle, CURLOPT_READFUNCTION, read_callback);
  curl_easy_setopt(curl.handle, CURLOPT_READDATA, &email_content);
  curl_easy_setopt(curl.handle, CURLOPT_UPLOAD, 1L);

  const CURLcode result = curl_easy_perform(curl.handle);
  if (result != CURLE_OK) {
    return std::string("SMTP request failed: ") + curl_easy_strerror(result);
  }
  return "Email sent successfully!";
}

} // namespace xlang_net
