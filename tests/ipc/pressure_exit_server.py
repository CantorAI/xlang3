import sys
import time

prefix = sys.argv[2]

class Server:
    def pressure_hold(self, payload):
        with open(prefix + ".entered", "w") as marker:
            marker.write("entered")
        time.sleep(60)
        return payload

register_remote_object("ipc_srv", Server())
lrpc_listen(int(sys.argv[1]), False)
with open(prefix + ".ready", "w") as marker:
    marker.write("ready")
while True:
    time.sleep(1)
