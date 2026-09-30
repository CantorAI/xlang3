import strawberry
from fastapi import FastAPI
from fastapi.testclient import TestClient
from strawberry.fastapi import GraphQLRouter


@strawberry.type
class User:
    name: str
    age: int


@strawberry.type
class Query:
    @strawberry.field
    def user(self) -> User:
        return User(name="Patrick", age=100)


app = FastAPI()
app.include_router(GraphQLRouter(strawberry.Schema(query=Query)), prefix="/graphql")

with TestClient(app) as client:
    response = client.post("/graphql", json={"query": "{ user { name age } }"})
    assert response.status_code == 200
    assert response.json() == {"data": {"user": {"name": "Patrick", "age": 100}}}
    print(response.status_code, response.json())
