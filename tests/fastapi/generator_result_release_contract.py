import gc
import json

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/generator-result-release")
async def generator_result_release():
    marker = []

    def generator_result():
        return marker
        yield

    generator = generator_result()
    results = []
    for _ in range(2):
        try:
            next(generator)
        except StopIteration as error:
            results.append(error.value is marker)

    async def rejected_future():
        import asyncio

        future = asyncio.get_running_loop().create_future()
        error = OSError("sample")
        future.set_exception(error)
        try:
            await future
        except OSError as caught:
            return caught

    error = await rejected_future()
    referrers = sorted(type(item).__name__ for item in gc.get_referrers(error))
    return {"generator_results": results, "referrers": referrers}


response = TestClient(app).get("/generator-result-release")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
