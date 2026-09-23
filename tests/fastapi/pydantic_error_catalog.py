import hashlib
import json

from pydantic_core._pydantic_core import list_all_errors


errors = list_all_errors()
canonical = json.dumps(errors, sort_keys=True, ensure_ascii=True, separators=(",", ":"))
print(len(errors), len({error["type"] for error in errors}))
print([error["type"] for error in errors[:4]])
print(hashlib.sha256(canonical.encode()).hexdigest())
