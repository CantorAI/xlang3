/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "curl_module.h"
#include "net_common.h"

#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace xlang_net {

class HttpRequest {
public:
  HttpRequest() = default;
  explicit HttpRequest(const httplib::Request* request);

  X::Value GetMethod() const;
  X::Value GetBody() const;
  X::Value GetPath() const;
  X::Value GetRemoteAddr() const;
  X::Value GetAllHeaders() const;
  X::Value GetParams() const;

  BEGIN_PACKAGE(HttpRequest)
    APISET().AddProp("params", &HttpRequest::GetParams);
    APISET().AddProp("args", &HttpRequest::GetParams);
    APISET().AddProp("all_headers", &HttpRequest::GetAllHeaders);
    APISET().AddProp("headers", &HttpRequest::GetAllHeaders);
    APISET().AddProp("body", &HttpRequest::GetBody);
    APISET().AddProp("method", &HttpRequest::GetMethod);
    APISET().AddProp("path", &HttpRequest::GetPath);
    APISET().AddProp("remote_addr", &HttpRequest::GetRemoteAddr);
  END_PACKAGE

private:
  const httplib::Request* request_ = nullptr;
};

class HttpResponse {
public:
  HttpResponse() = default;
  explicit HttpResponse(httplib::Response* response);

  bool SetContent(X::Value value, std::string content_type);
  bool AddHeader(std::string name, X::Value value);
  bool StreamFile(std::string file_path, long long start, long long end, std::string content_type);
  bool StreamFileWithCallback(std::string file_path, long long start, long long end, std::string content_type, X::Value callback);

  BEGIN_PACKAGE(HttpResponse)
    APISET().AddFunc<2>("set_content", &HttpResponse::SetContent);
    APISET().AddFunc<2>("add_header", &HttpResponse::AddHeader);
    APISET().AddFunc<4>("stream_file", &HttpResponse::StreamFile);
    APISET().AddFunc<5>("stream_file_with_cb", &HttpResponse::StreamFileWithCallback);
  END_PACKAGE

private:
  httplib::Response* response_ = nullptr;
};

class HttpClient {
public:
  explicit HttpClient(std::string url);

  bool Get(std::string path);
  bool Post(std::string path, std::string content_type, std::string body);
  bool PostWithCallback(std::string path, std::string content_type, std::string body, X::Value callback);
  void SetHeaders(X::Value headers);
  X::Value Headers() const;
  bool EnableServerCertificateVerification() const;
  long long Status() const;
  X::Value ResponseHeaders() const;
  X::Value Body() const;

  BEGIN_PACKAGE(HttpClient)
    APISET().AddFunc<1>("get", &HttpClient::Get);
    APISET().AddFunc<3>("post", &HttpClient::Post);
    APISET().AddFunc<4>("post_with_callback", &HttpClient::PostWithCallback);
    APISET().AddFunc<1>("setHeaders", &HttpClient::SetHeaders);
    APISET().AddProp("headers", &HttpClient::Headers);
    APISET().AddProp("enable_server_certificate_verification", &HttpClient::EnableServerCertificateVerification);
    APISET().AddProp("status", &HttpClient::Status);
    APISET().AddProp("response_headers", &HttpClient::ResponseHeaders);
    APISET().AddProp("body", &HttpClient::Body);
  END_PACKAGE

private:
  X::Value headers();
  bool CaptureResult(const httplib::Result& result);

  std::string base_url_;
  std::unique_ptr<httplib::Client> client_;
  X::Value headers_;
  X::Value response_headers_;
  X::Value body_;
  long long status_ = 0;
  bool verify_server_cert_ = true;
};

struct RouteDef {
  std::string rule;
  std::regex regex;
  X::Value handler;
};

class HttpServer {
public:
  HttpServer();
  ~HttpServer();

  bool Listen(std::string address, int port, int backlog, int thread_pool_count);
  bool Stop();
  bool Get(std::string pattern, X::Value handler);
  bool AddRoute(std::string pattern, X::Value handler);
  bool Route(X::Value handler);
  X::Value GetRoutes() const;
  bool SetAuthenticationCallback(X::Value callback, X::Value parameters);
  X::Value GetMimeType(std::string extension) const;
  bool SupportStaticFiles() const;
  X::Value StaticIndexFile() const;
  X::Value StaticRoots() const;

  BEGIN_PACKAGE(HttpServer)
    APISET().AddEvent("OnConnect");
    APISET().AddProp("SupportStaticFiles", &HttpServer::SupportStaticFiles);
    APISET().AddProp("StaticIndexFile", &HttpServer::StaticIndexFile);
    APISET().AddProp("StaticRoots", &HttpServer::StaticRoots);
    APISET().AddFunc<4>("listen", &HttpServer::Listen);
    APISET().AddFunc<0>("stop", &HttpServer::Stop);
    APISET().AddFunc<2>("get", &HttpServer::Get);
    APISET().AddFunc<1>("route", &HttpServer::Route);
    APISET().AddFunc<2>("add_route", &HttpServer::AddRoute);
    APISET().AddFunc<0>("get_routes", &HttpServer::GetRoutes);
    APISET().AddFunc<1>("getMimeType", &HttpServer::GetMimeType);
    APISET().AddFunc<2>("set_authentication_callback", &HttpServer::SetAuthenticationCallback);
  END_PACKAGE

private:
  void Init();
  bool CallHandler(X::Value handler, const httplib::Request& req, httplib::Response& res, std::vector<X::Value> args);

  std::unique_ptr<httplib::Server> server_;
  std::vector<RouteDef> routes_;
  X::Value auth_callback_;
  X::Value auth_parameters_;
  std::vector<std::string> static_roots_;
  std::string static_index_file_ = "index.html";
  bool support_static_files_ = true;
};

class xlang_net_http {
public:
  X::Value WritePad(X::Value input);
  void OnPackageCreated(X::Package<xlang_net_http>* package);

  BEGIN_PACKAGE(xlang_net_http)
    APISET().AddFunc<1>("WritePad", &xlang_net_http::WritePad);
    APISET().AddClass<0, HttpServer>("Server");
    APISET().AddClass<0, HttpResponse>("Response");
    APISET().AddClass<0, HttpRequest>("Request");
    APISET().AddClass<1, HttpClient>("Client");
    APISET().AddClass<0, Curl>("Curl");
  END_PACKAGE
};

} // namespace xlang_net
