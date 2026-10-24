"""Focused pure-Python Pickler workload with repeated bound ``write`` calls."""
import datetime
import io
import pickle


DATA = {
    "ads_flags": 0,
    "age": 18,
    "birthday": datetime.date(1980, 5, 7),
    "country": "BR",
    "encrypted_id": "G9urXXAJwjE",
    "flags": 412317970704,
    "gender": "m",
    "id": 302935349,
    "tags": ["a", "b", "c", "d", "e", "f", "g"],
    "time_created": 1225237014,
    "time_updated": 1233134493,
    "username": "collinwinter",
}


def main():
    output_bytes = 0
    # `_Pickler` deliberately selects pickle.py's Python implementation. Each
    # fresh writer captures `_Framer.write` in its `write` instance attribute;
    # the repeated opcode writes stress that fused bound-instance method path.
    for index in range(60):
        output = io.BytesIO()
        pickler = pickle._Pickler(output, protocol=5)
        pickler.dump((DATA, index, [DATA, DATA]))
        output_bytes += len(output.getvalue())
    print(output_bytes)


main()
