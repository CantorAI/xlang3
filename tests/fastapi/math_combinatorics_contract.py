import math

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


class Factor:
    def __init__(self, value):
        self.value = value

    def __mul__(self, other):
        return Factor(self.value * (other.value if isinstance(other, Factor) else other))

    def __rmul__(self, other):
        return Factor(other * self.value)


class Reflected:
    def __mul__(self, other):
        return NotImplemented

    def __rmul__(self, other):
        return 77


@app.get('/combinatorics')
def combinatorics() -> dict:
    return {
        'combination': math.comb(100, 50),
        'permutation': math.perm(20, 6),
        'factorial': math.factorial(25),
        'large_n': math.comb(10**25, 2),
        'log1p': math.log1p(1e-16),
        'product': math.prod([2, 3, 5], start=7),
        'protocol_product': math.prod([Factor(2), Factor(3)]).value,
        'reflected_product': math.prod([Reflected()]),
        'isqrt': math.isqrt(10**50),
    }


with TestClient(app) as client:
    response = client.get('/combinatorics')
    print(response.status_code, response.json())
