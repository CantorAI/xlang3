"""Public deterministic vectors: streaming padding and Fernet interoperability."""
import base64,json,sys
from pathlib import Path
from cryptography.exceptions import AlreadyFinalized
from cryptography.fernet import Fernet,InvalidToken
from cryptography.hazmat.primitives import padding

fixture=json.loads(Path(__file__).with_name('padding_fernet_vectors.json').read_text())
for row in fixture['padding']:
    factory=getattr(padding,row['kind'])(row['bits'])
    context=factory.padder()
    output=[context.update(bytes.fromhex(chunk)).hex() for chunk in row['input']]
    output.append(context.finalize().hex())
    assert output==row['output'],row
    try:context.update(b'')
    except AlreadyFinalized:pass
    else:raise AssertionError('Finalized padder accepted data')
    encoded=bytes.fromhex(''.join(output))
    for split in range(len(encoded)+1):
        context=factory.unpadder()
        decoded=context.update(memoryview(encoded)[:split])+context.update(bytearray(encoded[split:]))+context.finalize()
        assert decoded==bytes.fromhex(''.join(row['input']))
    invalid=bytearray(encoded);invalid[-1]=0
    context=factory.unpadder();context.update(invalid)
    try:context.finalize()
    except ValueError:pass
    else:raise AssertionError('Invalid padding accepted')
    try:context.finalize()
    except AlreadyFinalized:pass
    else:raise AssertionError('Invalid finalized context was reused')
for row in fixture['fernet']:
    key=row['key'].encode();token=row['token'].encode();data=bytes.fromhex(row['plaintext'])
    codec=Fernet(key)
    assert codec.decrypt(token)==data
    assert codec._encrypt_from_parts(data,row['timestamp'],bytes.fromhex(row['iv']))==token
    corrupt=bytearray(base64.urlsafe_b64decode(token));corrupt[-1]^=1
    try:codec.decrypt(base64.urlsafe_b64encode(corrupt))
    except InvalidToken:pass
    else:raise AssertionError('Invalid authentication tag accepted')
    try:codec.decrypt_at_time(token,60,row['timestamp']+61)
    except InvalidToken:pass
    else:raise AssertionError('Expired token accepted')
print('padding-streaming-finalization-and-fernet-interoperability-passed')
