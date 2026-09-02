# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import sys


endpoint = "lrpc:" + sys.argv[1]

import ipc_srv as srv thru endpoint

print(srv.name)
