def classify_url(value):
    url = str(value)
    url = url.lstrip()
    if ':' in url and not url.lower().startswith('http'):
        return 'other', url
    return 'http', url


for address in ('http://example.com/', '  https://example.com/', 'data:text/plain,hello'):
    print(classify_url(address))
