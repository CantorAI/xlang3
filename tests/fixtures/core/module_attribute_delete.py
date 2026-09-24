import email.utils
import types
from unittest.mock import mock_open, patch


module = types.ModuleType("probe")
module.value = 7
namespace = module.__dict__
del module.value
print("value" in namespace, namespace is module.__dict__)
try:
    del module.value
except AttributeError:
    print("missing attribute")

for text in ("first", "second"):
    with patch("email.utils.open", mock_open(read_data=text)):
        print(email.utils.open().read())
    print("open" in email.utils.__dict__)
