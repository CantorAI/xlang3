import sys
sys.path.insert(0, sys.argv[1])
from xlang_net import cypher

cypher.StorePath = sys.argv[2]
cypher.rsa_padding_mode = cypher.RSA_PKCS1_PADDING
mode = sys.argv[3]
public_path = sys.argv[2] + '/public.pem'
cipher_path = sys.argv[2] + '/message.bin'
message = b'\x00cantor-registry-restart\xff'
if mode == 'create':
    public_key = cypher.generate_key_pair(2048, 'node_test')
    assert public_key.startswith('-----BEGIN PUBLIC KEY-----')
    assert cypher.get_public_key('node_test') == public_key
    with open(public_path, 'w') as stream:
        stream.write(public_key)
    with open(cipher_path, 'wb') as stream:
        stream.write(cypher.encrypt_with_private_key(message, 'node_test'))
elif mode == 'reload':
    with open(public_path) as stream:
        expected = stream.read()
    assert cypher.get_public_key('node_test') == expected
    with open(cipher_path, 'rb') as stream:
        cipher = stream.read()
    assert cypher.decrypt_with_public_key(cipher, expected) == message
    encrypted = cypher.encrypt_with_public_key(message, expected)
    assert cypher.decrypt_with_private_key(encrypted, 'node_test') == message
elif mode == 'invalid':
    assert cypher.get_public_key('missing') == ''
    assert cypher.get_public_key('corrupt') == ''
else:
    raise RuntimeError('unknown test mode')
print('cypher-key-store-' + mode + '-passed')
