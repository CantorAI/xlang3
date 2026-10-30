# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0
import sys

endpoint = "lrpc:" + sys.argv[1]
import death_test as server thru endpoint

if len(sys.argv) > 2 and sys.argv[2] == "--shutdown":
    assert server.shutdown() == "done"
elif len(sys.argv) > 2:
    server.hold(sys.argv[2])
else:
    assert server.ping() == "alive"
