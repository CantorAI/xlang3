/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang3/xlang3.h"

#include <string>

namespace xlang_net {

class xlang_net_smtp {
public:
  std::string Send(std::string from, std::string to, std::string subject, std::string content);

  BEGIN_PACKAGE(xlang_net_smtp)
    APISET().AddFunc<4>("send", &xlang_net_smtp::Send);
    APISET().AddPropWithType<std::string>("cert_path", &xlang_net_smtp::cert_path_);
    APISET().AddPropWithType<std::string>("client_id", &xlang_net_smtp::client_id_);
    APISET().AddPropWithType<std::string>("client_secret", &xlang_net_smtp::client_secret_);
    APISET().AddPropWithType<std::string>("tenant_id", &xlang_net_smtp::tenant_id_);
    APISET().AddPropWithType<std::string>("smtp_scope", &xlang_net_smtp::smtp_scope_);
    APISET().AddPropWithType<std::string>("smtp_server", &xlang_net_smtp::smtp_server_);
    APISET().AddPropWithType<int>("smtp_port", &xlang_net_smtp::smtp_port_);
  END_PACKAGE

private:
  std::string GetAccessToken();

  std::string cert_path_;
  std::string client_id_;
  std::string client_secret_;
  std::string tenant_id_;
  std::string smtp_scope_;
  std::string smtp_server_;
  int smtp_port_ = 25;
};

} // namespace xlang_net
