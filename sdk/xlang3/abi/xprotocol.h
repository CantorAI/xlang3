/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#ifndef XLANG3_ABI_PROTOCOL_H
#define XLANG3_ABI_PROTOCOL_H
#include "xlang3/abi/xapi.h"
#ifdef __cplusplus
extern "C" {
#endif
X3_API X3Status x3_call_builtin(X3Runtime*, const char* name, const X3Value*, uint32_t, X3Value*);
X3_API X3Status x3_get_iter(X3Runtime*, X3Value, X3Value*);
/* Exhaustion is successful, sets done=1 and result=None. */
X3_API X3Status x3_iter_next(X3Runtime*, X3Value, X3Value*, int32_t* done);
X3_API X3Status x3_set_item(X3Runtime*, X3Value, X3Value key, X3Value value);
X3_API X3Status x3_delete_item(X3Runtime*, X3Value, X3Value key);
X3_API X3Status x3_delete_attr(X3Runtime*, X3Value, const char* name);
/* Consumes the pending exception. Returned thread-local name is borrowed. */
X3_API const char* x3_runtime_take_exception_type(X3Runtime*);
#ifdef __cplusplus
}
#endif
#endif
