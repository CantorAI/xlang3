import annotationlib
import decimal
from typing import Annotated


def define():
    class Model:
        value: Annotated[int, decimal.Decimal("1")]

    return Model


model = define()
annotation = annotationlib.call_annotate_function(
    model.__annotate__, format=annotationlib.Format.FORWARDREF, owner=model
)["value"]
print(annotation.__origin__ is int, annotation.__metadata__[0] == decimal.Decimal(1))
