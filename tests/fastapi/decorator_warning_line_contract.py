import linecache
import warnings

from fastapi import FastAPI
from fastapi.testclient import TestClient


def deprecate(label):
    warnings.warn(label, DeprecationWarning, stacklevel=2)
    return lambda function: function


app = FastAPI()


@app.get("/warning-line")
def warning_line():
    with warnings.catch_warnings(record=True) as captured:
        warnings.simplefilter("always")

        class Example:
            @deprecate("old")
            @classmethod
            def method(cls):
                return 1

    warning = captured[0]
    return {
        "source": linecache.getline(warning.filename, warning.lineno).strip(),
        "method": Example.method(),
    }


response = TestClient(app).get("/warning-line")
print(response.status_code, response.json())
