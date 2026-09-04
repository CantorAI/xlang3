from xlang_net import http
from xlang_net import cypher
from xlang_net import smtp
import xlang_net.http as http_direct

print(http.WritePad("ok"))
print(http_direct.WritePad("direct"))
c = http.Curl()
print(http.Curl.CURLOPT_URL == http.Curl.URL)
print(c.CURLINFO_RESPONSE_CODE > 0)
print(c.escape("a b"))
print(c.unescape("a%20b"))
print(cypher.RSA_PKCS1_OAEP_PADDING)
cypher.StorePath = "xlang3_net_crypto_test"
cypher.rsa_padding_mode = cypher.RSA_PKCS1_OAEP_PADDING
public_key = cypher.generate_key_pair(1024, "test_key")
print(public_key.startswith("-----BEGIN PUBLIC KEY-----"))
encrypted = cypher.encrypt_with_public_key(b"\x00xlang3-binary", public_key)
print(type(encrypted).__name__, len(encrypted) > 0)
decrypted = cypher.decrypt_with_private_key(encrypted, "test_key")
print(decrypted == b"\x00xlang3-binary")
print(cypher.remove_private_key("test_key"))
smtp.smtp_server = "smtp.example.invalid"
smtp.smtp_port = 2525
smtp.smtp_scope = "scope"
print(smtp.smtp_server, smtp.smtp_port, smtp.smtp_scope)

server = http.Server()
print(server.SupportStaticFiles)
print(server.StaticIndexFile)
print(server.getMimeType("html")[0])
