import sys
import threading
import time
if len(sys.argv) > 1:
    sys.path.insert(0, sys.argv[1])
from xlang_net import http

client = http.Client("http://127.0.0.1:18173")

print(client.get("/small"))
print(client.status)
print(client.body)
print(client.response_headers["X-Test"])

print(client.get("/large"))
print(len(client.body))

print(client.get("/binary"))
print(len(client.body))

concurrent_results = [None, None]
def get_slow(index):
    concurrent_client = http.Client("http://127.0.0.1:18173")
    concurrent_results[index] = (
        concurrent_client.get("/slow"),
        concurrent_client.status,
        concurrent_client.body,
    )

started = time.monotonic()
threads = [threading.Thread(target=get_slow, args=(index,)) for index in range(2)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
elapsed = time.monotonic() - started
print(concurrent_results == [(True, 200, "slow"), (True, 200, "slow")])
print(elapsed < 0.85)

curl_client = http.Curl()
print(curl_client.setOpt(curl_client.URL, "http://127.0.0.1:18173/small"))
print(curl_client.perform())
print(curl_client.getInfo(curl_client.CURLINFO_RESPONSE_CODE))
print(curl_client.response)

print(client.get("/shutdown"))
print(client.status)
