from xlang_net import http

server = http.Server()

def small(req, res):
    res.add_header("X-Test", "small")
    res.set_content("hello", "text/plain")

def large(req, res):
    res.set_content("x" * 65536, "text/plain")

def binary(req, res):
    res.set_content(b"\x00\x01\x02\x03", "application/octet-stream")

def shutdown(req, res):
    res.set_content("bye", "text/plain")
    server.stop()

server.get("/small", small)
server.get("/large", large)
server.get("/binary", binary)
server.get("/shutdown", shutdown)
server.listen("127.0.0.1", 18173, 128, 2)
