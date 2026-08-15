#ifndef XLANG3_XMODULE_H
#define XLANG3_XMODULE_H

#include "xlang3/xapi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct X3ModuleDef {
  uint32_t abi_version;
  const char* name;
  X3Status (*init)(X3Runtime* runtime);
} X3ModuleDef;

typedef X3ModuleDef* (*X3ModuleInitFn)(void);

#define X3_ABI_VERSION 1u
#define X3_MODULE_INIT_NAME x3_module_init

#ifdef __cplusplus
}
#endif

#endif
