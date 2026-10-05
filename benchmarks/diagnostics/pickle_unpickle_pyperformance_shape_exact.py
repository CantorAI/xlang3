"""Directly run pyperformance 1.14.0 bm_pickle's unpickle workload shape.

The object definitions and 20-load loop follow the upstream benchmark. This
runs without pyperf calibration; it is a focused diagnostic, not an official
pyperf score.
"""
import datetime
import os
import random
import sys
import time

# pyperformance's --pure-python option sets this sentinel before importing
# pickle.py. Do the same here so both CPython and XLang3 measure
# pickle.Unpickler.loads implemented in Python, not the _pickle accelerator.
sys.modules['_pickle'] = None
import pickle

DICT = {
    "ads_flags": 0,
    "age": 18,
    "birthday": datetime.date(1980, 5, 7),
    "bulletin_count": 0,
    "comment_count": 0,
    "country": "BR",
    "encrypted_id": "G9urXXAJwjE",
    "favorite_count": 9,
    "first_name": "",
    "flags": 412317970704,
    "friend_count": 0,
    "gender": "m",
    "gender_for_display": "Male",
    "id": 302935349,
    "is_custom_profile_icon": 0,
    "last_name": "",
    "locale_preference": "pt_BR",
    "member": 0,
    "tags": ["a", "b", "c", "d", "e", "f", "g"],
    "profile_foo_id": 827119638,
    "secure_encrypted_id": "Z_xxx2dYx3t4YAdnmfgyKw",
    "session_number": 2,
    "signup_id": "201-19225-223",
    "status": "A",
    "theme": 1,
    "time_created": 1225237014,
    "time_updated": 1233134493,
    "unread_message_count": 0,
    "user_group": "0",
    "username": "collinwinter",
    "play_count": 9,
    "view_count": 7,
    "zip": "",
}
TUPLE = (
    [
        265867233, 265868503, 265252341, 265243910, 265879514,
        266219766, 266021701, 265843726, 265592821, 265246784,
        265853180, 45526486, 265463699, 265848143, 265863062,
        265392591, 265877490, 265823665, 265828884, 265753032,
    ],
    60,
)


def mutate_dict(orig_dict, random_source):
    new_dict = dict(orig_dict)
    for key, value in new_dict.items():
        rand_val = random_source.random() * sys.maxsize
        if isinstance(key, (int, bytes, str)):
            new_dict[key] = type(key)(rand_val)
    return new_dict


random_source = random.Random(5)
DICT_GROUP = [mutate_dict(DICT, random_source) for _ in range(3)]
loads = pickle.loads
payloads = tuple(pickle.dumps(obj, protocol=5) for obj in (DICT, TUPLE, DICT_GROUP))
loops = int(os.environ.get("PICKLE_DIAG_LOOPS", "300"))
range_it = range(loops)
start = time.perf_counter()
for _ in range_it:
    for payload in payloads:
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
        loads(payload)
elapsed = time.perf_counter() - start
print(f"official unpickle shape: {elapsed / loops * 1e6:.1f} us/loop; payloads={[len(p) for p in payloads]}")
