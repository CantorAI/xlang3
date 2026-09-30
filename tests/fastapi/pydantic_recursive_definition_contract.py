from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import SchemaValidator, core_schema


class Branch:
    pass


reference = core_schema.definition_reference_schema(schema_ref="Branch")
validator = SchemaValidator(
    core_schema.definitions_schema(
        reference,
        [
            core_schema.model_schema(
                Branch,
                core_schema.model_fields_schema(
                    {
                        "width": core_schema.model_field(core_schema.int_schema()),
                        "branch": core_schema.model_field(
                            core_schema.with_default_schema(
                                core_schema.nullable_schema(reference), default=None
                            )
                        ),
                    }
                ),
                ref="Branch",
            )
        ],
    )
)

app = FastAPI()


@app.post("/nested")
def validate_nested(data: dict):
    branch = validator.validate_python(data)
    depth = 0
    last = branch.width
    while branch.branch is not None:
        branch = branch.branch
        depth += 1
        last = branch.width
    return {"depth": depth, "first": data["width"], "last": last}


with TestClient(app) as client:
    for length in (12, 48, 97):
        data = {"width": -1}
        branch = data
        for width in range(length):
            branch["branch"] = {"width": width}
            branch = branch["branch"]
        response = client.post("/nested", json=data)
        assert response.status_code == 200, response.text
        assert response.json() == {"depth": length, "first": -1, "last": length - 1}
        print(length, response.status_code, response.json())
