sources = (
    "from ...utils import marker",
    "from .... import marker",
    "from .....pkg.module import marker",
)
print(all(type(compile(source, "<relative-import>", "exec")).__name__ == "code" for source in sources))
