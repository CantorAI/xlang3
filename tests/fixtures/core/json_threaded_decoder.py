# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0
import json
import threading

value = {str(i): {"value": i, "nested": [i, True, None]} for i in range(180)}
payload = json.dumps(value)
decoder = json.JSONDecoder()
encoder = json.JSONEncoder(sort_keys=True)
errors = []


def worker(index):
    try:
        for iteration in range(500):
            # Default loads shares a decoder and its native key memo. An
            # explicit decoder/encoder must also support concurrent reuse.
            decoded = json.loads(payload) if index % 2 else decoder.decode(payload)
            assert len(decoded) == 180 and decoded["179"]["nested"] == [179, True, None]
            if iteration % 20 == 0:
                assert json.loads(encoder.encode(decoded)) == value
                assert decoder.decode('{"\\u0061":1,"a":2,"":3,"":4}') == {"a": 2, "": 4}
                try:
                    decoder.decode('{"broken":')
                except json.JSONDecodeError:
                    pass
                else:
                    raise AssertionError("Malformed input accepted")
    except Exception as error:
        errors.append(str(error))


threads = [threading.Thread(target=worker, args=(index,)) for index in range(6)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
assert not errors, errors
assert decoder.memo == {}
print("json-threaded-decoder-passed")
