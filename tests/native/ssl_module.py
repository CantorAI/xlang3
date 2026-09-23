# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import os
import select
import socket
import sys

sys.path.insert(0, sys.argv[1])
sys.path.insert(0, sys.argv[2])

import _ssl
import ssl

print(_ssl.OPENSSL_VERSION.startswith("OpenSSL 3."))
print(_ssl.OPENSSL_VERSION == ssl.OPENSSL_VERSION)

incoming = ssl.MemoryBIO()
print(incoming.pending, incoming.eof, incoming.read())
print(incoming.write(b"abc"))
print(incoming.pending, incoming.read(2), incoming.pending, incoming.eof)
incoming.write_eof()
print(incoming.read(), incoming.eof)

server_auth = _ssl.txt2obj("1.3.6.1.5.5.7.3.1", name=False)
print(server_auth[0], server_auth[1], server_auth[3])
print(len(_ssl.RAND_bytes(16)), _ssl.RAND_status())

context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
print(context.protocol == ssl.PROTOCOL_TLS_CLIENT)
print(context.verify_mode == ssl.CERT_REQUIRED, context.check_hostname)
context.check_hostname = False
context.verify_mode = ssl.CERT_NONE
print(context.verify_mode == ssl.CERT_NONE, context.check_hostname)
print(isinstance(context.options, ssl.Options))
context_ciphers = context.get_ciphers()
print(bool(context_ciphers), bool(context_ciphers[0]["name"]), context_ciphers[0]["strength_bits"] > 0)
print(context.cert_store_stats() == {"x509": 0, "crl": 0, "x509_ca": 0})
print(sorted(context.session_stats()) == [
    "accept", "accept_good", "accept_renegotiate", "cache_full", "connect",
    "connect_good", "connect_renegotiate", "hits", "misses", "number", "timeouts",
])

default_context = ssl.create_default_context()
default_context.set_alpn_protocols(["h2", "http/1.1"])
print(
    default_context.verify_mode == ssl.CERT_REQUIRED,
    default_context.check_hostname,
    bool(default_context.verify_flags & ssl.VERIFY_X509_STRICT),
)

if sys.platform == "win32":
    certificates = _ssl.enum_certificates("ROOT")
    print(bool(certificates), len(certificates[0]), certificates[0][1])

client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
client_context.maximum_version = ssl.TLSVersion.TLSv1_2
server_context.maximum_version = ssl.TLSVersion.TLSv1_2
fixture_dir = os.path.join(os.path.dirname(__file__), "ssl")
certificate_path = os.path.join(fixture_dir, "localhost-cert.pem")
decoded = _ssl._test_decode_cert(certificate_path)
print(decoded["subject"][0][0], decoded["issuer"][0][0], decoded["version"], "notBefore" in decoded)
client_context.load_verify_locations(certificate_path)
ca_certificates = client_context.get_ca_certs()
binary_ca_certificates = client_context.get_ca_certs(True)
print(
    len(ca_certificates) == 1,
    bool(ca_certificates[0]["subject"]),
    len(binary_ca_certificates) == 1,
    isinstance(binary_ca_certificates[0], bytes),
)
client_context.set_ecdh_curve("prime256v1")
print(True)
server_context.load_cert_chain(
    certificate_path,
    os.path.join(fixture_dir, "localhost-key.pem"),
)
alternate_server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
alternate_server_context.maximum_version = ssl.TLSVersion.TLSv1_2
alternate_server_context.load_cert_chain(
    certificate_path,
    os.path.join(fixture_dir, "localhost-key.pem"),
)
sni_events = []


def servername_callback(ssl_object, servername, initial_context):
    sni_events.append(
        (
            type(ssl_object).__name__,
            servername,
            initial_context is server_context,
            ssl_object.context is server_context,
        )
    )
    ssl_object.context = alternate_server_context


server_context.set_servername_callback(servername_callback)
print(callable(server_context.sni_callback))
message_events = []


def message_callback(connection, direction, version, content_type, message_type, data):
    message_events.append(
        (direction, int(content_type), isinstance(data, bytes), type(connection).__name__)
    )


client_context._msg_callback = message_callback
print(client_context._msg_callback is message_callback)
keylog_path = os.path.join(fixture_dir, "tls-keylog.tmp")
if os.path.exists(keylog_path):
    os.remove(keylog_path)


class KeylogPath:
    def __fspath__(self):
        return keylog_path


keylog_value = KeylogPath()
client_context.keylog_filename = keylog_value
print(client_context.keylog_filename is keylog_value)

client_in = ssl.MemoryBIO()
client_out = ssl.MemoryBIO()
server_in = ssl.MemoryBIO()
server_out = ssl.MemoryBIO()
client = client_context.wrap_bio(client_in, client_out, server_hostname="localhost")
server = server_context.wrap_bio(server_in, server_out, server_side=True)
client_done = False
server_done = False
for _ in range(20):
    if not client_done:
        try:
            client.do_handshake()
            client_done = True
        except ssl.SSLWantReadError:
            pass
    data = client_out.read()
    if data:
        server_in.write(data)
    if not server_done:
        try:
            server.do_handshake()
            server_done = True
        except ssl.SSLWantReadError:
            pass
    data = server_out.read()
    if data:
        client_in.write(data)
    if client_done and server_done:
        break

print(client_done, server_done, client.version(), server.version())
client_binding = client.get_channel_binding()
server_binding = server.get_channel_binding()
print(
    isinstance(client_binding, bytes),
    len(client_binding) > 0,
    client_binding == server_binding,
    client._sslobj.owner is client,
)
server_shared_ciphers = server.shared_ciphers()
print(
    bool(server_shared_ciphers),
    all(len(cipher) == 3 for cipher in server_shared_ciphers),
    client.shared_ciphers(),
)
verified_chain = client.get_verified_chain()
unverified_chain = client.get_unverified_chain()
print(
    len(verified_chain) == 1,
    isinstance(verified_chain[0], bytes),
    verified_chain == unverified_chain,
)
native_chain = client._sslobj.get_verified_chain()
print(
    type(native_chain[0]).__name__,
    native_chain[0].public_bytes(_ssl.ENCODING_PEM).startswith(
        "-----BEGIN CERTIFICATE-----"
    ),
    bool(native_chain[0].get_info()["subject"]),
)
print(
    bool(message_events),
    sorted(set(item[0] for item in message_events)),
    all(item[2] for item in message_events),
    set(item[3] for item in message_events),
    any(item[1] == 22 for item in message_events),
    any(item[1] == 256 for item in message_events),
)
client_context._msg_callback = None
print(client_context._msg_callback)
client_context.keylog_filename = None
print(client_context.keylog_filename)
with open(keylog_path, encoding="ascii") as keylog_file:
    keylog_lines = keylog_file.read().splitlines()
print(
    keylog_lines[0] == "# TLS secrets log file, generated by OpenSSL / Python",
    len(keylog_lines) > 1,
    any(line.startswith("CLIENT_RANDOM ") for line in keylog_lines[1:]),
)
os.remove(keylog_path)
peer = client.getpeercert()
print(peer["subject"][0][0], peer["issuer"][0][0], peer["version"], len(client.getpeercert(True)) > 0)
print(client.write(b"hello"))
server_in.write(client_out.read())
read_buffer = bytearray(b"........")
print(server.read(5, read_buffer), read_buffer)
print(sni_events, server.context is alternate_server_context)

session = client.session
print(
    isinstance(session, ssl.SSLSession),
    isinstance(session.id, bytes) and len(session.id) > 0,
    session.time > 0,
    session.timeout > 0,
    isinstance(session.has_ticket, bool),
    session.ticket_lifetime_hint >= 0,
)

resume_client_in = ssl.MemoryBIO()
resume_client_out = ssl.MemoryBIO()
resume_server_in = ssl.MemoryBIO()
resume_server_out = ssl.MemoryBIO()
resume_client = client_context.wrap_bio(
    resume_client_in,
    resume_client_out,
    server_hostname="localhost",
    session=session,
)
resume_server = server_context.wrap_bio(
    resume_server_in,
    resume_server_out,
    server_side=True,
)
resume_client_done = False
resume_server_done = False
for _ in range(20):
    if not resume_client_done:
        try:
            resume_client.do_handshake()
            resume_client_done = True
        except ssl.SSLWantReadError:
            pass
    data = resume_client_out.read()
    if data:
        resume_server_in.write(data)
    if not resume_server_done:
        try:
            resume_server.do_handshake()
            resume_server_done = True
        except ssl.SSLWantReadError:
            pass
    data = resume_server_out.read()
    if data:
        resume_client_in.write(data)
    if resume_client_done and resume_server_done:
        break
print(resume_client_done, resume_server_done, resume_client.session_reused)
server_context.sni_callback = None
print(server_context.sni_callback)

psk_key = b"production-psk-key"
psk_events = []
psk_client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
psk_client_context.check_hostname = False
psk_client_context.verify_mode = ssl.CERT_NONE
psk_client_context.maximum_version = ssl.TLSVersion.TLSv1_2
psk_client_context.set_ciphers("PSK-AES128-CBC-SHA256")
psk_server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
psk_server_context.maximum_version = ssl.TLSVersion.TLSv1_2
psk_server_context.set_ciphers("PSK-AES128-CBC-SHA256")


def psk_client_callback(hint):
    psk_events.append(("client", hint))
    return "xlang-client", psk_key


def psk_server_callback(identity):
    psk_events.append(("server", identity))
    return psk_key if identity == "xlang-client" else b""


psk_client_context.set_psk_client_callback(psk_client_callback)
psk_server_context.set_psk_server_callback(psk_server_callback)
psk_client_in = ssl.MemoryBIO()
psk_client_out = ssl.MemoryBIO()
psk_server_in = ssl.MemoryBIO()
psk_server_out = ssl.MemoryBIO()
psk_client = psk_client_context.wrap_bio(psk_client_in, psk_client_out)
psk_server = psk_server_context.wrap_bio(
    psk_server_in, psk_server_out, server_side=True
)
psk_client_done = False
psk_server_done = False
for _ in range(20):
    if not psk_client_done:
        try:
            psk_client.do_handshake()
            psk_client_done = True
        except ssl.SSLWantReadError:
            pass
    data = psk_client_out.read()
    if data:
        psk_server_in.write(data)
    if not psk_server_done:
        try:
            psk_server.do_handshake()
            psk_server_done = True
        except ssl.SSLWantReadError:
            pass
    data = psk_server_out.read()
    if data:
        psk_client_in.write(data)
    if psk_client_done and psk_server_done:
        break
print(
    psk_client_done,
    psk_server_done,
    psk_client.version(),
    psk_server.version(),
    psk_events,
)
print(psk_client.write(b"psk"))
psk_server_in.write(psk_client_out.read())
print(psk_server.read(3))
psk_client_context.set_psk_client_callback(None)
psk_server_context.set_psk_server_callback(None)
print(True)

socket_client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
socket_client_context.check_hostname = False
socket_client_context.verify_mode = ssl.CERT_NONE
socket_server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
socket_server_context.load_cert_chain(
    certificate_path,
    os.path.join(fixture_dir, "localhost-key.pem"),
)
raw_client, raw_server = socket.socketpair()
socket_client = socket_client_context.wrap_socket(
    raw_client, server_hostname="localhost", do_handshake_on_connect=False
)
socket_server = socket_server_context.wrap_socket(
    raw_server, server_side=True, do_handshake_on_connect=False
)
socket_client.setblocking(False)
socket_server.setblocking(False)
client_done = False
server_done = False
for _ in range(100):
    readers = []
    writers = []
    if not client_done:
        try:
            socket_client.do_handshake()
            client_done = True
        except ssl.SSLWantReadError:
            readers.append(socket_client)
        except ssl.SSLWantWriteError:
            writers.append(socket_client)
    if not server_done:
        try:
            socket_server.do_handshake()
            server_done = True
        except ssl.SSLWantReadError:
            readers.append(socket_server)
        except ssl.SSLWantWriteError:
            writers.append(socket_server)
    if client_done and server_done:
        break
    select.select(readers, writers, [], 1.0)
socket_peer = socket_client.getpeercert(True)
print(client_done, server_done, socket_client.version(), socket_peer is not None)
sent = socket_client.send(b"socket")
select.select([socket_server], [], [], 1.0)
print(sent, socket_server.recv(6))
socket_client.close()
socket_server.close()
