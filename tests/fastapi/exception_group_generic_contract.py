from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/exception-group-generic')
def exception_group_generic():
    return {
        'base': BaseExceptionGroup[Exception].__origin__ is BaseExceptionGroup,
        'specific': ExceptionGroup[ValueError].__origin__ is ExceptionGroup,
    }


response = TestClient(app).get('/exception-group-generic')
print(response.status_code, response.json())
