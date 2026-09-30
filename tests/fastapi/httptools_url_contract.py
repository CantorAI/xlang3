from httptools.parser.errors import HttpParserInvalidURLError
from httptools.parser.url_parser import parse_url


for source in (
    b"/path?q=1#part",
    bytearray(b"https://u:p@example.org:8443/a"),
    memoryview(b"/memory?x=2"),
):
    parsed = parse_url(source)
    print(tuple(getattr(parsed, name) for name in (
        "schema", "host", "port", "path", "query", "fragment", "userinfo"
    )))

print(repr(parse_url(b"http://example.com/path")))

for source in (b"http://[", b"/" * 65536):
    try:
        parse_url(source)
    except HttpParserInvalidURLError as exc:
        print(type(exc).__name__, str(exc)[:92])
