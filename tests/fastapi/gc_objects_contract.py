import gc

from fastapi import FastAPI
from fastapi.testclient import TestClient


class Marker:
    pass


app = FastAPI()


@app.get("/gc-objects")
async def gc_objects():
    marker = Marker()
    values = [marker]
    mapping = {"marker": marker}
    objects = gc.get_objects()
    return {
        "marker_present": any(item is marker for item in objects),
        "list_present": any(item is values for item in objects),
        "dict_present": any(item is mapping for item in objects),
        "atomic_absent": not any(type(item) is str for item in objects),
        "tracked_instance": gc.is_tracked(marker),
        "tracked_list": gc.is_tracked(values),
        "tracked_dict": gc.is_tracked(mapping),
        "untracked_empty_tuple": not gc.is_tracked(()),
        "untracked_bytes": not gc.is_tracked(b"bytes"),
    }


with TestClient(app) as client:
    response = client.get("/gc-objects")
    print(response.status_code)
    print(response.json())
