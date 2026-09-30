import ssl

import trustme
from cryptography import x509
from cryptography.hazmat.primitives.serialization import Encoding
from fastapi import FastAPI
from fastapi.testclient import TestClient
from truststore._windows import _verify_peercerts_impl


app = FastAPI()


@app.get('/windows-chain-policy')
def windows_chain_policy() -> dict:
    ca = trustme.CA()
    leaf = ca.issue_cert('localhost')
    certificate = x509.load_pem_x509_certificate(leaf.cert_chain_pems[0].bytes())
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ca.configure_trust(context)
    der = certificate.public_bytes(Encoding.DER)
    ca_binary = context.get_ca_certs(binary_form=True)
    _verify_peercerts_impl(context, [der], server_hostname='localhost')
    try:
        _verify_peercerts_impl(context, [der], server_hostname='wrong.test')
    except ssl.SSLCertVerificationError as exc:
        mismatch = isinstance(exc.verify_code, int) and bool(exc.verify_message)
    else:
        mismatch = False
    return {'trusted_ca': bool(ca_binary), 'matching_host': True,
            'mismatched_host_rejected': mismatch}


with TestClient(app) as client:
    response = client.get('/windows-chain-policy')
    print(response.status_code, response.json())
