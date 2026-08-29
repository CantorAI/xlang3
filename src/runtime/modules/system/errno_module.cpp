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
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"

#include <cerrno>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

namespace xlang3 {

namespace {

struct ErrnoSymbol {
  const char* name;
  int value;
};

void add_errno_symbol(
    NativeModuleBuilder& builder,
    std::vector<std::pair<Value, Value>>& errorcode,
    const char* name,
    int value) {
  builder.value(name, Value::int64(value));
  errorcode.push_back({Value::int64(value), Value::string(name)});
}

} // namespace

void register_errno_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "errno");
  builder.value("__doc__", Value::string("This module makes available standard errno system symbols."));
  std::vector<std::pair<Value, Value>> errorcode;
  errorcode.reserve(128);

#define XLANG3_ADD_ERRNO(name) \
  do {                         \
    add_errno_symbol(builder, errorcode, #name, name); \
  } while (false)

#ifdef EPERM
  XLANG3_ADD_ERRNO(EPERM);
#endif
#ifdef ENOENT
  XLANG3_ADD_ERRNO(ENOENT);
#endif
#ifdef ESRCH
  XLANG3_ADD_ERRNO(ESRCH);
#endif
#ifdef EINTR
  XLANG3_ADD_ERRNO(EINTR);
#endif
#ifdef EIO
  XLANG3_ADD_ERRNO(EIO);
#endif
#ifdef ENXIO
  XLANG3_ADD_ERRNO(ENXIO);
#endif
#ifdef E2BIG
  XLANG3_ADD_ERRNO(E2BIG);
#endif
#ifdef ENOEXEC
  XLANG3_ADD_ERRNO(ENOEXEC);
#endif
#ifdef EBADF
  XLANG3_ADD_ERRNO(EBADF);
#endif
#ifdef ECHILD
  XLANG3_ADD_ERRNO(ECHILD);
#endif
#ifdef EAGAIN
  XLANG3_ADD_ERRNO(EAGAIN);
#endif
#ifdef ENOMEM
  XLANG3_ADD_ERRNO(ENOMEM);
#endif
#ifdef EACCES
  XLANG3_ADD_ERRNO(EACCES);
#endif
#ifdef EFAULT
  XLANG3_ADD_ERRNO(EFAULT);
#endif
#ifdef EBUSY
  XLANG3_ADD_ERRNO(EBUSY);
#endif
#ifdef EEXIST
  XLANG3_ADD_ERRNO(EEXIST);
#endif
#ifdef EXDEV
  XLANG3_ADD_ERRNO(EXDEV);
#endif
#ifdef ENODEV
  XLANG3_ADD_ERRNO(ENODEV);
#endif
#ifdef ENOTDIR
  XLANG3_ADD_ERRNO(ENOTDIR);
#endif
#ifdef EISDIR
  XLANG3_ADD_ERRNO(EISDIR);
#endif
#ifdef EINVAL
  XLANG3_ADD_ERRNO(EINVAL);
#endif
#ifdef ENFILE
  XLANG3_ADD_ERRNO(ENFILE);
#endif
#ifdef EMFILE
  XLANG3_ADD_ERRNO(EMFILE);
#endif
#ifdef ENOTTY
  XLANG3_ADD_ERRNO(ENOTTY);
#endif
#ifdef EFBIG
  XLANG3_ADD_ERRNO(EFBIG);
#endif
#ifdef ENOSPC
  XLANG3_ADD_ERRNO(ENOSPC);
#endif
#ifdef ESPIPE
  XLANG3_ADD_ERRNO(ESPIPE);
#endif
#ifdef EROFS
  XLANG3_ADD_ERRNO(EROFS);
#endif
#ifdef EMLINK
  XLANG3_ADD_ERRNO(EMLINK);
#endif
#ifdef EPIPE
  XLANG3_ADD_ERRNO(EPIPE);
#endif
#ifdef EDOM
  XLANG3_ADD_ERRNO(EDOM);
#endif
#ifdef ERANGE
  XLANG3_ADD_ERRNO(ERANGE);
#endif
#ifdef EDEADLK
  XLANG3_ADD_ERRNO(EDEADLK);
#endif
#ifdef EDEADLOCK
  XLANG3_ADD_ERRNO(EDEADLOCK);
#endif
#ifdef ENAMETOOLONG
  XLANG3_ADD_ERRNO(ENAMETOOLONG);
#endif
#ifdef ENOLCK
  XLANG3_ADD_ERRNO(ENOLCK);
#endif
#ifdef ENOSYS
  XLANG3_ADD_ERRNO(ENOSYS);
#endif
#ifdef ENOTEMPTY
  XLANG3_ADD_ERRNO(ENOTEMPTY);
#endif
#ifdef EILSEQ
  XLANG3_ADD_ERRNO(EILSEQ);
#endif
#ifdef EBADMSG
  XLANG3_ADD_ERRNO(EBADMSG);
#endif
#ifdef ECANCELED
  XLANG3_ADD_ERRNO(ECANCELED);
#endif
#ifdef EIDRM
  XLANG3_ADD_ERRNO(EIDRM);
#endif
#ifdef ENODATA
  XLANG3_ADD_ERRNO(ENODATA);
#endif
#ifdef ENOLINK
  XLANG3_ADD_ERRNO(ENOLINK);
#endif
#ifdef ENOMSG
  XLANG3_ADD_ERRNO(ENOMSG);
#endif
#ifdef ENOSR
  XLANG3_ADD_ERRNO(ENOSR);
#endif
#ifdef ENOSTR
  XLANG3_ADD_ERRNO(ENOSTR);
#endif
#ifdef ENOTRECOVERABLE
  XLANG3_ADD_ERRNO(ENOTRECOVERABLE);
#endif
#ifdef ENOTSUP
  XLANG3_ADD_ERRNO(ENOTSUP);
#endif
#ifdef EOVERFLOW
  XLANG3_ADD_ERRNO(EOVERFLOW);
#endif
#ifdef EOWNERDEAD
  XLANG3_ADD_ERRNO(EOWNERDEAD);
#endif
#ifdef EPROTO
  XLANG3_ADD_ERRNO(EPROTO);
#endif
#ifdef ETIME
  XLANG3_ADD_ERRNO(ETIME);
#endif
#ifdef ETXTBSY
  XLANG3_ADD_ERRNO(ETXTBSY);
#endif

#if defined(_WIN32)
  XLANG3_ADD_ERRNO(WSABASEERR);
  XLANG3_ADD_ERRNO(WSAEINTR);
  XLANG3_ADD_ERRNO(WSAEBADF);
  XLANG3_ADD_ERRNO(WSAEACCES);
  XLANG3_ADD_ERRNO(WSAEFAULT);
  XLANG3_ADD_ERRNO(WSAEINVAL);
  XLANG3_ADD_ERRNO(WSAEMFILE);
  XLANG3_ADD_ERRNO(WSAEWOULDBLOCK);
  XLANG3_ADD_ERRNO(WSAEINPROGRESS);
  XLANG3_ADD_ERRNO(WSAEALREADY);
  XLANG3_ADD_ERRNO(WSAENOTSOCK);
  XLANG3_ADD_ERRNO(WSAEDESTADDRREQ);
  XLANG3_ADD_ERRNO(WSAEMSGSIZE);
  XLANG3_ADD_ERRNO(WSAEPROTOTYPE);
  XLANG3_ADD_ERRNO(WSAENOPROTOOPT);
  XLANG3_ADD_ERRNO(WSAEPROTONOSUPPORT);
  XLANG3_ADD_ERRNO(WSAESOCKTNOSUPPORT);
  XLANG3_ADD_ERRNO(WSAEOPNOTSUPP);
  XLANG3_ADD_ERRNO(WSAEPFNOSUPPORT);
  XLANG3_ADD_ERRNO(WSAEAFNOSUPPORT);
  XLANG3_ADD_ERRNO(WSAEADDRINUSE);
  XLANG3_ADD_ERRNO(WSAEADDRNOTAVAIL);
  XLANG3_ADD_ERRNO(WSAENETDOWN);
  XLANG3_ADD_ERRNO(WSAENETUNREACH);
  XLANG3_ADD_ERRNO(WSAENETRESET);
  XLANG3_ADD_ERRNO(WSAECONNABORTED);
  XLANG3_ADD_ERRNO(WSAECONNRESET);
  XLANG3_ADD_ERRNO(WSAENOBUFS);
  XLANG3_ADD_ERRNO(WSAEISCONN);
  XLANG3_ADD_ERRNO(WSAENOTCONN);
  XLANG3_ADD_ERRNO(WSAESHUTDOWN);
  XLANG3_ADD_ERRNO(WSAETOOMANYREFS);
  XLANG3_ADD_ERRNO(WSAETIMEDOUT);
  XLANG3_ADD_ERRNO(WSAECONNREFUSED);
  XLANG3_ADD_ERRNO(WSAELOOP);
  XLANG3_ADD_ERRNO(WSAENAMETOOLONG);
  XLANG3_ADD_ERRNO(WSAEHOSTDOWN);
  XLANG3_ADD_ERRNO(WSAEHOSTUNREACH);
  XLANG3_ADD_ERRNO(WSAENOTEMPTY);
  XLANG3_ADD_ERRNO(WSAEPROCLIM);
  XLANG3_ADD_ERRNO(WSAEUSERS);
  XLANG3_ADD_ERRNO(WSAEDQUOT);
  XLANG3_ADD_ERRNO(WSAESTALE);
  XLANG3_ADD_ERRNO(WSAEREMOTE);
  XLANG3_ADD_ERRNO(WSASYSNOTREADY);
  XLANG3_ADD_ERRNO(WSAVERNOTSUPPORTED);
  XLANG3_ADD_ERRNO(WSANOTINITIALISED);
  XLANG3_ADD_ERRNO(WSAEDISCON);

  builder.value("EWOULDBLOCK", Value::int64(WSAEWOULDBLOCK))
      .value("EINPROGRESS", Value::int64(WSAEINPROGRESS))
      .value("EALREADY", Value::int64(WSAEALREADY))
      .value("ENOTSOCK", Value::int64(WSAENOTSOCK))
      .value("EDESTADDRREQ", Value::int64(WSAEDESTADDRREQ))
      .value("EMSGSIZE", Value::int64(WSAEMSGSIZE))
      .value("EPROTOTYPE", Value::int64(WSAEPROTOTYPE))
      .value("ENOPROTOOPT", Value::int64(WSAENOPROTOOPT))
      .value("EPROTONOSUPPORT", Value::int64(WSAEPROTONOSUPPORT))
      .value("ESOCKTNOSUPPORT", Value::int64(WSAESOCKTNOSUPPORT))
      .value("EOPNOTSUPP", Value::int64(WSAEOPNOTSUPP))
      .value("EPFNOSUPPORT", Value::int64(WSAEPFNOSUPPORT))
      .value("EAFNOSUPPORT", Value::int64(WSAEAFNOSUPPORT))
      .value("EADDRINUSE", Value::int64(WSAEADDRINUSE))
      .value("EADDRNOTAVAIL", Value::int64(WSAEADDRNOTAVAIL))
      .value("ENETDOWN", Value::int64(WSAENETDOWN))
      .value("ENETUNREACH", Value::int64(WSAENETUNREACH))
      .value("ENETRESET", Value::int64(WSAENETRESET))
      .value("ECONNABORTED", Value::int64(WSAECONNABORTED))
      .value("ECONNRESET", Value::int64(WSAECONNRESET))
      .value("ENOBUFS", Value::int64(WSAENOBUFS))
      .value("EISCONN", Value::int64(WSAEISCONN))
      .value("ENOTCONN", Value::int64(WSAENOTCONN))
      .value("ESHUTDOWN", Value::int64(WSAESHUTDOWN))
      .value("ETOOMANYREFS", Value::int64(WSAETOOMANYREFS))
      .value("ETIMEDOUT", Value::int64(WSAETIMEDOUT))
      .value("ECONNREFUSED", Value::int64(WSAECONNREFUSED))
      .value("ELOOP", Value::int64(WSAELOOP))
      .value("EHOSTDOWN", Value::int64(WSAEHOSTDOWN))
      .value("EHOSTUNREACH", Value::int64(WSAEHOSTUNREACH))
      .value("EUSERS", Value::int64(WSAEUSERS))
      .value("EDQUOT", Value::int64(WSAEDQUOT))
      .value("ESTALE", Value::int64(WSAESTALE))
      .value("EREMOTE", Value::int64(WSAEREMOTE));
#endif

#undef XLANG3_ADD_ERRNO

  builder.value("errorcode", Value::dict(std::move(errorcode)));
  runtime.register_module("errno", builder.finish());
}

} // namespace xlang3
