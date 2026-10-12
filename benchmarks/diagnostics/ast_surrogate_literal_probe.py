"""Record escaped-surrogate AST decoding independently of native repr."""

import ast
import json

original = "\ud800"
rendered = repr(original)
decoded = ast.literal_eval(rendered)
print(json.dumps({"input_repr": rendered, "decoded_repr": repr(decoded),
                  "decoded_matches": decoded == original}, indent=2))
