from fastapi import FastAPI
from fastapi.testclient import TestClient


class Version:
    def __init__(self, major: int, minor: int):
        self.major = major
        self.minor = minor

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Version) and (self.major, self.minor) == (other.major, other.minor)

    def __lt__(self, other: 'Version') -> bool:
        return (self.major, self.minor) < (other.major, other.minor)

    def __gt__(self, other: 'Version') -> bool:
        return (self.major, self.minor) > (other.major, other.minor)


app = FastAPI()


@app.get('/version-choice')
def version_choice() -> dict:
    versions = [Version(1, 2), Version(1, 3), Version(2, 0)]
    key = lambda item: (0, item, ())
    maximum = max(versions, key=key)
    minimum = min(versions, key=key)
    return {'max': [maximum.major, maximum.minor],
            'min': [minimum.major, minimum.minor]}


with TestClient(app) as client:
    response = client.get('/version-choice')
    print(response.status_code, response.json())
