from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, TypeAdapter


class Record(BaseModel):
    name: str
    enabled: bool = True
    note: str | None = None


records = [Record(name="one"), Record(name="two", enabled=False, note="set")]
print("list", TypeAdapter(list[Record]).dump_python(records, exclude_unset=True))
print("dict", TypeAdapter(dict[str, Record]).dump_python(
    {"first": records[0], "second": records[1]}, exclude_unset=True))


app = FastAPI()


@app.get("/records", response_model=list[Record], response_model_exclude_unset=True)
def get_records():
    return records


with TestClient(app) as client:
    response = client.get("/records")
    print("route", response.status_code, response.json())
