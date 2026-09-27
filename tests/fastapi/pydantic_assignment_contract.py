from pydantic import BaseModel, ConfigDict, ValidationError


class Model(BaseModel):
    model_config = ConfigDict(validate_assignment=True)
    value: int


model = Model(value=1)
model.value = 2
print("valid", model.value)
try:
    model.value = "bad"
except ValidationError as exc:
    print("invalid", exc.errors()[0]["type"], model.value)
