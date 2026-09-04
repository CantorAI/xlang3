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

curl_client = http.Curl()
print(curl_client.setOpt(curl_client.URL, "http://127.0.0.1:18173/small"))
print(curl_client.perform())
print(curl_client.getInfo(curl_client.CURLINFO_RESPONSE_CODE))
print(curl_client.response)

print(client.get("/shutdown"))
print(client.status)
