# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0
import sys
import time
stopping = False


class Server:
    def hold(self, marker):
        with open(marker, "w") as stream:
            stream.write("entered")
        time.sleep(0.05)
        return "late"

    def ping(self):
        return "alive"

    def shutdown(self):
        global stopping
        stopping = True
        return "done"


register_remote_object("death_test", Server())
lrpc_listen(int(sys.argv[1]), False)
with open(sys.argv[2], "w") as stream:
    stream.write("ready")
while not stopping:
    time.sleep(1)
