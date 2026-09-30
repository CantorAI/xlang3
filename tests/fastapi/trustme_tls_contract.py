import json
import ssl
from pathlib import Path
from tempfile import TemporaryDirectory

import trustme
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.serialization import load_pem_private_key
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


def verified_handshake(ca, leaf, hostname="localhost", trust=True, server_context=None):
    if server_context is None:
        server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        leaf.configure_cert(server_context)
    client_context = ssl.create_default_context()
    if trust:
        ca.configure_trust(client_context)

    client_in, client_out = ssl.MemoryBIO(), ssl.MemoryBIO()
    server_in, server_out = ssl.MemoryBIO(), ssl.MemoryBIO()
    client = client_context.wrap_bio(
        client_in, client_out, server_side=False, server_hostname=hostname
    )
    server = server_context.wrap_bio(server_in, server_out, server_side=True)
    client_done = server_done = False
    for _ in range(20):
        if not client_done:
            try:
                client.do_handshake()
                client_done = True
            except ssl.SSLWantReadError:
                pass
        server_in.write(client_out.read())
        if not server_done:
            try:
                server.do_handshake()
                server_done = True
            except ssl.SSLWantReadError:
                pass
        client_in.write(server_out.read())
        if client_done and server_done:
            break
    return client_done, server_done, client.version(), client.getpeercert()["subjectAltName"]


@app.get("/trustme-tls")
def trustme_tls():
    automatic_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    automatic_context.check_hostname = False
    automatic_context.verify_mode = ssl.CERT_NONE
    automatic_context.check_hostname = True
    hostname_auto_verify = automatic_context.verify_mode == ssl.CERT_REQUIRED
    eof_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    eof_context.check_hostname = False
    eof_context.verify_mode = ssl.CERT_NONE
    eof_in, eof_out = ssl.MemoryBIO(), ssl.MemoryBIO()
    eof_client = eof_context.wrap_bio(
        eof_in, eof_out, server_hostname="localhost"
    )
    try:
        eof_client.do_handshake()
    except ssl.SSLWantReadError:
        pass
    eof_in.write_eof()
    try:
        eof_client.do_handshake()
    except ssl.SSLError as exc:
        eof_fields = [
            isinstance(exc, ssl.SSLEOFError),
            exc.errno == ssl.SSL_ERROR_EOF,
            exc.reason == "UNEXPECTED_EOF_WHILE_READING",
            isinstance(exc.strerror, str),
        ]
    else:
        eof_fields = []
    named_server_bio = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER).wrap_bio(
        ssl.MemoryBIO(), ssl.MemoryBIO(), server_side=True,
        server_hostname="localhost"
    )
    ca = trustme.CA()
    leaf = ca.issue_cert("localhost", "127.0.0.1")
    certificate = x509.load_pem_x509_certificate(leaf.cert_chain_pems[0].bytes())
    client_done, server_done, version, peer_san = verified_handshake(ca, leaf)

    rsa_root = trustme.CA(key_type=trustme.KeyType.RSA)
    intermediate = trustme.CA(parent_cert=rsa_root)
    rsa_leaf = intermediate.issue_cert("localhost", key_type=trustme.KeyType.RSA)
    rsa_client_done, rsa_server_done, rsa_version, _ = verified_handshake(
        rsa_root, rsa_leaf
    )
    try:
        verified_handshake(ca, leaf, hostname="wrong.test")
    except ssl.SSLCertVerificationError as exc:
        hostname_rejected = True
        hostname_error_fields = [
            exc.errno == ssl.SSL_ERROR_SSL,
            isinstance(exc.strerror, str),
            isinstance(exc.verify_code, int) and exc.verify_code > 0,
            isinstance(exc.verify_message, str),
        ]
    else:
        hostname_rejected = False
        hostname_error_fields = []
    try:
        verified_handshake(ca, leaf, trust=False)
    except ssl.SSLCertVerificationError as exc:
        untrusted_rejected = True
        untrusted_error_fields = [
            exc.errno == ssl.SSL_ERROR_SSL,
            isinstance(exc.strerror, str),
            isinstance(exc.verify_code, int) and exc.verify_code > 0,
            isinstance(exc.verify_message, str),
        ]
    else:
        untrusted_rejected = False
        untrusted_error_fields = []

    return {
        "hostname_auto_verify": hostname_auto_verify,
        "bio_eof_fields": eof_fields,
        "handshake": [client_done, server_done],
        "server_bio_hostname": named_server_bio.server_hostname,
        "tls_version": version,
        "peer_san": peer_san,
        "certificate_san": [
            type(name).__name__
            for name in certificate.extensions.get_extension_for_class(
                x509.SubjectAlternativeName
            ).value
        ],
        "extension_types": [type(ext.value).__name__ for ext in certificate.extensions],
        "issuer_matches_ca": certificate.issuer == ca._certificate.subject,
        "rsa_chain_handshake": [rsa_client_done, rsa_server_done],
        "rsa_chain_length": len(rsa_leaf.cert_chain_pems),
        "rsa_chain_tls_version": rsa_version,
        "hostname_rejected": hostname_rejected,
        "hostname_error_fields": hostname_error_fields,
        "untrusted_rejected": untrusted_rejected,
        "untrusted_error_fields": untrusted_error_fields,
    }


response = TestClient(app).get("/trustme-tls")
print(response.status_code, json.dumps(response.json(), sort_keys=True))


@app.get("/trustme-encrypted-key")
def trustme_encrypted_key():
    ca = trustme.CA()
    leaf = ca.issue_cert("localhost")
    key = load_pem_private_key(leaf.private_key_pem.bytes(), None)
    encrypted_key = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.BestAvailableEncryption(b"password"),
    )
    with TemporaryDirectory() as directory:
        cert_path = Path(directory) / "cert.pem"
        key_path = Path(directory) / "key.pem"
        cert_path.write_bytes(leaf.cert_chain_pems[0].bytes())
        key_path.write_bytes(encrypted_key)
        calls = []

        def get_password():
            calls.append(True)
            return b"password"

        server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        server_context.load_cert_chain(cert_path, key_path, get_password)
        client_done, server_done, version, _ = verified_handshake(
            ca, leaf, server_context=server_context
        )
        plain_key_path = Path(directory) / "plain-key.pem"
        plain_key_path.write_bytes(leaf.private_key_pem.bytes())
        unused_password_calls = []

        def unused_password():
            unused_password_calls.append(True)
            return b"unused"

        plain_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        plain_context.load_cert_chain(cert_path, plain_key_path, unused_password)
        callback_error_propagated = False

        def failing_password():
            raise RuntimeError("password callback failed")

        try:
            ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER).load_cert_chain(
                cert_path, key_path, failing_password
            )
        except RuntimeError as exc:
            callback_error_propagated = str(exc) == "password callback failed"
    return {
        "password_calls": len(calls),
        "unused_password_calls": len(unused_password_calls),
        "callback_error_propagated": callback_error_propagated,
        "handshake": [client_done, server_done],
        "tls_version": version,
    }


response = TestClient(app).get("/trustme-encrypted-key")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
