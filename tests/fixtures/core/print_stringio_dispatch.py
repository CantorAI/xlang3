import io


default_stream = io.StringIO()
print("fast-print", file=default_stream)
print(repr(default_stream.getvalue()))

translated_stream = io.StringIO(newline="\r\n")
print("translated", file=translated_stream)
print(repr(translated_stream.getvalue()))

overridden_stream = io.StringIO()
overridden_stream.write = lambda text: "ignored-write-result"
print("override", file=overridden_stream)
print(repr(overridden_stream.getvalue()))
