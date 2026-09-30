import re


pattern = re.compile(r"(?:(?<=:)|^)'((?:''|[^']++)*+)'(?::|$)", re.MULTILINE)
for text in ("'Harry''s':", "x:'A''B':", "'a''b':\n'z''y':"):
    print(pattern.findall(text))
