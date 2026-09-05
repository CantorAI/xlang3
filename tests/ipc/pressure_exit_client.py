import sys
endpoint = "lrpc:" + sys.argv[1]
import ipc_srv as srv thru endpoint
srv.pressure_hold(b"x" * 1048576)
