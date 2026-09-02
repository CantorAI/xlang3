# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import email.message


class Plain:
    pass


class Wrapped(email.message.Message):
    pass


print("plain-hook", Plain.__getattribute__)
print("wrapped-hook", Wrapped.__getattribute__)
print("object-hook", object.__getattribute__)
